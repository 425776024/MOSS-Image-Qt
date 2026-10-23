# Windows 编译与验收

Windows 版目标为 **Windows 10 1903+ / Windows 11、x64**。Qt 6.10 支持 Windows 10 1809+；本包额外使用 Windows 10 1903 起的 UTF-8 进程清单处理中文路径。当前已发布待实机验收的测试预览版。

## 安装与启动

新的 Windows 分发使用 `XAvatar-0.1.0-Windows-x64-Setup.exe` 单文件安装程序。双击安装后，从桌面或开始菜单启动“新身数字人”（英文系统为 XAvatar），无需在解压目录中寻找程序。应用主程序为 `XAvatar.exe`，使用与 macOS 相同的产品图标，并包含 Windows 文件属性中的产品名称和版本。

安装到当前用户的 `AppData\Local\Programs\XAvatar`，不要求管理员权限。可在 Windows“已安装的应用”中卸载；下载的模型、设置和作品保存在原用户目录，升级和卸载保留这些数据。首次进入先多选创作资源、确认下载，关闭资源对话框后会提示从输入框下拉菜单选择功能；两项引导均记录完成状态，不会每次启动重复显示。

下载入口为 [Releases](https://github.com/425776024/MOSS-Image-Qt/releases)。安装包仅在对应 Windows 构建的打包、中文路径安装、文件完整性、已安装程序启动、快捷方式和卸载检查全部成功后发布。旧预览版 ZIP 仍保留在历史 Release，新的安装包以 `XAvatar-...-Setup.exe` 为准。

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

1. 打开安装目录，双击 `test_windows.cmd`。桌面会生成 `Moss-Test-日期时间.zip`，包含系统、显卡驱动、离屏启动结果和日志；这一轮不下载模型。
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

工作流为 `.github/workflows/windows.yml`，在 `windows-2022` 上用 MSVC 2022、Qt 6.10.2 和 Python 3.12 构建。`master` / `dev` 的 push / PR、`v*` / `windows-*` 标签以及手动触发均会运行；普通构建上传 Artifact，标签上传草稿 Release。Windows 与 macOS 共用 Release 写入锁，校验文件分别命名。

本地使用 MSVC x64 Developer PowerShell，安装 CMake 3.31、Ninja、Python 3.12、Git，设置 Qt 路径后运行：

```powershell
$env:QT_ROOT = 'C:\Qt\6.10.2\msvc2022_64'
python scripts/build_ci_windows.py
```

依赖固定于 `config/windows-dependencies.json`、`config/windows-vcpkg/vcpkg.json`，跨平台上游提交沿用 `config/macos-dependencies.json`。vcpkg 只启用 LGPL FFmpeg 组件，不启用 GPL / nonfree / x264。MediaPipe 从校验后的官方 Windows wheel 提取 C DLL，分发不包含 Python。运行库与许可随安装程序携带。首轮 Windows CI 已通过 15 项 CTest、31 项 Python 分发测试、开发路径隔离启动、941 项 DLL 导入检查、公开源码边界及模型／Python 排除审计；Ditto 已验证 ONNX Runtime 1.23.0 初始化、CPU 张量及 DirectML provider 枚举。实际 GPU 推理及桌面验收仍以实机记录为准。

常规打包默认不编译和运行单元测试，保留安装程序、中文目录、启动、快捷方式、卸载及文件校验。
排查问题时可手动触发工作流并勾选 `run_tests`，或本地运行 `python scripts/build_ci_windows.py --run-tests`。
vcpkg 缓存保存已完成的依赖包，完整原生引擎单独缓存；工具链变化会使部分缓存失效，冷构建可能超过一小时。

首次 `win-0.1.0-preview.1` 是从已通过 CI 的 Windows 分支提交手动发布，未合并待实机验收的 PR；与上面的 `v*` / `windows-*` 标签自动生成草稿流程分开。

官方依据：[Qt 6.10 平台要求](https://doc.qt.io/qt-6.10/supported-platforms.html)、[sd.cpp 构建](https://github.com/leejet/stable-diffusion.cpp/blob/b13a48219d5cdf408edcb93da6cd69b612ed03d3/docs/build.md)、[DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)、[Windows UTF-8 清单](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)。
