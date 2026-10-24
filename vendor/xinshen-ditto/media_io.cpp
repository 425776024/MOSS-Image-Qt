#include "media_io.h"
#include "avatar_runtime.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cmath>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {
std::runtime_error fferror(const char* operation, int code) {
  char message[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(code, message, sizeof(message));
  return std::runtime_error(std::string(operation) + ": " + message);
}
void check(int code, const char* operation) {
  if (code < 0) throw fferror(operation, code);
}
struct Input {
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  int stream = -1;
  explicit Input(const std::filesystem::path& path, AVMediaType type) {
    check(avformat_open_input(&format, path.string().c_str(), nullptr, nullptr), "Open media");
    check(avformat_find_stream_info(format, nullptr), "Find streams");
    stream = av_find_best_stream(format, type, -1, -1, nullptr, 0);
    check(stream, "Find requested stream");
    const auto* decoder = avcodec_find_decoder(format->streams[stream]->codecpar->codec_id);
    if (!decoder) throw std::runtime_error(std::string("Media decoder unavailable: ") +
        avcodec_get_name(format->streams[stream]->codecpar->codec_id));
    codec = avcodec_alloc_context3(decoder);
    if (!codec) throw std::runtime_error("Allocate media decoder");
    check(avcodec_parameters_to_context(codec, format->streams[stream]->codecpar), "Copy codec parameters");
    check(avcodec_open2(codec, decoder, nullptr), "Open media decoder");
  }
  ~Input() {
    avcodec_free_context(&codec);
    avformat_close_input(&format);
  }
};
}  // namespace

std::vector<float> decodeAudio16k(const std::filesystem::path& path) {
  Input input(path, AVMEDIA_TYPE_AUDIO);
  AVChannelLayout mono;
  av_channel_layout_default(&mono, 1);
  SwrContext* resampler = nullptr;
  check(swr_alloc_set_opts2(&resampler, &mono, AV_SAMPLE_FMT_FLT, 16000,
                            &input.codec->ch_layout, input.codec->sample_fmt,
                            input.codec->sample_rate, 0, nullptr), "Set audio resampler");
  check(swr_init(resampler), "Initialize audio resampler");
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  std::vector<float> samples;
  auto drain = [&]() {
    while (true) {
      avatar::checkCancelled();
      const int result = avcodec_receive_frame(input.codec, frame);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
      check(result, "Decode audio");
      const int capacity = static_cast<int>(av_rescale_rnd(
          swr_get_delay(resampler, input.codec->sample_rate) + frame->nb_samples,
          16000, input.codec->sample_rate, AV_ROUND_UP));
      std::vector<float> buffer(capacity);
      uint8_t* output[] = {reinterpret_cast<uint8_t*>(buffer.data())};
      const int count = swr_convert(resampler, output, capacity,
                                    const_cast<const uint8_t**>(frame->extended_data),
                                    frame->nb_samples);
      check(count, "Resample audio");
      samples.insert(samples.end(), buffer.begin(), buffer.begin() + count);
      av_frame_unref(frame);
    }
  };
  while (av_read_frame(input.format, packet) >= 0) {
    avatar::checkCancelled();
    if (packet->stream_index == input.stream) {
      check(avcodec_send_packet(input.codec, packet), "Submit audio packet");
      drain();
    }
    av_packet_unref(packet);
  }
  check(avcodec_send_packet(input.codec, nullptr), "Flush audio decoder");
  drain();
  // Resampling retains a filter tail even after the decoder reaches EOF.
  // Drain it before validating a clip whose range reaches the source end.
  while (true) {
    std::vector<float> buffer(4096);
    uint8_t* output[] = {reinterpret_cast<uint8_t*>(buffer.data())};
    const int count = swr_convert(resampler, output, buffer.size(), nullptr, 0);
    check(count, "Flush audio resampler");
    if (count == 0) break;
    samples.insert(samples.end(), buffer.begin(), buffer.begin() + count);
  }
  av_frame_free(&frame);
  av_packet_free(&packet);
  swr_free(&resampler);
  av_channel_layout_uninit(&mono);
  if (samples.empty()) throw std::runtime_error("Audio file contains no decoded samples");
  return samples;
}

