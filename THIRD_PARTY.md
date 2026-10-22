# Third-party components

Original project code is licensed under the MIT License in the repository's
`LICENSE` file (Copyright 2026 Zhaolei Wang). This grant does not relicense
third-party code, model weights, or assets; their respective terms continue
to apply. The application bundle includes the project's MIT license at
`Contents/Resources/licenses/Xinshen-MIT.txt`.

## Qt source and user rights

The macOS application dynamically links Qt 6.10.2. The Store bundle includes
Qt license texts and upstream third-party attribution under
`Contents/Resources/licenses/qt/`. The source manifest there identifies the
official source archives and their SHA-256 values.

Corresponding Qt source archives are available as
`XAvatar-Qt-6.10.2-sources.zip` in the project's
[GitHub release](https://github.com/425776024/MOSS-Image-Qt/releases/tag/macos-0.1.0-preview.1).
Qt components retain their own licenses. Nothing in the application's model
terms restricts rights to modify, relink, or debug modifications to Qt that
its applicable open-source licenses grant. Application source is available
in this repository; build instructions are in `docs/build.md`.

## Sound effects

Powered by Stability AI. Stable Audio 3 Small SFX is distributed under the
Stability AI Community License; T5Gemma uses the Gemma Terms of Use, including
its Section 3.2 use restrictions. These models are not covered by the inference
engine's MIT license. Full terms and attribution are bundled in
`Contents/Resources/licenses/audio/sfx/` and in the corresponding Apple model
resource packs. See https://stability.ai/use-policy and
https://ai.google.dev/gemma/prohibited_use_policy for usage restrictions.

The published GGUF files are format conversions by thepatch, redistributed
without further changes; exact revisions and hashes are in `config/models.json`.
Stable Audio is trained on English descriptions. The app currently passes the
sound-effect prompt unchanged and does not automatically translate Chinese.

Public model acquisition is implemented; this is not a notarized public release.

The home shell, project list, avatar/voice library layouts, settings and contact
page are ported from the user's authorized Xinshen Flutter project
(`Xinshen/lib/studio_home_ui.dart`,
`studio_works.dart`, `studio_avatars.dart`, `voice_library.dart`,
`studio_settings.dart`, and `studio_contact.dart`). The contact QR image is
copied from that project's `assets/builtin/images/wechat_contact_qr.png`.
This reuse does not import Xinshen's work editor, models or project library.
Moss uses a single-window conversation workspace with its existing native
inference paths. Voice import uses a user-entered transcript.

| Component | Source | Use |
|---|---|---|
| Qt 6.10.2 | https://www.qt.io/ | Dynamically linked desktop UI and media playback |
| stable-diffusion.cpp | https://github.com/leejet/stable-diffusion.cpp | MIT, commit b13a48219d5cdf408edcb93da6cd69b612ed03d3 |
| ggml | stable-diffusion.cpp pinned submodule | MIT, CPU/Metal inference |
| MOSS-Image SFT | https://huggingface.co/OpenMOSS-Team/MOSS-Image-SFT | Model family, Apache-2.0; Q4_K / Q8_0 conversions from https://huggingface.co/leejet/MOSS-Image-SFT-GGUF |
| Qwen3.5 9B | https://huggingface.co/unsloth/Qwen3.5-9B-GGUF | Q4 text encoder; separate from prompt enhancement |
| Ditto | https://github.com/antgroup/ditto-talkinghead | Native integration from the user's Xinshen project |
| MediaPipe | https://github.com/google-ai-edge/mediapipe | Native C API for Ditto source eye references; Apache-2.0 notice in `vendor/xinshen-ditto/LICENSE.mediapipe` |
| ONNX Runtime | https://github.com/microsoft/onnxruntime | MIT, Ditto networks |
| OpenCV | https://github.com/opencv/opencv | Image preprocessing |
| YuNet 2023 | https://github.com/opencv/opencv_zoo/tree/47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_detection_yunet | MIT model; replaces the public InsightFace detector/106-point stage; notice in `vendor/xinshen-ditto/LICENSE.YuNet` |
| LivePortrait landmarks | https://github.com/KlingAIResearch/LivePortrait | Retained 203-point model; upstream notice including its exceptions in `vendor/xinshen-ditto/LICENSE.LivePortrait` |
| FFmpeg | https://ffmpeg.org/ | Native decoding, resampling, VideoToolbox encoding |
| OpenSSL | https://www.openssl.org/ | Hash verification |

`vendor/xinshen-ditto/PROVENANCE.md` records the local source reuse.
`vendor/videocut-voice/PROVENANCE.md` records the Ming-3.0 Q8 voice backend,
builtin voice references, and VideoCut's MIT `sa3.cpp` SFX engine port.
Audio notices are retained in `vendor/videocut-voice/licenses` and
`assets/voices`; model permissions are separate from engine permissions.

The shared QML controls (`AppButton`, `IconButton`, `AppSlider`, `AppCheckBox`,
`AppSelect`, `AppInputSurface`, `AppMenuItem`, `CardSurface`) are adapted from the
user-authorized `VideoCut-Qt/app/qml/controls/` sources.
`VideoCutTheme.qml` adapts that project's `app/qml/design/Theme.qml` palette,
control metrics and `AppearanceController.cpp` theme presets. The standalone
adapters use Moss's own appearance preferences and SVG icons, and do not link
the VideoCut SDK or its controller graph. The conversation cards, inline media
players and two-lane packing UI compose these controls in Moss.

`ConversationDocument` retains per-work conversation history and owned imported
assets. The two packing lanes dispatch to
`vendor/xinshen-ditto/timeline_render.*`, using the existing OpenCV and
FFmpeg/VideoToolbox adapter. This introduces no additional inference model.

`Contents/Resources/native-dependencies.json` records the actual copied native
libraries. This file is an inventory, not a substitute for the applicable
component licenses, source offers, model permissions or release review.

For public distribution, retain the exact dependency sources, required notices
and Qt/FFmpeg replacement/relink materials. Do not substitute the development
Homebrew GPL FFmpeg for the isolated distribution FFmpeg.

## Public model downloads

`config/models.json` pins every downloaded file by repository revision, size and
SHA-256. The native Ditto public path uses original official weights and Voxta's
opset-20 warp format conversion (weights unchanged). The private finetuned model
and local optimized model exports are excluded. See `docs/ditto-default-model.md`.
Ming-3.0 Q8 is the default and sole speech engine. Stable Audio's separate
community license and T5Gemma's terms still apply.

Public model repositories: https://huggingface.co/digital-avatar/ditto-talkinghead,
https://huggingface.co/voxta/ditto-talkinghead-onnx,
https://huggingface.co/audio-cpp/Ming-3.0-TTS-0.5B-GGUF,
https://huggingface.co/thepatch/stable-audio-3-small-sfx-GGUF,
https://huggingface.co/thepatch/t5gemma-b-b-ul2-GGUF.
Downloading through hf-mirror.com changes the endpoint, not the pinned identities
or applicable upstream terms. In particular, bundled upstream auxiliary models
may have terms different from Ditto's source license.

## Ming-3.0 Q8 speech

Both the public and App Store editions use the same Ming-only CPU C ABI from
[audio.cpp](https://github.com/0xShug0/audio.cpp), pinned at
`db433f3e6dfe0159828532958698d56529bb3c5c` (Apache-2.0). The native build retains
notices for ggml, SentencePiece, utf8proc, cJSON, libyaml and the tokenizer.
The build overlay replaces debugger subprocess backtraces with native stack symbols.
No eSpeak, model-manager networking or Python inference is included.

[Ming Q8 weights](https://huggingface.co/audio-cpp/Ming-3.0-TTS-0.5B-GGUF) use
revision `2eb8d37c7f10672e69c8aa33091baa04c461b66c`, SHA-256
`fcf2aa25e8e9cf2065a936e506581735c64d6255fb7869f09fbb61a2dde09aa1`.
The upstream inclusionAI model is MIT; its full license is retained under
`vendor/videocut-voice/licenses/ming/`. Voice recording and preset rights remain
separate from model rights. The retired Hojo backend, headers and model notices have been removed.
