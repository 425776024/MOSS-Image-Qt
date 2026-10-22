# XAvatar / 新身数字人

[简体中文](README.md) · **English**

A local AI creation app for Apple Silicon Macs. Create images, speech, and talking-avatar videos through conversation.

[**Download for macOS (DMG)**](https://github.com/425776024/MOSS-Image-Qt/releases/download/macos-0.1.0-preview.1/Moss-Studio-0.1.0-macOS-arm64-notarized.dmg) · [App ZIP](https://github.com/425776024/MOSS-Image-Qt/releases/download/macos-0.1.0-preview.1/Moss-Studio-0.1.0-macOS-arm64-notarized.app.zip) · [Release notes and checksums](https://github.com/425776024/MOSS-Image-Qt/releases/tag/macos-0.1.0-preview.1)

Version 0.1.0 preview for **Apple Silicon (M1 or later) and macOS 15+**. The app and DMG are Developer ID signed and Apple notarized. No Python installation is required; models download on demand.

## Feature preview

Generate images through conversation, paint over areas to edit, and drag objects to guide composition. Design or clone a voice, animate a portrait, then combine media and export an MP4.

| Image generation | Local editing | Layout composition |
| --- | --- | --- |
| <a href="assets/screenshots/en/studio.png"><img src="assets/screenshots/en/studio.png" alt="Image generation" width="270" /></a> | <a href="assets/screenshots/en/image-edit.png"><img src="assets/screenshots/en/image-edit.png" alt="Local editing" width="270" /></a> | <a href="assets/screenshots/en/layout.png"><img src="assets/screenshots/en/layout.png" alt="Layout composition" width="270" /></a> |
| **Talking avatars** | **Voice design** | **Video composition** |
| <a href="assets/screenshots/en/avatar.png"><img src="assets/screenshots/en/avatar.png" alt="Talking avatars" width="270" /></a> | <a href="assets/screenshots/en/voice-design.png"><img src="assets/screenshots/en/voice-design.png" alt="Voice design" width="270" /></a> | <a href="assets/screenshots/en/video-pack.png"><img src="assets/screenshots/en/video-pack.png" alt="Video composition" width="270" /></a> |

Screenshots show the current development UI; the published preview build may differ. Click any screenshot to view it at full size. [All English screenshots](assets/screenshots/en) · [中文截图](assets/screenshots/zh-Hans)

## Image editing examples

Replace text or edit selected regions while retaining the surrounding image and lettering style.

<table>
<tr><th width="50%">Text replacement</th><th width="50%">Local hair-color editing</th></tr>
<tr>
<td align="center"><a href="assets/showcase/text-edit-before-after.png"><img src="assets/showcase/text-edit-before-after.png" alt="Text replacement" width="240" /></a></td>
<td align="center"><a href="assets/showcase/portrait-edit-before-after.png"><img src="assets/showcase/portrait-edit-before-after.png" alt="Local hair-color editing" width="420" /></a></td>
</tr>
</table>

## Talking-avatar demos

The optimized version combines **5,000-step fine-tuning, silent-mouth calibration, and speech-onset transitions** to improve resting mouth shapes and the transition into speech.

**The default community / GitHub download uses the original official model, corresponding to the official result on the left. The optimized result on the far right requires the app downloaded from the App Store.**

Both videos include audio. Click play to watch.

**Multiple avatars · 84 seconds**

https://github.com/user-attachments/assets/c8c14e41-5ce8-4330-bbd6-d52f6e8f06cb

**Video portrait · 20 seconds**

https://github.com/user-attachments/assets/cefbd02c-c2e2-4b03-85a7-4597a5fbbea8

[Download original video 1](assets/showcase/avatar-demo-01.mp4) · [Download original video 2](assets/showcase/avatar-demo-02.mp4)

## Resources and imports

Manage resources by task: images, avatars, speech, and sound effects. Download, verify, or remove resources as needed, and preview portraits before importing them.

| Creative resources | On-demand downloads | Portrait import preview |
| --- | --- | --- |
| <a href="assets/screenshots/en/models.png"><img src="assets/screenshots/en/models.png" alt="Creative resources" width="270" /></a> | <a href="assets/screenshots/en/download.png"><img src="assets/screenshots/en/download.png" alt="On-demand downloads" width="270" /></a> | <a href="assets/screenshots/en/avatar-import.png"><img src="assets/screenshots/en/avatar-import.png" alt="Portrait import preview" width="270" /></a> |

## Models

| Purpose | Model | Features / preview |
| --- | --- | --- |
| Image generation and editing | [MOSS-Image SFT GGUF](https://huggingface.co/leejet/MOSS-Image-SFT-GGUF) · Q4_K / Q8_0 | [Generation](assets/screenshots/en/studio.png), [local editing](assets/screenshots/en/image-edit.png), [layout](assets/screenshots/en/layout.png) |
| Image text encoder | [Qwen3.5-9B GGUF](https://huggingface.co/unsloth/Qwen3.5-9B-GGUF) · Q4_K_M | Image descriptions and editing instructions |
| Image decoder and tokenizer | [FLUX.2 VAE](https://huggingface.co/Comfy-Org/MOSS-Image), [MOSS-Image tokenizer](https://huggingface.co/OpenMOSS-Team/MOSS-Image-Base) | Shared image components |
| Talking avatars | [ditto-talkinghead](https://github.com/antgroup/ditto-talkinghead) | [Audio-driven portraits](assets/screenshots/en/avatar.png), silent-mouth calibration, and speech-onset optimization |
| Face detection and rendering | [YuNet](https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet), [LivePortrait](https://github.com/KwaiVGI/LivePortrait) | Face alignment, motion, and rendering |
| Speech and voices | [Ming-3.0 TTS 0.5B](https://github.com/inclusionAI/Ming-3.0-tts) · [GGUF Q8_0](https://huggingface.co/audio-cpp/Ming-3.0-TTS-0.5B-GGUF) | Text-to-speech, voice cloning, [voice design](assets/screenshots/en/voice-design.png), emotion, and dialect controls |
| Sound effects | [Stable Audio 3 Small SFX](https://huggingface.co/thepatch/stable-audio-3-small-sfx-GGUF) · F16 with T5Gemma | Environmental sounds and effects from text |

Built with Qt / QML / C++. Pinned model revisions, hashes, and sources are listed in the [model catalog](config/models.json).

[Design guidelines](DESIGN.md) · [Build and run](docs/build.md) · [macOS packaging and notarization](docs/macos-release.md) · [Model management](docs/model-management.md) · [Ditto model notes](docs/ditto-default-model.md) · [Third-party sources](THIRD_PARTY.md)

## License

Original project code is licensed under [MIT](LICENSE). Third-party code, model weights, and media retain their own licenses. See [THIRD_PARTY.md](THIRD_PARTY.md) for attribution and terms.
