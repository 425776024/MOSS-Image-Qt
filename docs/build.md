# 构建与运行

GitHub 托管 Mac 的自动构建、App/DMG 下载、Developer ID 签名与 Apple 公证配置，见
[macOS 自动发布](macos-release.md)。完整 CI 依赖从公开上游下载，不使用开发者的本地 SDK 路径。

Windows 10/11 x64 的 Vulkan / DirectML 构建、安装包与 RTX 3090 实机验收步骤，见
[Windows 构建与验收](windows-release.md)。GPU 推理和真实桌面验收仍需实机记录。


目标平台为 Apple Silicon macOS。需要 Xcode/Metal、CMake、Ninja、Qt 6.10.2，
以及 ONNX Runtime、OpenCV、OpenSSL、FFmpeg、nlohmann/json、MediaPipe C API，
语音模块使用固定提交的 Ming-only audio.cpp C ABI；先运行 `python3 scripts/prepare_ming.py` 构建。依赖源和许可需保留，公开源码不附带开发者本机库。

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.2/macos" \
  -DMOSS_FFMPEG_ROOT=/path/to/ffmpeg \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime \
  -DMOSS_MEDIAPIPE_ROOT=/path/to/mediapipe \
  -DMOSS_AUDIO_DEPS=/path/to/audio-deps \
  -DMOSS_MING_ROOT=/path/to/audio-deps/ming \
  -DJSON_INCLUDE=/path/to/nlohmann/include \
  -DVOICE_JSON_INCLUDE=/path/to/nlohmann/include \
  -DBUILD_TESTING=OFF
