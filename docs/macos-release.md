# GitHub 自动打包、签名和公证

工作流：`.github/workflows/macos.yml`。当前仅支持 **Apple Silicon / macOS 15+**，
此工作流不生成 Intel 或 Windows 版本；Windows 由 `.github/workflows/windows.yml` 同步构建。GitHub 托管的 `macos-15` runner 构建，无需把个人 Mac 注册为 runner。

## 普通构建

推送到 `public-release`，向默认分支提交 PR，或手动运行工作流，
会从公开依赖构建完整 App：Qt 界面、sd-cli、moss-ditto、moss-voice、sa3-generate。
两个平台均构建触发该次运行的提交，不固定旧分支或旧标签；新推送会取消同分支尚未完成的旧推送构建。
本地改动须先提交并推送才会进入 CI；手动签名和标签发布不受推送取消影响。
模型不进入包，由用户在模型中心按需下载。无需配置 Apple Secrets 即可构建。

在 Actions → macOS App and DMG → 对应运行 → Artifacts 下载 `macOS-arm64-unsigned`，里面包含：

- `Moss-Studio-<版本>-macOS-arm64-unsigned.app.zip`：保留 App 权限和符号链接的 ZIP。
- `Moss-Studio-<版本>-macOS-arm64-unsigned.dmg`：带 Applications 快捷方式的 DMG。
- `SHA256SUMS.txt` 和 `release-manifest.json`。

`unsigned` 包只有本地 ad-hoc 签名，**不是公证发布版**，不能承诺通过 Gatekeeper。
macOS 未公证开发包只保留在 Actions Artifacts，不再自动公开发布到 Releases。
`publish-preview.yml` 仅自动发布 Windows 构建；macOS 使用下面的本地一键脚本。
流程运行 CTest、公开源码边界审计、App 依赖审计、DMG 校验，
以及部署后离屏启动测试。CI 不下载大模型，也不宣称已验收模型推理、GPU 性能或真实用户首次安装。

## 本地一键公证和发布

在签名 Mac 上双击 `scripts/release_macos.command`，或在仓库根目录运行：

```sh
./scripts/release_macos.command
```

脚本会选择 GitHub 当前 `public-release` 提交的 macOS push 构建；仍在构建时会等待。
失败、取消、PR 或非主线构建不会发布，也不会悄悄退回旧构建。
它不提交或上传本地未提交代码；界面修改须先提交并推送到主线。

执行流程：校验构建来源与 SHA-256 → 下载 App ZIP → 验证包内路径 →
使用钥匙串 Developer ID 签名 → App 公证、附票与 Gatekeeper 检查 →
生成 DMG → DMG 签名、公证、附票与 Gatekeeper 检查 →
签名后离屏启动 → 上传 → 核对 GitHub 返回的资产 SHA-256 → 更新发布说明。

同一构建使用 `macos-build-<run-id>` 下载条目：若原来是未公证预览，完成后改成“已公证”，
仅删除该条目中被替代的未公证 App ZIP/DMG；其他版本不受影响。
新条目先以草稿上传，校验完毕再公开。版本仍标为预发布，不冒充正式稳定版。
README 始终链接到下载目录，无需修改版本号。

下载、公证产物和日志放在 `.local/macos-release/<run-id>/`，不会进入 Git。
上传失败后重跑会复用已完成公证的产物，校验并跳过已上传的相同文件。
公证阶段失败会保留该次 `attempt-*` 日志，下次从已校验的下载包重新签名、公证。
同一构建的并发操作会被本地文件锁阻止。

若公证期间有更新的成功构建，默认停止发布旧构建并保留产物，重新运行即可处理新版。
需要明确重试某次成功主线构建时，可指定 Actions run ID：

```sh
./scripts/release_macos.command --run-id 123456789
```

### 首次准备

需要 Xcode Command Line Tools、Python 3.9+、已登录且可发布此仓库 Release 的 `gh`，
以及钥匙串中的正式 **Developer ID Application** 证书和对应私钥。
只有一个正式签名身份时自动选择，忽略名字包含 Self-Signed 的测试身份。
多身份时用 `--identity <SHA-1指纹>` 或环境变量 `MOSS_SIGNING_IDENTITY` 指定。
最终仍以 Apple 公证结果和 Gatekeeper 检查为准。

公证凭据默认读取钥匙串 profile `MossStudio-release`；可用
`--keychain-profile <名称>` 或 `MOSS_NOTARY_PROFILE` 改名。
已有可用 profile 无需重复设置。首次配置时，在自己的终端执行：

```sh
gh auth login
xcrun notarytool store-credentials MossStudio-release \
  --apple-id '你的开发者账号邮箱' --team-id '你的 Team ID'
```

专用密码由 Apple 工具隐藏输入，不要写入脚本、参数或聊天。
脚本不导出 `.p12`，不读取密码，不把私钥或 Apple 认证凭据上传 GitHub。

底层手动公证命令仍可用于已部署的本地 App，但它本身不上传：

```sh
python3 scripts/macos_distribution.py sign-notarize-local 'dist/Moss Studio.app' \
  --identity '你的证书指纹' --keychain-profile MossStudio-release --output dist/notarized
```

## 一次性配置 GitHub Apple 凭据

本机 Xcode 已登录，不代表 GitHub 的临时 Mac 拥有签名权限。需要手动、安全地配置一次：