struct VideoReader::State {
  Input input;
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  SwsContext* scaler = nullptr;
  bool draining = false;
  MediaInfo info;
  int64_t frameIndex = 0;
  Image selected, ahead;
  bool hasSelected = false, hasAhead = false;
  explicit State(const std::filesystem::path& path) : input(path, AVMEDIA_TYPE_VIDEO) {
    auto* stream = input.format->streams[input.stream];
    info.width = input.codec->width; info.height = input.codec->height;
    const auto fps = av_guess_frame_rate(input.format, stream, nullptr);
    if (fps.num > 0 && fps.den > 0) { info.fpsNum = fps.num; info.fpsDen = fps.den; }
    if (stream->duration != AV_NOPTS_VALUE)
      info.durationUs = av_rescale_q(stream->duration, stream->time_base, AV_TIME_BASE_Q);
    else if (input.format->duration != AV_NOPTS_VALUE) info.durationUs = input.format->duration;
    const auto* desc = av_pix_fmt_desc_get(input.codec->pix_fmt);
    info.hasAlpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    const auto sar = av_guess_sample_aspect_ratio(input.format, stream, nullptr);
    if (sar.num > 0 && sar.den > 0) info.pixelAspect = av_q2d(sar);
    const auto* side = av_packet_side_data_get(stream->codecpar->coded_side_data,
        stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (side && side->size >= 9 * sizeof(int32_t)) {
      const auto angle = -av_display_rotation_get(reinterpret_cast<const int32_t*>(side->data));
      if (std::isfinite(angle)) info.rotation = (int(std::round(angle / 90)) * 90 % 360 + 360) % 360;
    }
    const std::string format = input.format->iformat->name;
    info.still = format.find("image2") != std::string::npos || format.find("_pipe") != std::string::npos;
    info.width = std::max(1, int(std::round(info.width * info.pixelAspect)));
    if (info.rotation == 90 || info.rotation == 270) std::swap(info.width, info.height);
  }
  ~State() {
    sws_freeContext(scaler);
    av_frame_free(&frame);
    av_packet_free(&packet);
  }
};

VideoReader::VideoReader(const std::filesystem::path& path) : state_(std::make_unique<State>(path)) {}
VideoReader::~VideoReader() = default;
MediaInfo VideoReader::info() const { return state_->info; }
void applyVideoAlpha(Image& image, const Image& matte) {
  if (image.width != matte.width || image.height != matte.height ||
      matte.bgr.size() != size_t(image.width) * image.height * 3)
    throw std::runtime_error("Avatar alpha dimensions do not match the source frame");
  const auto count = size_t(image.width) * image.height;
  std::vector<uint8_t> alpha(count);
  bool opaque = true;
  for (size_t i = 0; i < count; ++i) {
    const auto* p = matte.bgr.data() + i * 3;
    alpha[i] = (722U*p[0] + 7152U*p[1] + 2126U*p[2] + 5000U) / 10000U;
    opaque = opaque && alpha[i] >= 253;
  }
  if (opaque) return;
  if (!image.alpha.empty()) {
    for (size_t i = 0; i < count; ++i) alpha[i] = (unsigned(alpha[i]) * image.alpha[i] + 127) / 255;
  } else {
    // Only infer a white backplate from abundant, nearly uniform transparent
    // source pixels. Colored/photo backgrounds and intrinsically-alpha sources
    // retain ordinary straight-alpha handling. Sampling avoids another full
    // image pass and the median is stable against codec ringing.
    std::array<std::array<unsigned,256>,3> histogram{};
    unsigned samples=0, white=0;
    for (int y=0;y<image.height;y+=16) for (int x=0;x<image.width;x+=16) {
      const auto i=size_t(y)*image.width+x;
      if (alpha[i]>2) continue;
      const auto* p=image.bgr.data()+i*3;
      ++samples;
      if (std::min({p[0],p[1],p[2]})>=245) ++white;
      for (size_t c=0;c<3;++c) ++histogram[c][p[c]];
    }
    if (samples>=64 && white*100>=samples*99) {
      std::array<uint8_t,3> background{};
      for (size_t c=0;c<3;++c) {
        unsigned sum=0;
        for (unsigned value=0;value<256;++value) {
          sum+=histogram[c][value];
          if (sum*2>=samples) { background[c]=value; break; }
        }
      }
      image.matteBackgroundBgr=background;
    }
  }
  image.alpha = std::move(alpha);
}
int64_t VideoReader::quantizeTimeUs(int64_t timeUs) const {
  const auto base = state_->input.format->streams[state_->input.stream]->time_base;
  return av_rescale_q(av_rescale_q(timeUs, AV_TIME_BASE_Q, base), base, AV_TIME_BASE_Q);
}

bool VideoReader::next(Image& out) {
  auto& s = *state_;
  while (true) {
    avatar::checkCancelled();
    const int result = avcodec_receive_frame(s.input.codec, s.frame);
    if (result == 0) {
      const int width = s.frame->width, height = s.frame->height;
      const int squareWidth = std::max(1, int(std::round(width * s.info.pixelAspect)));
      const auto* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(s.frame->format));
      const bool hasAlpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
      // Legacy body/cull/blend videos are opaque. Decode directly to the BGR
      // consumed by their renderer, avoiding a full-resolution BGRA allocation
      // and a second color conversion on every frame.
      cv::Mat pixels(height, squareWidth, hasAlpha ? CV_8UC4 : CV_8UC3);
      s.scaler = sws_getCachedContext(s.scaler, width, height,
          static_cast<AVPixelFormat>(s.frame->format), squareWidth, height,
          hasAlpha ? AV_PIX_FMT_BGRA : AV_PIX_FMT_BGR24,
          SWS_BILINEAR, nullptr, nullptr, nullptr);
      if (!s.scaler) throw std::runtime_error("Create video color converter");
      uint8_t* output[] = {pixels.data};
      const int stride[] = {static_cast<int>(pixels.step)};
      sws_scale(s.scaler, s.frame->data, s.frame->linesize, 0, height, output, stride);
      if (s.info.rotation == 90) cv::rotate(pixels, pixels, cv::ROTATE_90_CLOCKWISE);
      if (s.info.rotation == 180) cv::rotate(pixels, pixels, cv::ROTATE_180);
      if (s.info.rotation == 270) cv::rotate(pixels, pixels, cv::ROTATE_90_COUNTERCLOCKWISE);
      out.width = pixels.cols; out.height = pixels.rows;
      out.alpha.clear();
      out.matteBackgroundBgr.reset();
      cv::Mat bgr;
      if (hasAlpha) cv::cvtColor(pixels, bgr, cv::COLOR_BGRA2BGR);
      else bgr = pixels;
      out.bgr.assign(bgr.data, bgr.data + bgr.total() * 3);
      if (hasAlpha) {
        cv::Mat alpha; cv::extractChannel(pixels, alpha, 3);
        out.alpha.assign(alpha.data, alpha.data + alpha.total());
        s.info.hasAlpha = true;
      }
      auto* stream = s.input.format->streams[s.input.stream];
      const auto pts = s.frame->best_effort_timestamp;
      const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
      out.ptsUs = pts == AV_NOPTS_VALUE ? av_rescale_q(s.frameIndex, {s.info.fpsDen, s.info.fpsNum}, AV_TIME_BASE_Q)
          : av_rescale_q(pts - start, stream->time_base, AV_TIME_BASE_Q);
      out.durationUs = s.frame->duration > 0 ? av_rescale_q(s.frame->duration, stream->time_base, AV_TIME_BASE_Q)
          : av_rescale_q(1, {s.info.fpsDen, s.info.fpsNum}, AV_TIME_BASE_Q);
      ++s.frameIndex;
      av_frame_unref(s.frame);
      return true;
    }
    if (result == AVERROR_EOF) return false;
    if (result != AVERROR(EAGAIN)) throw fferror("Decode video", result);
    bool submitted = false;
    while (av_read_frame(s.input.format, s.packet) >= 0) {
      if (s.packet->stream_index == s.input.stream) {
        check(avcodec_send_packet(s.input.codec, s.packet), "Submit video packet");
        submitted = true;
      }
      av_packet_unref(s.packet);
      if (submitted) break;
    }
    if (!submitted) {
      if (s.draining) return false;
      check(avcodec_send_packet(s.input.codec, nullptr), "Flush video decoder");
      s.draining = true;
    }
  }
}

