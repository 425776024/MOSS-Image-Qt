#include "timeline_render.h"
#include "media_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {
namespace fs = std::filesystem;
using avatar::Json;

double number(const Json& object, const char* key, double fallback) {
    const double value = object.value(key, fallback);
    if (!std::isfinite(value)) throw std::runtime_error(std::string("无效的时间轴数值：") + key);
    return value;
}

fs::path localPath(std::string value) {
    if (value.rfind("file://", 0) == 0) {
        value.erase(0, 7);
        if (value.rfind("localhost/", 0) == 0) value.erase(0, 9);
        if (value.empty() || value.front() != '/') throw std::runtime_error("只支持本地轨道素材");
        std::string decoded;
        const auto hex = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '%' && i + 2 < value.size() &&
                hex(value[i + 1]) >= 0 && hex(value[i + 2]) >= 0) {
                const char c = char(hex(value[i + 1]) * 16 + hex(value[i + 2]));
                if (!c) throw std::runtime_error("无效的素材路径");
                decoded += c; i += 2;
            } else decoded += value[i];
        }
        value = std::move(decoded);
    }
    return fs::path(value);
}

cv::Scalar color(const std::string& value) {
    if (value.size() != 7 || value.front() != '#' ||
        value.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
        throw std::runtime_error("背景颜色需要使用 #RRGGBB 格式");
    const auto rgb = std::stoul(value.substr(1), nullptr, 16);
    return {double(rgb & 255), double((rgb >> 8) & 255), double((rgb >> 16) & 255)};
}

struct Track {
    std::string key, kind;
    fs::path source, audioSource;
    double start = 0, duration = 30, gain = 1;
    double x = 0, y = 0, width = 1, height = 1;
    cv::Scalar fill;
    bool audioOverride = false, eof = false, hasAhead = false;
    std::unique_ptr<VideoReader> reader;
    Image frame, ahead;
    cv::Mat fittedBgr, fittedAlpha;
    cv::Rect destination;
    int64_t fittedPts = -1;

    bool active(double time) const { return time >= start && time < start + duration; }
    bool visual() const { return kind == "image" || kind == "video" || kind == "color"; }

    void open() {
        if (kind != "image" && kind != "video") return;
        reader = std::make_unique<VideoReader>(source);
        if (!reader->next(frame)) throw std::runtime_error("无法读取轨道画面：" + source.string());
        eof = kind == "image" || reader->info().still;
    }

    // Render in increasing timeline order. Once the source ends, keep the last
    // image for the remaining clip range, without seeking past EOF.
    void sample(double seconds) {
        if (!reader || eof) return;
        const auto wanted = int64_t(std::llround(std::max(0., seconds) * 1000000));
        while (true) {
            avatar::checkCancelled();
            if (!hasAhead) {
                if (!reader->next(ahead)) { eof = true; return; }
                hasAhead = true;
            }
            if (ahead.ptsUs > wanted) return;
            frame = std::move(ahead);
            hasAhead = false;
        }
    }

    void prepare(int canvasWidth, int canvasHeight) {
        if (!fittedBgr.empty() && fittedPts == frame.ptsUs) return;
        const double boxWidth = width * canvasWidth, boxHeight = height * canvasHeight;
        const double scale = std::min(boxWidth / frame.width, boxHeight / frame.height);
        const int scaledWidth = std::max(1, int(std::lround(frame.width * scale)));
        const int scaledHeight = std::max(1, int(std::lround(frame.height * scale)));
        destination = {int(std::lround(x * canvasWidth + (boxWidth - scaledWidth) / 2)),
                       int(std::lround(y * canvasHeight + (boxHeight - scaledHeight) / 2)),
                       scaledWidth, scaledHeight};
        cv::Mat original(frame.height, frame.width, CV_8UC3, frame.bgr.data());
        if (frame.alpha.empty()) {
            cv::resize(original, fittedBgr, destination.size(), 0, 0, cv::INTER_LINEAR);
            fittedAlpha.release();
        } else {
            // Resize premultiplied channels to avoid dark seams on cutouts.
            cv::Mat premultiplied(frame.height, frame.width, CV_8UC3);
            for (size_t p = 0; p < frame.alpha.size(); ++p)
                for (size_t c = 0; c < 3; ++c)
                    premultiplied.data[p * 3 + c] = frame.premultipliedChannel(p, c);
            cv::resize(premultiplied, fittedBgr, destination.size(), 0, 0, cv::INTER_LINEAR);
            cv::Mat alpha(frame.height, frame.width, CV_8UC1, frame.alpha.data());
            cv::resize(alpha, fittedAlpha, destination.size(), 0, 0, cv::INTER_LINEAR);
        }
        fittedPts = frame.ptsUs;
    }

    void paint(Image& output, double time) {
        cv::Mat canvas(output.height, output.width, CV_8UC3, output.bgr.data());
        const cv::Rect bounds(0, 0, output.width, output.height);
        if (kind == "color") {
            const cv::Rect rect(int(std::lround(x * output.width)), int(std::lround(y * output.height)),
                                std::max(1, int(std::lround(width * output.width))),
                                std::max(1, int(std::lround(height * output.height))));
            const auto clipped = rect & bounds;
            if (!clipped.empty()) canvas(clipped).setTo(fill);
            return;
        }
        sample(time - start);
        prepare(output.width, output.height);
        const auto clipped = destination & bounds;
        if (clipped.empty()) return;
        const cv::Rect local(clipped.x - destination.x, clipped.y - destination.y,
                             clipped.width, clipped.height);
        if (fittedAlpha.empty()) {
            fittedBgr(local).copyTo(canvas(clipped));
            return;
        }
        for (int y = 0; y < clipped.height; ++y) {
            auto* target = canvas.ptr<uint8_t>(clipped.y + y) + clipped.x * 3;
            const auto* pixels = fittedBgr.ptr<uint8_t>(local.y + y) + local.x * 3;
            const auto* alpha = fittedAlpha.ptr<uint8_t>(local.y + y) + local.x;
            for (int x = 0; x < clipped.width; ++x)
                for (int c = 0; c < 3; ++c)
                    target[x * 3 + c] = uint8_t(std::min(255, int(pixels[x * 3 + c]) +
                        (int(target[x * 3 + c]) * (255 - alpha[x]) + 127) / 255));
        }
    }
};
}

