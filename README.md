# 新身数字人 / XAvatar

**简体中文** · [English](README-en.md)

面向 Apple Silicon Mac 的本地 AI 创作 App，用对话完成图像、声音与数字人视频创作。

[**下载 macOS 安装包（DMG）**](https://github.com/425776024/MOSS-Image-Qt/releases/download/macos-0.1.0-preview.1/Moss-Studio-0.1.0-macOS-arm64-notarized.dmg) · [App ZIP](https://github.com/425776024/MOSS-Image-Qt/releases/download/macos-0.1.0-preview.1/Moss-Studio-0.1.0-macOS-arm64-notarized.app.zip) · [版本说明与校验文件](https://github.com/425776024/MOSS-Image-Qt/releases/tag/macos-0.1.0-preview.1)

0.1.0 预览版，支持 **Apple Silicon（M1 及后续芯片）、macOS 15+**。App 与 DMG 已完成 Developer ID 签名及 Apple 公证。运行无需 Python，模型按需下载。

## 功能预览

对话生成图像，涂抹局部编辑，拖动对象控制构图；设计或克隆音色，驱动人像说话，再组合素材导出 MP4。

| 图像创作 | 局部编辑 | 布局构图 |
| --- | --- | --- |
| <a href="assets/screenshots/zh-Hans/studio.png"><img src="assets/screenshots/zh-Hans/studio.png" alt="图像创作" width="270" /></a> | <a href="assets/screenshots/zh-Hans/image-edit.png"><img src="assets/screenshots/zh-Hans/image-edit.png" alt="局部编辑" width="270" /></a> | <a href="assets/screenshots/zh-Hans/layout.png"><img src="assets/screenshots/zh-Hans/layout.png" alt="布局构图" width="270" /></a> |
| **数字人创作** | **语音设计** | **视频打包** |
| <a href="assets/screenshots/zh-Hans/avatar.png"><img src="assets/screenshots/zh-Hans/avatar.png" alt="数字人创作" width="270" /></a> | <a href="assets/screenshots/zh-Hans/voice-design.png"><img src="assets/screenshots/zh-Hans/voice-design.png" alt="语音设计" width="270" /></a> | <a href="assets/screenshots/zh-Hans/video-pack.png"><img src="assets/screenshots/zh-Hans/video-pack.png" alt="视频打包" width="270" /></a> |

截图展示当前开发界面，已发布预览安装包可能有所不同。点击截图查看原图。[全部中文截图](assets/screenshots/zh-Hans) · [English screenshots](assets/screenshots/en)

## 图像编辑效果

保留原有字形风格与背景，修改文字或选定区域。

<table>
<tr><th width="50%">中英文文字替换</th><th width="50%">人像局部改色</th></tr>
<tr>
<td align="center"><a href="assets/showcase/text-edit-before-after.png"><img src="assets/showcase/text-edit-before-after.png" alt="中英文文字替换" width="240" /></a></td>
<td align="center"><a href="assets/showcase/portrait-edit-before-after.png"><img src="assets/showcase/portrait-edit-before-after.png" alt="人像局部改色" width="420" /></a></td>
</tr>
</table>

## 数字人效果

优化版采用 **5000 步微调、静音口型校准与起音过渡**，用于改善停顿时的嘴形与开口衔接。

**默认下载的社区／GitHub 版使用官方原版模型，对应视频左侧官方效果；最右侧优化效果需要从 App Store 下载的 App 版本才能使用。**

以下视频含声音，点击播放。

**多种形象 · 84 秒**

https://github.com/user-attachments/assets/c8c14e41-5ce8-4330-bbd6-d52f6e8f06cb

**视频人物 · 20 秒**

https://github.com/user-attachments/assets/cefbd02c-c2e2-4b03-85a7-4597a5fbbea8

[下载原始视频一](assets/showcase/avatar-demo-01.mp4) · [下载原始视频二](assets/showcase/avatar-demo-02.mp4)

## 资源与导入

按图像、数字人、语音和音效管理资源，支持按需下载、校验与清理。形象导入时直接预览图片。

| 创作资源 | 按需下载 | 形象导入预览 |
| --- | --- | --- |
| <a href="assets/screenshots/zh-Hans/models.png"><img src="assets/screenshots/zh-Hans/models.png" alt="创作资源" width="270" /></a> | <a href="assets/screenshots/zh-Hans/download.png"><img src="assets/screenshots/zh-Hans/download.png" alt="按需下载" width="270" /></a> | <a href="assets/screenshots/zh-Hans/avatar-import.png"><img src="assets/screenshots/zh-Hans/avatar-import.png" alt="形象导入预览" width="270" /></a> |

## 使用的模型

| 用途 | 模型 | 功能 / 展示 |
| --- | --- | --- |
| 图像生成与编辑 | [MOSS-Image SFT GGUF](https://huggingface.co/leejet/MOSS-Image-SFT-GGUF) · Q4_K / Q8_0 | [对话创作](assets/screenshots/zh-Hans/studio.png)、[局部编辑](assets/screenshots/zh-Hans/image-edit.png)、[布局构图](assets/screenshots/zh-Hans/layout.png) |
| 图像文本编码 | [Qwen3.5-9B GGUF](https://huggingface.co/unsloth/Qwen3.5-9B-GGUF) · Q4_K_M | 理解图像描述与编辑指令 |
| 图像解码与分词 | [FLUX.2 VAE](https://huggingface.co/Comfy-Org/MOSS-Image)、[MOSS-Image tokenizer](https://huggingface.co/OpenMOSS-Team/MOSS-Image-Base) | 图像模型共用组件 |
| 数字人 | [ditto-talkinghead](https://github.com/antgroup/ditto-talkinghead) | [音频驱动人像](assets/screenshots/zh-Hans/avatar.png)、静音口型校准与起音过渡优化 |
| 人脸检测与渲染 | [YuNet](https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet)、[LivePortrait](https://github.com/KwaiVGI/LivePortrait) | 人脸定位、动作与画面合成 |
| 语音与音色 | [Ming-3.0 TTS 0.5B](https://github.com/inclusionAI/Ming-3.0-tts) · [GGUF Q8_0](https://huggingface.co/audio-cpp/Ming-3.0-TTS-0.5B-GGUF) | 文字配音、音色克隆、[语音设计](assets/screenshots/zh-Hans/voice-design.png)、情绪与方言控制 |
| 音效 | [Stable Audio 3 Small SFX](https://huggingface.co/thepatch/stable-audio-3-small-sfx-GGUF) · F16，配套 T5Gemma | 文字生成环境声与音效 |


基于 Qt / QML / C++。固定模型版本、文件摘要与来源见 [模型清单](config/models.json)。

[设计规范](DESIGN.md) · [构建与运行](docs/build.md) · [macOS 打包与公证](docs/macos-release.md) · [模型管理](docs/model-management.md) · [Ditto 模型说明](docs/ditto-default-model.md) · [第三方来源](THIRD_PARTY.md)

Windows 10/11 x64 的自动构建与显卡测试正在接入，尚未发布可下载版本；进展与测试方式见 [Windows 构建与验收](docs/windows-release.md)。

## 许可证

原创代码采用 [MIT 许可证](LICENSE)。第三方代码、模型权重及素材保留各自许可证，来源与许可说明见 [THIRD_PARTY.md](THIRD_PARTY.md)。
