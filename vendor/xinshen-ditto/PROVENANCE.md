# Ditto native module

Copied from the user's Xinshen project (`native/`) on 2026-10-20.
The original source checkout is not modified by this project.

`liveportrait/ditto.cpp` implements antgroup/ditto-talkinghead
commit `c3e47eee2e626500017a0556b470d6d4182f85e8`; upstream is Apache-2.0.
The remaining copied files are Xinshen's native media/runtime/Core ML adapters.
`worker.cpp` and this CMake integration are new to Moss Studio.
The H.264 writer uses Apple's `h264_videotoolbox` in place of the source
project's `libx264`, with native hardware encoding and an explicit bitrate.

The 2026-10-20 default-model refresh also imports `liveportrait/ditto.cpp` and
`liveportrait/ditto_source_condition.h` from Xinshen's focused-05000 integration.
These add source-neutral-v1 conditioning and source-relative mouth mapping.
The source eye reference uses the MediaPipe C API, linked and bundled natively.

Models are a separate asset pack. The public default uses the original official
Ditto ONNX weights, with a public opset-20 warp conversion. Exact repository
revisions and hashes are in `config/models.json`. Private finetuned weights,
condition caches and Core ML exports are not included or automatically loaded.

On 2026-10-21 the four `ditto*` source/header files for source conditioning,
mouth references, and silence calibration were synchronized from Xinshen's
`silent-mouth-calibration-v2` implementation. The reviewed policy catalog is
stored in `mouth-reference.json`. The runtime uses a Moss-specific cache directory;
the algorithm and rejection bounds retain their source provenance. No private
denoiser is included in the public source or catalog.
This refresh also includes the direct RGB8-to-CHW conversion that avoids the
OpenCV/MediaPipe tensor-allocation crash observed in the previous integration.
Moss adds a persisted per-page auto/source control and forwards it to the worker.

On 2026-10-24 Moss added `emotion-linked-mouth-calibration-v3`. Expression
references use the selected emotion's three-seed silence estimate; non-neutral
references require a separately source-validated neutral estimate and retain the
original temporal/seed stability bounds. The neutral source-offset bound is
unchanged, but it is no longer applied to the expression's resting displacement.
Neutral reviewed catalog profiles are not used for other emotions. This is a
local adaptation, not a claim of upstream emotion-quality validation.

Build dependencies: ONNX Runtime, OpenCV, OpenSSL, FFmpeg and macOS Core ML.
The distributable must carry their applicable notices and actual linked libraries.
This integration does not establish redistribution clearance for model assets.

The 2026-10-21 YuNet migration adapts the user's Xinshen
`native/liveportrait/inference.cpp` five-point alignment contract, `yunet-203-v1`.
OpenCV Zoo revision `47534e27c9851bb1128ccc0102f1145e27f23f98` explicitly licenses
all files in its face_detection_yunet directory under MIT. The official OpenCV
Hugging Face mirror at `d4938dfc9d4ec5d098bfa33e98b3f3345a236586` supplies the
identical 232589-byte model, SHA256
`8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4`.
`LICENSE.YuNet` preserves its notice. `LICENSE.LivePortrait` preserves the
203-point model's upstream notice, including the original InsightFace exception.
The 203-point model matches Xinshen's LivePortrait landmark.onnx byte for byte
(SHA256 `31d22a5041326c31f19b78886939a634a5aedcaa5ab8b9b951a1167595d147db`).
The public catalog no longer downloads detector.onnx or landmark106.onnx.
Old manifest profiles retain their old runtime solely for compatibility and A/B
validation; new profiles are selected explicitly by manifest, never file presence.
This change does not clear unrelated model, framework, or App Store obligations.

The public split ONNX renderer adapts the same full warp/decoder architecture.
Its weights come from the official Ditto and Voxta ONNX conversion repositories.
The public default uses the official conditioning configuration; private policy
artifacts do not become public model defaults.
