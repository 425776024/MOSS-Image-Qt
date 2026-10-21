# Third-party components

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
| FFmpeg | https://ffmpeg.org/ | Native decoding, resampling, VideoToolbox encoding |
| OpenSSL | https://www.openssl.org/ | Hash verification |

`vendor/xinshen-ditto/PROVENANCE.md` records the local source reuse.
`vendor/videocut-voice/PROVENANCE.md` records the VideoCut-Qt voice backend,
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
Hojo uses the native-backend-compatible revision 5973d74; Stable Audio's separate
community license and T5Gemma's terms still apply. Chinese normalization rules are
from the pinned M5Stack/MeloTTS-Chinese-ax630c public mirror of WeText FSTs.

Public model repositories: https://huggingface.co/digital-avatar/ditto-talkinghead,
https://huggingface.co/voxta/ditto-talkinghead-onnx,
https://huggingface.co/HojoAI/Hojo-TTS-Light,
https://huggingface.co/M5Stack/MeloTTS-Chinese-ax630c,
https://huggingface.co/thepatch/stable-audio-3-small-sfx-GGUF,
https://huggingface.co/thepatch/t5gemma-b-b-ul2-GGUF.
Downloading through hf-mirror.com changes the endpoint, not the pinned identities
or applicable upstream terms. In particular, bundled upstream auxiliary models
may have terms different from Ditto's source license.