void VideoReader::rewind() {
  auto& s = *state_;
  check(av_seek_frame(s.input.format, s.input.stream, 0, AVSEEK_FLAG_BACKWARD), "Rewind pose video");
  avcodec_flush_buffers(s.input.codec);
  s.draining = false;
  s.frameIndex = 0;
  s.hasSelected = s.hasAhead = false;
}

bool VideoReader::readAt(int64_t timeUs, Image& out) {
  auto& s = *state_;
  timeUs = std::max<int64_t>(0, timeUs);
  if (s.info.still && s.hasSelected) { out = s.selected; return true; }
  if (!s.hasSelected || timeUs < s.selected.ptsUs || timeUs > s.selected.ptsUs + 1000000) {
    if (!s.info.still) {
      auto* stream = s.input.format->streams[s.input.stream];
      const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
      check(av_seek_frame(s.input.format, s.input.stream,
          start + av_rescale_q(timeUs, AV_TIME_BASE_Q, stream->time_base), AVSEEK_FLAG_BACKWARD), "Seek avatar frame");
      avcodec_flush_buffers(s.input.codec); s.draining = false;
      s.frameIndex = timeUs * s.info.fpsNum / (1000000LL * s.info.fpsDen);
    }
    s.hasSelected = next(s.selected); s.hasAhead = false;
    if (!s.hasSelected) return false;
  }
  while (!s.info.still) {
    if (!s.hasAhead) s.hasAhead = next(s.ahead);
    if (!s.hasAhead || s.ahead.ptsUs > timeUs) break;
    s.selected = std::move(s.ahead); s.hasAhead = false;
  }
  out = s.selected;
  return true;
}