1. 用“钥匙串访问”找到 **Developer ID Application**，确认它下面有对应私钥。
   只导出选中的这一份签名身份为 `.p12`，设置强密码；不要导出整个钥匙串。
   Apple Development / Apple Distribution / Developer ID Installer 均不能替代它。
2. 在 App Store Connect → 用户和访问 → 集成 / API 中创建一个用于 CI 的**团队 API Key**，
   选择满足公证服务要求的最低角色。保存下载的 `.p8`、Key ID、Issuer ID。
   本流程使用带 Issuer ID 的团队密钥，不接受需要省略 issuer 的个人 API key。
   `.p8` 只可下载一次；保存在仓库外。无需 Apple 主密码或双重认证验证码。
3. 在仓库 Settings → Environments 创建 `macos-release`，限制允许的部署分支为
   `public-release`，允许的标签为 `v*`。如默认分支改名，更新这里及工作流触发条件。
   同时通过仓库 Rulesets 限制发布标签只能由维护者创建/修改，并保护默认分支和 workflow 修改。
   如套餐支持，可添加 Required reviewers。只有一个维护者时不要启用“禁止自行批准”。
4. 在该 Environment 的 Secrets 中添加下表内容。不要使用普通 Variables。

| Secret | 内容 |
|---|---|
| `APPLE_CERTIFICATE_P12_BASE64` | 包含证书及私钥的 `.p12` 的单行 Base64 |
| `APPLE_CERTIFICATE_PASSWORD` | `.p12` 导出密码 |
| `APPLE_NOTARY_KEY_P8` | `.p8` 文件原文，保留换行 |
| `APPLE_NOTARY_KEY_ID` | API Key ID |
| `APPLE_NOTARY_ISSUER_ID` | 团队 Issuer ID |

也可在本机终端运行交互式导入工具。它从文件及隐藏密码输入读取，再经 `gh secret set` 的
标准输入加密上传；不把密钥写入命令行、聊天或日志。它要求先配置限制了分支的 Environment：

```sh
gh auth login
python3 scripts/import_apple_secrets.py
```

导入工具不会导出钥匙串，不会读取其他开发者账号，也不会把账号主密码上传 GitHub。
Base64 本身不是加密，绝不能把 `.p12` 的 Base64 提交进 Git。源码审计也拒绝常见私钥文件后缀。
签名中的开发者姓名、Team ID 属于公开证书信息，签名后的 App 无法隐藏它们。

## 正式发布

先合并工作流到默认分支，在 Actions 手动运行，勾选 `notarize`，验证签名、公证通路。
签名任务只接受默认分支上的已审查提交；手动签名必须选择默认分支。

正式版本的 `CMakeLists.txt` 中项目版本、Bundle 版本和 `src/main.cpp` 的 App 版本须一致。
例如 0.1.0 发布标签必须为 `v0.1.0`，不能用无关测试标签冒充该版本。

```sh
git tag v0.1.0 <已合并且验证通过的提交>
git push origin v0.1.0
```

流程自动：构建 → 新临时 runner → 验证当前运行产物 → 从内到外 Developer ID 签名 →
App 公证并 staple → 生成 App ZIP 和 DMG → DMG 签名、公证并 staple →
Gatekeeper 验证 → 清理密钥后离屏启动 → 上传 `macOS-arm64-notarized` → 创建 **Draft Release**。
App 和 DMG 分别附票，因此 ZIP 解压出来的 App 也具有公证票据。
公证失败、凭据缺失或标签不匹配都会失败，不会降级成未签名的正式发布包。

最后在一台无开发环境的 Mac 上，通过浏览器下载草稿附件，测试安装、首次启动、模型下载和生成，
再点击 Publish release。首次互联网下载确认、系统权限询问仍可能正常出现。
签名不等于软件功能与性能验收。

## 依赖和安全边界

`config/macos-dependencies.json` 固定下载 URL、SHA-256 或 Git commit。
FFmpeg 从源码构建，关闭 GPL/nonfree/自动依赖探测；OpenCV 仅构建 core/imgproc/dnn，
不引入 Homebrew OpenCV 的 FFmpeg 依赖。MediaPipe 只从官方 wheel 提取原生动态库，
不会把 Python runtime 放进 App。Qt 版本固定为 6.10.2，OpenSSL 等 Homebrew 工具仍可能更新，
本工作流不是逐字节可复现构建的承诺。第三方义务见 `THIRD_PARTY.md`。

PR 构建没有 Apple 凭据。签名只在单独受保护的任务/步骤注入 Secret，使用临时钥匙串，
不缓存凭据、不运行上传后的 App 直到清理完成。第三方 Actions 固定完整 commit SHA。
不要改用 `pull_request_target` 执行 PR 代码；不要签署来源不明的下载产物。
有权修改获准运行的 workflow 的人仍可能窃取 Secrets，因此分支、标签和审查保护是必要边界。

GitHub 的私有仓库 runner 额度和 Environment 审批能力取决于账号套餐；本流程不修改仓库可见性。

参考：[Apple 公证](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)、
[GitHub 证书导入](https://docs.github.com/en/actions/how-tos/deploy/deploy-to-third-party-platforms/sign-xcode-applications)、
[GitHub Secrets](https://docs.github.com/en/actions/concepts/security/secrets)。