void renderPageTimeline(const avatar::Json& request) {
    const fs::path out = fs::absolute(localPath(request.at("directory").get<std::string>()));
    if (fs::exists(out / "result.json") || fs::exists(out / "timeline.mp4"))
        throw std::runtime_error("时间轴输出已存在");
    int width = request.value("width", 1280), height = request.value("height", 720);
    const int fps = request.value("fps", 25);
    if (width < 16 || height < 16 || width > 4096 || height > 4096 || fps < 1 || fps > 60)
        throw std::runtime_error("无效的视频尺寸或帧率");
    width = (width + 1) / 2 * 2; height = (height + 1) / 2 * 2;
    const auto& input = request.at("tracks");
    if (!input.is_array() || input.size() > 64) throw std::runtime_error("无效的轨道列表");
    std::vector<Track> tracks;
    double end = 0;
    for (const auto& item : input) {
        if (!item.value("enabled", true)) continue;
        Track track;
        track.key = item.at("key").get<std::string>();
        if (track.key != "image" && track.key != "speech" && track.key != "sfx" && track.key != "background")
            throw std::runtime_error("不支持的轨道：" + track.key);
        track.kind = item.value("kind", track.key == "sfx" ? "audio" : "image");
        if (track.kind != "image" && track.kind != "video" && track.kind != "audio" && track.kind != "color")
            throw std::runtime_error("不支持的轨道素材类型");
        track.source = localPath(item.value("source", std::string()));
        if (track.kind != "color" && !fs::is_regular_file(track.source))
            throw std::runtime_error("轨道素材不存在：" + track.source.string());
        track.start = number(item, "start", 0);
        track.duration = number(item, "duration", 30);
        if (track.start < 0 || track.duration <= 0 || track.start + track.duration > 600.001)
            throw std::runtime_error("轨道时间范围需要在十分钟内");
        track.gain = std::clamp(number(item, "gain", 1), 0., 4.);
        track.x = std::clamp(number(item, "x", 0), -4., 4.);
        track.y = std::clamp(number(item, "y", 0), -4., 4.);
        track.width = std::clamp(number(item, "width", 1), .001, 4.);
        track.height = std::clamp(number(item, "height", 1), .001, 4.);
        if (track.kind == "color") track.fill = color(item.value("color", std::string("#181b1d")));
        track.audioOverride = item.contains("audioSource");
        if (track.audioOverride) track.audioSource = localPath(item.at("audioSource").get<std::string>());
        end = std::max(end, track.start + track.duration);
        tracks.push_back(std::move(track));
    }
    const double requestedDuration = number(request, "duration", end);
    if (requestedDuration <= 0 || requestedDuration > 600)
        throw std::runtime_error("视频时长需要大于零且不超过十分钟");
    const int frames = int(std::ceil(requestedDuration * fps));
    const double duration = double(frames) / fps;
    std::stable_sort(tracks.begin(), tracks.end(), [](const Track& a, const Track& b) {
        const auto order = [](const std::string& key) { return key == "background" ? 0 : key == "image" ? 1 : 2; };
        return order(a.key) < order(b.key);
    });
    std::vector<float> mixed(size_t(std::llround(duration * 16000)), 0.f);
    bool hasAudio = false;
    for (auto& track : tracks) {
        avatar::checkCancelled();
        track.open();
        if (track.key != "speech" && track.key != "sfx") continue;
        const fs::path path = track.audioOverride ? track.audioSource :
            (track.kind == "video" || track.kind == "audio" ? track.source : fs::path());
        if (path.empty() || track.gain == 0) continue;
        if (!fs::is_regular_file(path)) throw std::runtime_error("轨道音频不存在：" + path.string());
        const auto samples = decodeAudio16k(path);
        const auto offset = size_t(std::llround(track.start * 16000));
        if (offset >= mixed.size()) continue;
        const auto count = std::min({samples.size(), mixed.size() - offset,
                                    size_t(std::llround(track.duration * 16000))});
        for (size_t n = 0; n < count; ++n) mixed[offset + n] += samples[n] * track.gain;
        hasAudio = hasAudio || count > 0;
    }
    for (auto& sample : mixed) sample = std::clamp(sample, -1.f, 1.f);
    fs::create_directories(out);
    const auto output = out / "timeline.mp4";
    const auto base = color(request.value("color", std::string("#101214")));
    VideoWriter writer(output, mixed, width, height, hasAudio, true, fps, 1);
    Image frame; frame.width = width; frame.height = height; frame.bgr.resize(size_t(width) * height * 3);
    cv::Mat canvas(height, width, CV_8UC3, frame.bgr.data());
    for (int n = 0; n < frames; ++n) {
        avatar::checkCancelled();
        canvas.setTo(base);
        const double time = double(n) / fps;
        for (auto& track : tracks)
            if (track.visual() && track.active(time)) track.paint(frame, time);
        writer.write(frame);
        if (n % fps == 0) avatar::progress("timeline-video", n, frames);
    }
    writer.finish();
    avatar::progress("timeline-video", frames, frames);
    avatar::writeJson(out / "result.json", {
        {"path", output.string()}, {"video", output.string()}, {"width", width}, {"height", height},
        {"duration", duration}, {"durationSeconds", duration}, {"frames", frames}, {"route", "timeline"}
    });
    std::cout << avatar::Json{{"event", "complete"}, {"result", (out / "result.json").string()}}.dump() << std::endl;
}