struct VideoWriter::State {
  AVFormatContext* format = nullptr;
  AVCodecContext* video = nullptr;
  AVCodecContext* audio = nullptr;
  AVStream* videoStream = nullptr;
  AVStream* audioStream = nullptr;
  AVFrame* videoFrame = av_frame_alloc();
  AVFrame* audioFrame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  SwsContext* scaler = nullptr;
  const std::vector<float>& samples;
  int64_t videoIndex = 0;
  int64_t audioIndex = 0;
  bool finished = false;
  bool preserveGeometry = false;
  bool sourceTimestamps = false;
  int64_t packetDuration = 1;
  explicit State(const std::filesystem::path& path, const std::vector<float>& audioSamples,
                 int width, int height, bool includeAudio, bool preserve,
                 int fpsNum, int fpsDen, bool lossless, bool timestamps)
      : samples(audioSamples), preserveGeometry(preserve), sourceTimestamps(timestamps) {
    if (width <= 0 || height <= 0 || (!lossless && (width % 2 || height % 2)))
      throw std::runtime_error("H.264 source geometry must have positive even dimensions");
    if (fpsNum <= 0 || fpsDen <= 0 || double(fpsNum) / fpsDen > 240)
      throw std::runtime_error("Invalid output frame rate");
    check(avformat_alloc_output_context2(&format, nullptr, lossless ? "matroska" : "mp4", path.string().c_str()), "Create video");
    const AVCodec* videoEncoder = avcodec_find_encoder_by_name(lossless ? "ffv1" :
#ifdef _WIN32
        "h264_mf"
#else
        "h264_videotoolbox"
#endif
    );
    if (!videoEncoder) throw std::runtime_error("Video encoder unavailable");
    videoStream = avformat_new_stream(format, nullptr);
    video = avcodec_alloc_context3(videoEncoder);
    video->width = width; video->height = height;
    video->pix_fmt = lossless ? AV_PIX_FMT_BGRA : AV_PIX_FMT_YUV420P;
    video->time_base = timestamps ? AVRational{1, 1000000} : AVRational{fpsDen, fpsNum};
    video->framerate = AVRational{fpsNum, fpsDen};
    video->gop_size = 50; video->max_b_frames = 0;
    if (format->oformat->flags & AVFMT_GLOBALHEADER) video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = nullptr;
    if (lossless) { video->level = 3; }
    else {
      video->bit_rate = std::max<int64_t>(4000000, int64_t(width)*height*fpsNum/fpsDen/5);
#ifdef _WIN32
      // Keep the system encoder's software default available on machines
      // without an H.264 hardware encoder. Neural inference uses the GPU.
      av_dict_set(&options, "hw_encoding", "0", 0);
#else
      av_dict_set(&options, "allow_sw", "0", 0);
#endif
    }
    check(avcodec_open2(video, videoEncoder, &options), "Open H.264 encoder");
    av_dict_free(&options);
    check(avcodec_parameters_from_context(videoStream->codecpar, video), "Set video stream");
    videoStream->time_base = video->time_base;

    if (includeAudio) {
    const AVCodec* audioEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audioEncoder) throw std::runtime_error("AAC encoder unavailable");
    audioStream = avformat_new_stream(format, nullptr);
    audio = avcodec_alloc_context3(audioEncoder);
    audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audio->sample_rate = 16000;
    audio->time_base = AVRational{1, 16000};
    audio->bit_rate = 64000;
    av_channel_layout_default(&audio->ch_layout, 1);
    if (format->oformat->flags & AVFMT_GLOBALHEADER) audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(audio, audioEncoder, nullptr), "Open AAC encoder");
    check(avcodec_parameters_from_context(audioStream->codecpar, audio), "Set audio stream");
    audioStream->time_base = audio->time_base;
    }

    videoFrame->format = video->pix_fmt;
    videoFrame->width = video->width; videoFrame->height = video->height;
    check(av_frame_get_buffer(videoFrame, 32), "Allocate video frame");
    if (audio) {
    audioFrame->format = audio->sample_fmt;
    audioFrame->sample_rate = audio->sample_rate;
    audioFrame->nb_samples = audio->frame_size;
    check(av_channel_layout_copy(&audioFrame->ch_layout, &audio->ch_layout), "Set audio layout");
    check(av_frame_get_buffer(audioFrame, 0), "Allocate audio frame");
    }
    check(avio_open(&format->pb, path.string().c_str(), AVIO_FLAG_WRITE), "Open output file");
    check(avformat_write_header(format, nullptr), "Write MP4 header");
  }
  ~State() {
    sws_freeContext(scaler);
    av_packet_free(&packet);
    av_frame_free(&audioFrame);
    av_frame_free(&videoFrame);
    avcodec_free_context(&audio);
    avcodec_free_context(&video);
    if (format) {
      if (format->pb) avio_closep(&format->pb);
      avformat_free_context(format);
    }
  }
  void drain(AVCodecContext* codec, AVStream* stream) {
    while (true) {
      const int result = avcodec_receive_packet(codec, packet);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
      check(result, "Encode packet");
      // H.264 packets may omit duration. Preserve the final 25 fps frame in
      // MP4's media duration so a native trim can reach the intended end.
      if (codec == video && packet->duration <= 0) packet->duration = packetDuration;
      av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
      packet->stream_index = stream->index;
      check(av_interleaved_write_frame(format, packet), "Mux MP4 packet");
      av_packet_unref(packet);
    }
  }
  void writeAudio(bool pad) {
    if (audioIndex >= static_cast<int64_t>(samples.size())) return;
    check(av_frame_make_writable(audioFrame), "Make audio frame writable");
    float* dst = reinterpret_cast<float*>(audioFrame->data[0]);
    const int count = std::min<int64_t>(audio->frame_size, samples.size() - audioIndex);
    std::memcpy(dst, samples.data() + audioIndex, count * sizeof(float));
    if (count < audio->frame_size) {
      if (!pad) return;
      std::fill(dst + count, dst + audio->frame_size, 0.0f);
    }
    // AAC accepts a short final input frame. Preserve the actual sample range
    // so the muxer trims encoder padding instead of extending the timeline.
    audioFrame->nb_samples = count;
    audioFrame->pts = audioIndex;
    check(avcodec_send_frame(audio, audioFrame), "Submit AAC frame");
    drain(audio, audioStream);
    audioIndex += count;
  }
};

