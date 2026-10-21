# GitHub 自动打包、签名和公证

工作流：`.github/workflows/macos.yml`。当前仅支持 **Apple Silicon / macOS 15+**，
不生成 Intel 或 Windows 版本。GitHub 托管的 `macos-15` runner 构建，无需把个人 Mac 注册为 runner。

## 普通构建

推送到 `codex/public-release`，向默认分支提交 PR，或手动运行工作流，
会从公开依赖构建完整 App：Qt 界面、sd-cli、moss-ditto、moss-voice、sa3-generate。
模型不进入包，由用户在模型中心按需下载。无需配置 Apple Secrets 即可构建。

在 Actions → macOS App and DMG → 对应运行 → Artifacts 下载 `macOS-arm64-unsigned`，里面包含：

- `Moss-Studio-<版本>-macOS-arm64-unsigned.app.zip`：保留 App 权限和符号链接的 ZIP。
- `Moss-Studio-<版本>-macOS-arm64-unsigned.dmg`：带 Applications 快捷方式的 DMG。
- `SHA256SUMS.txt` 和 `release-manifest.json`。

`unsigned` 包只有本地 ad-hoc 签名，**不是公证发布版**，不能承诺通过 Gatekeeper。
它不会自动进入 Releases。流程运行 CTest、公开源码边界审计、App 依赖审计、DMG 校验，
以及部署后离屏启动测试。CI 不下载大模型，也不宣称已验收模型推理、GPU 性能或真实用户首次安装。

## 先在本机公证（无需导出签名私钥）

本机已经有正式的 Developer ID Application 证书及私钥时，可直接使用钥匙串签名。
从成功的主分支 Actions 下载测试包，核对 `SHA256SUMS.txt`，再用 `ditto -x -k` 解压 App ZIP。

首次公证需在 [Apple 账号](https://account.apple.com/)的“登录与安全 → App 专用密码”中
生成专用密码，然后在自己的终端运行下面的命令。替换账号和 Team ID，
专用密码由 Apple 工具隐藏输入，不要写入命令参数、脚本或聊天：

```sh
xcrun notarytool store-credentials MossStudio-release \
  --apple-id '你的开发者账号邮箱' --team-id '你的 Team ID'
```

凭据验证成功后保存在本机钥匙串。用 `security find-identity -v -p codesigning`
查找正式 Apple 签发的 Developer ID Application 的 40 位 SHA-1 指纹，然后运行：

```sh
python3 scripts/macos_distribution.py sign-notarize-local 'dist/Moss Studio.app' \
  --identity '你的证书指纹' --keychain-profile MossStudio-release --output dist/notarized
```

本机模式与 CI 使用同样的签名、公证、附票和 Gatekeeper 检查；不导出 `.p12`，也不上传私钥到 GitHub。
它适合先完成一次发布。若要让 GitHub 后续全自动公证，再配置下一节的 CI 凭据。
证书名称相同的自签名证书不能代替 Apple 正式签发的 Developer ID 证书。

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
   `codex/public-release`，允许的标签为 `v*`。如默认分支改名，更新这里及工作流触发条件。
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
