# 构建与运行

GitHub 托管 Mac 的自动构建、App/DMG 下载、Developer ID 签名与 Apple 公证配置，见
[macOS 自动发布](macos-release.md)。完整 CI 依赖从公开上游下载，不使用开发者的本地 SDK 路径。


目标平台为 Apple Silicon macOS。需要 Xcode/Metal、CMake、Ninja、Qt 6.10.2，
以及 ONNX Runtime、OpenCV、OpenSSL、FFmpeg、nlohmann/json、MediaPipe C API，
语音模块还需 sherpa/kaldifst。依赖源和许可需保留，公开源码不附带开发者本机库。

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.2/macos" \
  -DMOSS_FFMPEG_ROOT=/path/to/ffmpeg \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime \
  -DMOSS_MEDIAPIPE_ROOT=/path/to/mediapipe \
  -DMOSS_AUDIO_DEPS=/path/to/audio-deps \
  -DJSON_INCLUDE=/path/to/nlohmann/include \
  -DVOICE_JSON_INCLUDE=/path/to/nlohmann/include
cmake --build build -j 6
ctest --test-dir build --output-on-failure
./build/MossStudio.app/Contents/MacOS/MossStudio
```

仅调试界面和下载管理时可设 `-DMOSS_BUILD_DITTO=OFF -DMOSS_BUILD_VOICE=OFF`。
图像与音效工作进程分别来自 stable-diffusion.cpp 和 sa3.cpp。
`scripts/build.sh` 准备固定提交的 stable-diffusion.cpp；本地已配置好依赖时可运行
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
源码许可证需作者在发布前确定。

自动测试默认离屏运行；`--home-smoke`、`--smoke-ui`、`--avatar-test` 等不会抢占桌面焦点。
离屏截图、测试通过和短片生成分别提供界面、代码与运行通路证据，不代表发行验收或性能保证。

公版工作区使用独立分支与历史；不要推送含私人服务器程序的开发分支历史。
打包前后会检查 App 的公版本地运行标记，旧版或带远程 worker 的构建会被拒绝。