VideoWriter::VideoWriter(const std::filesystem::path& path, const std::vector<float>& samples,
                         int width, int height, bool includeAudio, bool preserveGeometry,
                         int fpsNum, int fpsDen, bool lossless, bool sourceTimestamps)
    : state_(std::make_unique<State>(path, samples, width, height, includeAudio, preserveGeometry,
                                    fpsNum, fpsDen, lossless, sourceTimestamps)) {}
VideoWriter::~VideoWriter() = default;

void VideoWriter::write(const Image& image) {
  auto& s = *state_;
  avatar::checkCancelled();
  if (s.finished) throw std::runtime_error("MP4 already finished");
  if (image.width < 1 || image.height < 1) throw std::runtime_error("Source video frame too small");
  if (s.preserveGeometry && (image.width != s.video->width || image.height != s.video->height))
    throw std::runtime_error("Avatar frame geometry changed during generation");
  const int cropWidth = s.preserveGeometry ? image.width : std::min(image.width, image.height * 9 / 16);
  const int cropHeight = s.preserveGeometry ? image.height : std::min(image.height, image.width * 16 / 9);
  const int cropX = (image.width - cropWidth) / 2;
  const int cropY = (image.height - cropHeight) / 2;
  cv::Mat packed;
  if (s.video->pix_fmt == AV_PIX_FMT_BGRA) {
    cv::Mat bgr(image.height, image.width, CV_8UC3, const_cast<uint8_t*>(image.bgr.data()));
    cv::cvtColor(bgr, packed, cv::COLOR_BGR2BGRA);
    if (!image.alpha.empty()) {
      cv::Mat alpha(image.height, image.width, CV_8UC1, const_cast<uint8_t*>(image.alpha.data()));
      cv::insertChannel(alpha, packed, 3);
    }
  }
  const bool rgba = !packed.empty();
  s.scaler = sws_getCachedContext(s.scaler, cropWidth, cropHeight, rgba ? AV_PIX_FMT_BGRA : AV_PIX_FMT_BGR24,
      s.video->width, s.video->height, s.video->pix_fmt, SWS_BILINEAR,
      nullptr, nullptr, nullptr);
  if (!s.scaler) throw std::runtime_error("Create export frame scaler");
  check(av_frame_make_writable(s.videoFrame), "Make video frame writable");
  const uint8_t* source[] = {rgba ? packed.ptr(cropY) + cropX * 4 : image.pixel(cropX, cropY)};
  const int sourceStride[] = {image.width * (rgba ? 4 : 3)};
  sws_scale(s.scaler, source, sourceStride, 0, cropHeight,
            s.videoFrame->data, s.videoFrame->linesize);
  s.videoFrame->pts = s.sourceTimestamps ? image.ptsUs : s.videoIndex;
  s.packetDuration = s.sourceTimestamps ? image.durationUs : 1;
  s.videoFrame->duration = s.packetDuration;
  ++s.videoIndex;
  check(avcodec_send_frame(s.video, s.videoFrame), "Submit H.264 frame");
  s.drain(s.video, s.videoStream);
  const int64_t audioLimit = std::min<int64_t>(s.samples.size(),
      av_rescale_q(s.videoIndex, av_inv_q(s.video->framerate), {1,16000}));
  if (s.audio) while (s.audioIndex + s.audio->frame_size <= audioLimit) s.writeAudio(false);
}

