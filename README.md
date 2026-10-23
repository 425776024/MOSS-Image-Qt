# 新身数字人 · XAvatar

**简体中文** · [English](README-en.md)

本地 AI 创作工具，用对话完成图像、声音、数字人视频与成片制作。

## 功能清单

| 功能 | 支持内容 |
| --- | --- |
| 图像创作 | 文字生成图像、整图编辑、FlowEdit 保留原貌编辑、涂抹或框选局部重绘 |
| 构图设计 | 拖动对象安排画面，内置 21 套可编辑模板 |
| 数字人 | 图片或文字创建形象、导入人像、语音或录音驱动视频；内置 17 张形象 |
| 声音 | 文字配音、声音设计、录音导入与音色克隆、情绪和方言；内置音色及 20 个声音设计模板 |
| 音效 | 根据文字生成环境声与音效，试听并保存 |
| 视频制作 | 组合图像、视频和声音，调整轨道与画面，导出 MP4 |
| 作品管理 | 多作品对话、搜索、重命名、复制、保存、移除与恢复，素材预览与导出 |
| 资源管理 | 首次使用引导、多选下载、断点续传、自动选择下载源、校验与清理；图像默认 Q4 |
| Agent / MCP | 本机或局域网连接 Agent，管理作品、生成内容、上传和读取媒体、查询任务与资源 |
| 桌面体验 | 中英文、深浅主题、可调整的三面板工作区、诊断日志导出 |

## 界面预览

| 作品与图像创作 | 声音设计与模板 |
| --- | --- |
| [<img src="assets/screenshots/zh-Hans/studio.png" alt="作品与图像创作" width="420" />](assets/screenshots/zh-Hans/studio.png) | [<img src="assets/screenshots/zh-Hans/voice-design.png" alt="声音设计与模板" width="420" />](assets/screenshots/zh-Hans/voice-design.png) |

## 数字人视频

视频含声音。左侧为官方原版，最右侧优化效果需 App Store 版本；GitHub 版使用官方原版模型。

**多种形象 · 84 秒**

https://github.com/user-attachments/assets/c8c14e41-5ce8-4330-bbd6-d52f6e8f06cb

**视频人物 · 20 秒**

https://github.com/user-attachments/assets/cefbd02c-c2e2-4b03-85a7-4597a5fbbea8

[下载原始视频一](assets/showcase/avatar-demo-01.mp4) · [下载原始视频二](assets/showcase/avatar-demo-02.mp4)

## 下载

[**前往 Releases 下载安装包、查看更新与校验和**](https://github.com/425776024/MOSS-Image-Qt/releases)

| 平台 | 系统要求 | 安装方式 |
| --- | --- | --- |
| macOS | Apple Silicon（M1 及以后），macOS 15+ | 下载标记为“已公证”的 DMG，拖入 Applications |
| Windows | Windows 10 1903+ / 11，x64 | 运行 `XAvatar-…-Setup.exe`，从桌面或开始菜单启动 |

无需安装 Python。模型按需下载，安装包不包含权重；GitHub 版使用官方原版数字人模型。Windows 的 GPU 兼容性与已验证范围见对应发布说明。

## 快速开始

1. 首次启动时勾选需要的创作资源，也可稍后在「资源」中下载。
2. 在「作品」输入需求，通过输入框下拉菜单选择图像、数字人、配音、音效或视频制作。
3. 在「数字人」「声音」「构图」选择内置资源或创建自己的资源，再用于作品。

声音设计页左侧提供模板，点击即可填写名称、声音描述与试听文案。MCP 需要在应用中手动开启，再复制连接说明给 Agent。

## 文档与示例

[构建与运行](docs/build.md) · [Mac 打包与公证](docs/macos-release.md) · [Windows 构建与验收](docs/windows-release.md) · [MCP 接入](docs/mcp.md) · [诊断日志](docs/diagnostics.md)

[模型管理](docs/model-management.md) · [数字人模型说明](docs/ditto-default-model.md) · [模型清单](config/models.json) · [图像编辑说明与示例](vendor/moss-flowedit/README.md) · [界面截图](assets/screenshots/zh-Hans)

图像编辑会重新生成像素，不能保证人物身份或未编辑区域完全不变。第三方代码、模型与素材遵循各自许可，详见 [第三方来源](THIRD_PARTY.md)；原创代码采用 [MIT 许可证](LICENSE)。