cmake --build build -j 6
./build/MossStudio.app/Contents/MacOS/MossStudio
```

仅调试界面和下载管理时可设 `-DMOSS_BUILD_DITTO=OFF -DMOSS_BUILD_VOICE=OFF`。
图像与音效工作进程分别来自 stable-diffusion.cpp 和 sa3.cpp。
`scripts/build.sh` 准备固定提交的 stable-diffusion.cpp，并应用 `vendor/moss-flowedit/` 中的原生 FlowEdit 补丁；
更新「保留原貌」功能时须同时重建图像引擎，旧引擎会提示更新。该方式不新增模型或 Python 推理依赖。
常规打包默认不编译、不运行 CTest 和 Python 单元测试；只保留公开源码边界、包依赖、安装与启动校验。
需要单元测试时，手动触发 Actions 并勾选 `run_tests`；本地可用 `MOSS_RUN_TESTS=1 ./scripts/build.sh`。
直接使用 CMake 的开发者可设 `-DBUILD_TESTING=ON` 后自行运行 CTest。
Mac 原生依赖和图像引擎按固定源码、编译器与 SDK 复用缓存；Windows 使用 vcpkg 包缓存及完整引擎缓存。
首次构建、依赖升级或工具链变化仍需重新编译，缓存不保证每次命中。
本地已配置好依赖时可运行
`scripts/run-local.sh` 增量构建。`scripts/prepare_audio.sh` 是可选的开发机依赖导入器，
要求显式设置来源路径，不导入模型。

模型默认保存在用户应用数据目录，可通过模型中心更改；不会自动探测源码中的 `.local/models`。
作品默认保存到 `~/Library/Application Support/MossStudio/MossStudio/works/`。
公开版只支持本地生成，不包含远程服务器程序或连接入口。

## 公开源码与打包

```sh
python3 scripts/public_release.py audit
python3 scripts/public_release.py source /absolute/path/to/new-public-source
./scripts/package.sh
```

默认打包不含模型。离线模型包必须提供显式目录，按公开清单校验后逐文件复制，
详见模型管理文档。`.local`、运行数据、检查点和下载临时文件始终不进入发布源码。
原创源码采用仓库根目录的 MIT 许可证；第三方库、模型与素材保留各自许可证。

自动测试默认离屏运行；`--home-smoke`、`--smoke-ui`、`--avatar-test` 等不会抢占桌面焦点。
离屏截图、测试通过和短片生成分别提供界面、代码与运行通路证据，不代表发行验收或性能保证。

公版工作区使用独立分支与历史；不要推送含私人服务器程序的开发分支历史。
打包前后会检查 App 的公版本地运行标记，旧版或带远程 worker 的构建会被拒绝。

## 修改 Qt 后运行

应用动态链接 Qt 6.10.2。对应源码归档和校验值位于
[发布附件](https://github.com/425776024/MOSS-Image-Qt/releases/tag/macos-0.1.0-preview.1)
的 `XAvatar-Qt-6.10.2-sources.zip`；许可与第三方声明位于应用的
`Contents/Resources/licenses/qt/`。用户可修改、替换和调试这些库，模型使用条款不限制这些开源权利。

推荐从本仓库重新构建应用，以 `CMAKE_PREFIX_PATH` 指向自行编译的 Qt。
普通开发构建可以使用现有镜像获取同一版本的模型，无需原开发者的签名私钥或 Apple 托管资源权限。
自行构建的应用不是原开发者签署的商店更新。

## 应用更新

左侧「更新」显示当前版本、对应平台的最新发布与更新说明；启动后会在后台检查一次，进入页面可手动重查。
社区版读取 GitHub Releases（含已发布的预览版），macOS 仅选择已公证的 Apple Silicon App ZIP，
Windows 选择 x64 安装程序。API 受限时回退 GitHub 官方 Release 订阅源及同一发布的校验和、构建来源文件。
安装包先校验 GitHub 提供的 SHA-256；macOS 还验证完整签名、
与当前应用相同的开发者 Team ID、应用标识与 Gatekeeper。相同版本号的开发构建按源码提交时间比较，
没有源码版本信息的自行构建会提示查看发布页，不宣称已是最新。

「更新并重启」下载并验证完成后保存工作、退出当前应用，独立辅助程序替换安装并启动新版。
新版打开主窗口后确认本次更新，辅助程序才删除旧版备份与下载缓存；启动失败则尝试恢复旧版。
生成或资源下载进行中不能重启更新。macOS 需位于可写安装位置；开发构建、磁盘映像中运行的应用及内嵌模型的离线包提供发布页入口。
Windows 发行包携带 `update-files.json` 文件清单，更新时保留用户额外加入安装目录的文件，清理旧发行文件。
模型、作品和设置位于应用数据目录，不属于更新清理范围。

App Store 版本从 Apple Lookup API 查询已上架的正式版，更新交由 App Store；测试版入口打开 TestFlight，
可用构建与自动更新由 TestFlight 管理。公开的商店查询不返回用户获准使用的 TestFlight 构建列表，
因此应用不会把正式版查询结果当作测试版的最新版本。商店构建不打包社区版的安装辅助脚本。

此功能须先随新安装包发布并安装，已有旧版本不会凭空获得自更新能力。
开发验证需区分元数据/下载测试、临时应用替换与回滚、真实签名发行包升级，以及 Windows 实机安装重启。

也可先复制已安装的应用，在副本的 `Contents/Frameworks` 或
`Contents/PlugIns` 中替换 ABI 兼容的库或插件。替换会使原签名失效；
需为改动的嵌套代码和最外层应用重新签名。不要修改正在使用的安装副本，
也不要尝试复用原开发者的私钥或把本地修改版上传到原应用的商店记录。

2026 年 10 月 22 日的技术验证使用 Qt SVG 6.10.2 官方源码，在
`QSvgPlugin::create` 中加入日志标记，重新编译 `QSvgPlugin`，替换 TestFlight
构建 3 独立副本中的 `libqsvg.dylib`。副本以独立套装 ID 和本机临时签名运行，
保留 App Sandbox，不申请原应用的 App Group。离屏主页渲染加载了修改后的插件，
生成截图并以状态 0 退出。这项验证覆盖 SVG 插件替换及界面启动；
不代表使用本机签名可访问 Apple 托管资源，也不替代其他组件的许可要求。
