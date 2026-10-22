# 模型下载渠道与 Apple 资源包

## 发行渠道

- 普通构建（GitHub/DMG）：新安装默认 `https://hf-mirror.com`，保留用户已选的 Hugging Face 来源。
- `-DMOSS_APP_STORE=ON`：默认 `apple-assets`，独立设置键 `storeModelEndpoint`，不继承站外版来源。用户可明确选择镜像或 Hugging Face；失败不会自动切换来源。
- App Store 正式版最低系统为 macOS 26，面向 18 岁及以上用户。GitHub/DMG 的系统要求独立维护。

## 固定版本与安装

`config/models.json` 是唯一模型清单。每个外部文件对应一个按需资源包，ID 为 `model-<完整 SHA-256>`；内置 JSON 配置仍从应用资源安装。模型升级应使用新哈希和新资源包，避免 Apple 更新旧版本资源时破坏旧 App 的固定文件校验。

原生 Objective-C++ 桥接调用 BackgroundAssets，通过指定资源包的文件描述符读取文件，分块复制进应用容器 `.part`，保存随包提供的 `NOTICE.txt`，再由现有 ModelManager 做 SHA-256 校验和原子安装。暂存的系统资源包会移除，避免长期保留两套大模型；安装后的模型与许可文件保存在应用容器。磁盘预检额外计入一个最大模型文件的临时空间。

当前仅共享主 App 与下载扩展的协调目录，并未实现跨独立 App 的模型缓存。

## 构建与签名

使用 Xcode 26+ SDK 构建。CMake 为 Store 构建额外编译：

- `src/AppleAssetDownload.mm`：调用 BackgroundAssets；商店版的构建目标与 Info.plist 均要求 macOS 26+。
- `src/apple/DownloaderExtension.swift`：原生 `StoreDownloaderExtension`，嵌入 `Contents/Extensions/XinshenAssets.appex`。
- 主应用 ID：`ai.ffclip.studio`。
- 扩展 ID：`ai.ffclip.studio.assets`。
- App Group：`group.ai.ffclip.studio.assets`，须在主 App 和扩展的描述文件及签名权限中一致配置。

主应用使用 `config/macos-store-app.entitlements`；扩展使用 `config/apple-assets/extension.entitlements`；计算工作进程使用 `config/macos-store-helper.entitlements`。先签名扩展与嵌套代码，再签名主 App。扩展必须有独立匹配的 Store 描述文件。普通 DMG 不嵌入该扩展。

已部署依赖的应用可使用 `scripts/package_appstore.py` 制作 Store 安装包；先运行 `--check-only` 校验描述文件的团队、证书、有效期、应用 ID 和 App Group。实际签名读取现有钥匙串私钥，不导出私钥。此脚本只生成安装包，不代表已上传或通过审核。项目原创代码的 MIT 许可证通过 `scripts/package.sh` 随应用打包；第三方许可另行保留。

## 打包与上传

```sh
python3 scripts/apple_model_assets.py inventory \
  --root .local/models --root .local/public-models \
  --root .local/appstore/yunet-validation/models \
  --output .local/appstore/model-inventory.json

python3 scripts/apple_model_assets.py build \
  --inventory .local/appstore/model-inventory.json \
  --model ditto-original/face_detection_yunet_2023mar.onnx \
  --notice vendor/xinshen-ditto/LICENSE.YuNet \
  --output .local/appstore/asset-packs
```

脚本先校验本地文件，再制作仅包含该文件、来源信息和许可的 `.aar`；不递归打包整个模型目录。打包使用同卷临时硬链接，源模型在打包期间不可修改。`--notice` 必须提供适用的许可全文与署名；有文件不等于自动获得再分发权。

用户在自己的终端运行 `python3 scripts/configure_appstore_upload.py`，隐藏输入专用密码并存入本机钥匙串。脚本包含当前 altool 的 label/service 兼容修复，只调整记录元数据。上传使用钥匙串引用，不在仓库、命令文本或 CI 中放密码：

```sh
xcrun altool --upload-asset-pack /absolute/path/model-HASH.aar \
  --apple-id 6825373745 --username YOUR_APPLE_EMAIL \
  --password '@keychain:Xinshen-AppStore-Upload' --wait
```

上传成功、资源包处理完成、TestFlight 下载成功和 App Store 审核通过是四个不同阶段。必须用实际 TestFlight 安装测试首次下载、取消重试、离线复用、SHA-256 不符、磁盘不足与删除重下；本地编译和模拟测试不能替代这些验收。

参考：
- https://developer.apple.com/documentation/backgroundassets/downloading-apple-hosted-asset-packs
- https://developer.apple.com/documentation/backgroundassets/creating-managed-asset-packs

## Ming Q8 语音模型更新

两个版本共用 `voice/ming-3.0-tts-0.5b-q8_0.gguf`，不再请求 Hojo/FST
资源包。新资源包 ID 为
`model-fcf2aa25e8e9cf2065a936e506581735c64d6255fb7869f09fbb61a2dde09aa1`。
旧版本 App 所需资源包不可覆盖或删除。制作新包时同时传入
`--notice vendor/videocut-voice/licenses/ming/Ming-MIT.txt` 和
`--notice vendor/videocut-voice/PROVENANCE.md`。新 App 与新资源包必须配套发布；
本地生成 `.aar` 不代表已上传、Apple 已处理或 TestFlight 下载已验收。