void VideoWriter::finish() {
  auto& s = *state_;
  if (s.finished) return;
  if (s.audio) while (s.audioIndex < static_cast<int64_t>(s.samples.size())) s.writeAudio(true);
  check(avcodec_send_frame(s.video, nullptr), "Flush H.264 encoder");
  s.drain(s.video, s.videoStream);
  if (s.audio) {
    check(avcodec_send_frame(s.audio, nullptr), "Flush AAC encoder");
    s.drain(s.audio, s.audioStream);
  }
  check(av_write_trailer(s.format), "Finalize MP4");
  s.finished = true;
}

void writeImagePng(const std::filesystem::path& path, const Image& image) {
  struct Encoder {
    AVCodecContext* codec = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    ~Encoder() { avcodec_free_context(&codec); av_frame_free(&frame); av_packet_free(&packet); }
  } state;
  const auto* codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
  if (!codec) throw std::runtime_error("PNG encoder unavailable");
  state.codec = avcodec_alloc_context3(codec);
  if (!state.codec || !state.frame || !state.packet) throw std::runtime_error("PNG allocation failed");
  state.codec->width = image.width; state.codec->height = image.height;
  const int channels = image.alpha.empty() ? 3 : 4;
  state.codec->pix_fmt = channels == 3 ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_RGBA;
  state.codec->time_base = AVRational{1, 25};
  check(avcodec_open2(state.codec, codec, nullptr), "Open PNG encoder");
  state.frame->width = image.width; state.frame->height = image.height;
  state.frame->format = state.codec->pix_fmt;
  check(av_frame_get_buffer(state.frame, 32), "Allocate PNG frame");
  for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
    const auto* source = image.pixel(x, y);
    auto* output = state.frame->data[0] + y * state.frame->linesize[0] + x * channels;
    output[0] = source[2]; output[1] = source[1]; output[2] = source[0];
    if (channels == 4) output[3] = image.alpha[y * image.width + x];
  }
  check(avcodec_send_frame(state.codec, state.frame), "Submit PNG frame");
  check(avcodec_receive_packet(state.codec, state.packet), "Encode PNG frame");
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(state.packet->data), state.packet->size);
  if (!output) throw std::runtime_error("Write avatar preview failed");
}
