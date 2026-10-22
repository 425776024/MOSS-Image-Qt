# Windows 编译与验收

Windows 版目标为 **Windows 10 1903+ / Windows 11、x64**。Qt 6.10 支持 Windows 10 1809+；本包额外使用 Windows 10 1903 起的 UTF-8 进程清单处理中文路径。当前是待实机验收的预览构建，尚无正式 Windows 下载版本。

## 用户使用

CI 成功后，从对应 Actions 运行下载 `Windows-x64-preview`，解开其中的 ZIP，再完整解压 `Moss-Studio-0.1.0-Windows-x64-preview.zip`，运行 `MossStudio.exe`。保留整个目录，不能只复制 EXE。版本标签构建还会把 ZIP 和 SHA-256 文件上传至草稿 Release；发布草稿后才有无需登录的公开下载入口。

不需要 Python、Qt SDK 或 CUDA Toolkit。模型在 App 中按需下载；显卡需安装提供 Vulkan / DirectX 12 的厂商驱动。Windows N 版可能需要系统 Media Feature Pack。

| 用途 | Windows 后端 | 验收状态 |
| --- | --- | --- |
| MOSS 图像生成、局部编辑、布局 | stable-diffusion.cpp Vulkan | 待实机生成 |
| Ming-3.0 语音与音色设计 | audio.cpp Vulkan；可显式切换 CPU 诊断 | 待实机生成和性能对比 |
| 音效 | sa3.cpp Vulkan | 待实机生成 |
| Ditto 数字人 | ONNX Runtime DirectML；部分算子及预处理仍用 CPU | 待实机生成 |
| MP4 导出 | FFmpeg / Windows Media Foundation H.264 | 待实机导出与播放 |

不把编译开关、启动成功或日志中发现显卡等同于完整模型运行成功。GPU 不受支持、驱动过旧或显存不足时可能报错，不承诺所有 Windows 10 电脑和所有显卡可运行。

## RTX 3090 24GB 首轮测试

1. 完整解压测试包，双击 `test_windows.cmd`。桌面会生成 `Moss-Test-日期时间.zip`，包含系统、显卡驱动、离屏启动结果和日志；这一轮不下载模型。
2. 启动 App，下载图像 Q4、语音和数字人资源，确认使用的模型目录。
3. 关闭 App，在 PowerShell 中运行：

```powershell
.\test_windows.ps1 -Inference -ModelRoot 'D:\MossModels'
```

此步骤生成一段 Ming 设计语音与一张 512×512 图像。报告包含输出和任务 `engine.log`，需要听音频、看图像，并核实 Vulkan 实际执行。另运行 `-Inference -CpuVoice -ModelRoot 'D:\MossModels'` 可对比语音 CPU；两次测试固定默认输入，但图像耗时不能用于语音对比。

4. 在 App 中用同一张人像和同一段语音生成约 5 秒数字人视频，检查 DirectML 日志、口型、画面和声音，再导出并播放 MP4。此项需要用户提供选定素材，自动检测脚本尚未代做。
5. 检查真实桌面上的浅色、深色、中英文、最小窗口、Tab、Enter、Esc、文件选择及中文目录。离屏启动不能替代这些操作的验收。

初轮无须配置 SSH、开放端口或提供登录凭据。上传报告 ZIP 后可据日志排查；若需要连续远程调试，再配置局域网 OpenSSH。

## CI 与本地构建

工作流为 `.github/workflows/windows.yml`，在 `windows-2022` 上用 MSVC 2022、Qt 6.10.2 和 Python 3.12 构建。`public-release` 的 push / PR、`v*` / `windows-*` 标签以及手动触发均会运行；普通构建上传 Artifact，标签上传草稿 Release。Windows 与 macOS 共用 Release 写入锁，校验文件分别命名。

本地使用 MSVC x64 Developer PowerShell，安装 CMake 3.31、Ninja、Python 3.12、Git，设置 Qt 路径后运行：

```powershell
$env:QT_ROOT = 'C:\Qt\6.10.2\msvc2022_64'
python scripts/build_ci_windows.py
```

依赖固定于 `config/windows-dependencies.json`、`config/windows-vcpkg/vcpkg.json`，跨平台上游提交沿用 `config/macos-dependencies.json`。vcpkg 只启用 LGPL FFmpeg 组件，不启用 GPL / nonfree / x264。MediaPipe 从校验后的官方 Windows wheel 提取 C DLL，分发不包含 Python。运行库与许可随 ZIP 携带。包内检查开发路径隔离、导入 DLL、公开源码边界和模型／Python 排除；测试结果仍须以首次 Windows CI 及实机记录为准。

官方依据：[Qt 6.10 平台要求](https://doc.qt.io/qt-6.10/supported-platforms.html)、[sd.cpp 构建](https://github.com/leejet/stable-diffusion.cpp/blob/b13a48219d5cdf408edcb93da6cd69b612ed03d3/docs/build.md)、[DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)、[Windows UTF-8 清单](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)。
