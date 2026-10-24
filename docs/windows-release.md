# Windows 编译与验收

Windows 版目标为 **Windows 10 1903+ / Windows 11、x64**。Qt 6.10 支持 Windows 10 1809+；本包额外使用 Windows 10 1903 起的 UTF-8 进程清单处理中文路径。当前已发布待实机验收的测试预览版。

## 安装与启动

新的 Windows 分发使用 `XAvatar-0.1.0-Windows-x64-Setup.exe` 单文件安装程序。双击安装后，从桌面或开始菜单启动“新身数字人”（英文系统为 XAvatar），无需在解压目录中寻找程序。应用主程序为 `XAvatar.exe`，使用与 macOS 相同的产品图标，并包含 Windows 文件属性中的产品名称和版本。

安装到当前用户的 `AppData\Local\Programs\XAvatar`，不要求管理员权限。可在 Windows“已安装的应用”中卸载；下载的模型、设置和作品保存在原用户目录，升级和卸载保留这些数据。首次进入先多选创作资源、确认下载，关闭资源对话框后会提示从输入框下拉菜单选择功能；两项引导均记录完成状态，不会每次启动重复显示。

下载入口为 [Releases](https://github.com/425776024/MOSS-Image-Qt/releases)。安装包仅在对应 Windows 构建的打包、中文路径安装、文件完整性、已安装程序启动、快捷方式和卸载检查全部成功后发布。旧预览版 ZIP 仍保留在历史 Release，新的安装包以 `XAvatar-...-Setup.exe` 为准。

不需要 Python、Qt SDK 或 CUDA Toolkit。模型在 App 中按需下载；显卡需安装提供 Vulkan / DirectX 12 的厂商驱动。Windows N 版可能需要系统 Media Feature Pack。

Windows 模型下载仅使用 ModelScope。升级后，“自动选择”、hf-mirror 和 Hugging Face 的旧设置会自动迁移，已有模型和有效断点保留。安装包发布前会实际打开资源设置页，检查唯一选项为 ModelScope，并验证中英文、深浅主题、最小窗口与键盘操作。CUDA/cuDNN 加速组件仍使用独立的 GitHub Release 下载和缓存，与模型下载源分别管理。

| 用途 | Windows 后端 | 验收状态 |
| --- | --- | --- |
| MOSS 图像生成、局部编辑、布局 | stable-diffusion.cpp Vulkan | 待实机生成 |
| Ming-3.0 语音与音色设计 | audio.cpp Vulkan；可显式切换 CPU 诊断 | 待实机生成和性能对比 |
| 音效 | sa3.cpp Vulkan | 待实机生成 |
| Ditto 数字人 | NVIDIA 默认 CUDA 优先；初始化不可用时自动 DirectML；AMD/Intel 使用 DirectML | 待实机生成与性能对比 |
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

## 数字人 GPU 排查与对照

Windows 原先沿用了 macOS 的 CPU 模型选择，导致 `hubert`、`denoise` 和公共模型包的 `warp` 被整个放在 CPU。
现在这些重计算模型默认启用 GPU：优先检测 NVIDIA 驱动并启动隔离目录中的 CUDA 运行库，实际执行一个禁止 CPU 回退的 Conv + MatMul + 5D GridSample 探针；
探针成功才交给 CUDA 生成。不支持 CUDA、驱动初始化失败或探针失败时，明确记录原因并自动使用 DirectML。
小型关键点和 stitching 网络在 Windows 仍默认使用 CPU，避免逐次传输开销。
CUDA 多卡自动优先显存较大的设备；DirectML 按 DXGI 高性能偏好选择，再映射回原始 adapter index，不再固定使用显卡 0。
CUDA 使用 ONNX Runtime 1.25.0（已包含 5D GridSample CUDA kernel），DirectML 保留 1.23.0；两者核心 DLL 同名，分目录并通过独立子进程运行；取消或关闭父任务会通过 Windows Job Object 终止 CUDA 子进程。
Windows 不读取仅为 Core ML 转换的图。

这不代表全链路都在 GPU。[DirectML 的算子限制](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)
包括 opset 20 的 5D GridSample；warp 中不支持的算子仍由 ORT 分配给 CPU。
CUDA 路径可使用 CUDA provider 支持的算子，避免 DirectML 的上述限制；是否委派仍以逐模型 kernel profile 为准。
主安装包仅包含主程序、必要的界面/基础运行依赖、引擎程序和 `cuda-runtime.json`，不包含 CUDA/cuDNN 大型 DLL。首次在有可用 NVIDIA 驱动的设备上执行数字人任务时，应用自动准备加速资源并继续任务；进度和取消沿用当前任务流程。用户无需单独安装 cuDNN、CUDA Toolkit 或 Python。
CUDA 12.8、cuDNN 9.10、ONNX Runtime GPU 按上游依赖分别生成 ZIP 组件，每个组件只包含运行 DLL 和许可证。文件内容决定组件版本，应用版本和构建时间不影响组件身份。组件存入 `QStandardPaths::AppLocalDataLocation/runtimes/components/<sha256>/`，所需组合在 `runtimes/sets/<id>/` 中通过硬链接组装（文件系统不支持时复制）。因此更新 ONNX Runtime 不会重新下载未改变的 cuDNN，主程序更新也不会清理运行库。
每次使用会校验已安装组件；未改变且完整的组件不产生下载请求。下载支持断点续传；归档和解压后的每个文件分别校验 SHA-256，全部通过后才原子安装。损坏组件隔离后重新下载，取消保留部分下载；旧组件组合保留用于应用回退。不同应用实例通过锁串行准备组件。
CI 将主安装包与 `Windows-runtime-components` 分开上传。受信任发布任务先验证并上传缺少的组件到 `gpu-runtime-components-v1` Release（以内容哈希命名，禁止覆盖），确认远端 digest 后再公开主安装包。常规升级只下载主安装包，只有组件版本改变或文件损坏才下载相应组件。构建端也独立缓存经过校验的 CUDA 文件。
CUDA 子进程位于程序的 `cuda/` 目录，DLL 从独立缓存加载；父 worker 在创建子进程前通过 `SetDllDirectoryW` 指定缓存，确保它优先于 Windows System32 中的 ONNX Runtime，再用独立工作目录和 PATH 隔离主目录中的 DirectML 版本。CI 通过同一启动路径检查 CUDA 核心版本、provider 和 CPU 张量，无需构建机器具备 NVIDIA 显卡。NVIDIA 显卡驱动仍由系统提供。首次在线下载后的组件可离线使用；当前标准发行流程不额外生成离线整合安装包。
模型精度仍为原始 FP32，CUDA 关闭 TF32，未自动转换 FP16 权重，不承诺尚未测得的加速倍数。

安装目录附带以下实机脚本，使用同一组模型、人像、音频和随机种子，默认各生成 0.8 秒（20 帧）：

```powershell
.\test_ditto_gpu.ps1 -ModelDirectory 'D:\MossModels\ditto' -Image 'D:\测试素材\人像.png' -Audio 'D:\测试素材\语音.wav' -CompareCpu -CompareDirectML
```

桌面新建 `XAvatar-GPU-日期时间` 证据目录，包含两组视频、`engine.log`、原始 ORT profile 和 `summary.json`。
每个模型记录载入耗时、运行次数、运行耗时、实际执行 provider 的 kernel 事件数和累计时间；GPU/CPU 混合执行明确标记为 `mixed`。
没有 GPU kernel 证据时 GPU 测试失败，不能只凭 provider 枚举或初始化日志判定成功。
首次编译、profile 开销、预处理和封装均可能影响总耗时；请分别比较模型时间、首帧时间和完整视频耗时，并查看口型、身份与画面质量。
`gpu.csv` 如有生成，来自 `nvidia-smi` 全设备采样，不代表本进程独占 GPU 利用率。

开发排查可设置 `XINSHEN_ONNX_DEVICE=cpu` 强制所有 ONNX 模型使用 CPU；`auto` 默认自动选择 CUDA → DirectML。
`cuda` 强制 CUDA，初始化失败直接报错；`directml` 跳过 CUDA。自动回退只适用于初始化探针，正式模型推理失败不换后端重跑。
按模型覆盖使用 `XINSHEN_ONNX_DEVICE_WARP=cpu|gpu|cuda|directml`、`XINSHEN_ONNX_DEVICE_DENOISE=cpu|gpu|cuda|directml` 等变量，
分段模型分别使用 `DENOISE_CONDITION` 与 `DENOISE_STEP`。全局 `cpu` 优先于这些覆盖，以保证 CPU 对照有效。
显式指定某模型使用 `cuda`/`directml` 时，须与全局选择的运行库一致；跨运行库的单模型混用会明确报错。
`XINSHEN_DML_DEVICE_ID` 可指定 DXGI 原始 adapter index，`XINSHEN_CUDA_DEVICE_ID` 可指定 CUDA index，非法值或不可用设备会明确报错。
普通任务始终记录逐模型耗时；设置 `XINSHEN_ONNX_PROFILE_DIR` 后额外保存 ORT kernel 证据。
启用 DirectML 后若 provider 初始化或模型加载失败，直接返回错误，不静默重建 CPU session。

macOS 的 Ditto 仅接受 Apple Silicon 加速，不提供 CPU 模式。Core ML 不兼容时自动尝试原生 WebGPU/Metal，两条路径均禁止 CPU EP 回退。
同时检查 Core ML 计算计划是否把推理算子分配给 CPU；常量元数据不计入推理算子。`XINSHEN_ONNX_DEVICE=coreml|metal` 可仅选择指定加速后端。
这不是整段纯 GPU 生成已通过的声明：本机公开模型测试中，`landmark203` 的 Core ML 计划有 517 个 GPU 运算；`motion` 只有 463/481 个节点可交给 Core ML，
但同一个原始 `motion` 模型在原生 Metal 路径实际返回了 7 组有限输出，profile 记录 446 个 WebGPU provider kernel 事件、没有 CPU EP 事件。
公开 `denoise` 和 `warp` 在原生 Metal 中仍无法建立完整的无 CPU 会话；CPU YuNet 路径也会被拒绝，因此当前公开模型未完成整段视频生成。
Core ML 计算计划仅表示预期设备分配，不能替代实际设备利用率测量。

## CI 与本地构建

工作流为 `.github/workflows/windows.yml`，在 `windows-2022` 上用 MSVC 2022、Qt 6.10.2 和 Python 3.12 构建。`master` / `dev` 的 push / PR、`v*` / `windows-*` 标签以及手动触发均会运行；普通构建上传 Artifact，标签上传草稿 Release。Windows 与 macOS 共用 Release 写入锁，校验文件分别命名。

本地使用 MSVC x64 Developer PowerShell，安装 CMake 3.31、Ninja、Python 3.12、Git，设置 Qt 路径后运行：

```powershell
$env:QT_ROOT = 'C:\Qt\6.10.2\msvc2022_64'
python scripts/build_ci_windows.py
```

依赖固定于 `config/windows-dependencies.json`、`config/windows-vcpkg/vcpkg.json`，跨平台上游提交沿用 `config/macos-dependencies.json`。vcpkg 只启用 LGPL FFmpeg 组件，不启用 GPL / nonfree / x264。MediaPipe 从校验后的官方 Windows wheel 提取 C DLL，分发不包含 Python。运行库与许可随安装程序携带。首轮 Windows CI 已通过 15 项 CTest、31 项 Python 分发测试、开发路径隔离启动、941 项 DLL 导入检查、公开源码边界及模型／Python 排除审计；Ditto 已验证 ONNX Runtime 1.23.0 初始化、CPU 张量及 DirectML provider 枚举。实际 GPU 推理及桌面验收仍以实机记录为准。

常规打包默认不编译和运行单元测试，保留安装程序、中文目录、启动、快捷方式、卸载及文件校验。
PNG 编解码是必需的发布检查：Windows FFmpeg 显式启用 `zlib`，否则即使 DLL 加载正常也没有 PNG 编解码器。
打包和安装后分别使用实际分发的 `moss-ditto media-check` 读取独立生成的 RGB、带透明度的 RGBA PNG，
写出 PNG 后重新读取并逐像素比较颜色与透明度；失败即停止发布。这些检查不依赖模型或 GPU，也不受 `run_tests` 开关影响。
结果记录于 `windows-bundle-audit.json` 的 `mediaIo` / `installedMediaIo`，不能替代数字人推理及成片验收。
排查问题时可手动触发工作流并勾选 `run_tests`，或本地运行 `python scripts/build_ci_windows.py --run-tests`。
vcpkg 缓存保存已完成的依赖包，完整原生引擎单独缓存；工具链变化会使部分缓存失效，冷构建可能超过一小时。
`master` 的 push 构建成功后自动发布。手动打包完成后，在 **Publish development downloads** 中填写该次 `run_id` 发布；
发布任务在 Actions 内校验安装包和源码提交，避免开发机重复下载、再上传整个安装包。PR、其他分支及失败构建不允许发布。

首次 `win-0.1.0-preview.1` 是从已通过 CI 的 Windows 分支提交手动发布，未合并待实机验收的 PR；与上面的 `v*` / `windows-*` 标签自动生成草稿流程分开。

官方依据：[Qt 6.10 平台要求](https://doc.qt.io/qt-6.10/supported-platforms.html)、[sd.cpp 构建](https://github.com/leejet/stable-diffusion.cpp/blob/b13a48219d5cdf408edcb93da6cd69b612ed03d3/docs/build.md)、[DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)、[Windows UTF-8 清单](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)。
