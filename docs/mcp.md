# 内置 MCP

从左侧 **MCP → 开启服务 → 复制接入提示词**，粘贴给支持 Streamable HTTP 的 Agent。也可复制连接配置或接口说明。无需安装额外的 Python、Node 或后台服务；MCP 直接运行在 Qt 应用进程中。开发版已接入，发布安装包是否包含取决于构建版本。

首次默认关闭；手动开启后记住设置，随应用启动。关闭服务会断开客户端并记住关闭状态，已经接收的任务继续执行。退出应用会停止监听、结束应用工作进程，并将未完成 MCP 任务标记为 `interrupted`，不会在重启后自动重复生成。

实时面板保留本次运行最近 200 条调用（优先保留未完成任务），每条两行：接口名及时间、状态及耗时或执行阶段。右侧可查看和复制参数、进度及结果。清空面板不删除任务记录或生成作品。

## 局域网内的另一台 Agent 机器

在运行 App 的机器上开启 **允许局域网连接**，再复制接入提示词或配置到另一台机器。两台机器需要网络互通。多网卡时，在面板选择另一台机器能访问的 IPv4 地址，例如 `http://192.168.10.20:39871/mcp`；不能给另一台机器使用 `127.0.0.1`。开关默认关闭，开启状态会保存；关闭后立即恢复仅本机监听。只在可信局域网使用：当前 HTTP 不加密，凭证及素材会通过该网络传输。

如果连接失败，检查是否选错网卡、Wi-Fi 是否启用了客户端隔离、App 机器防火墙是否允许该应用的入站连接，以及 Agent 所在软件是否获准访问局域网。应用不会自动修改防火墙或路由器设置。当前支持私有/链路本地 IPv4，不提供公网部署、IPv6 或自动发现。

## 跨机器上传、预览与下载

接口管理接收目录，Agent 不需要知道 App 的目录结构：

1. `media_upload_begin`：提供文件名、原始字节数，可附 SHA-256，取得 `uploadId`。
2. `media_upload_chunk`：发送 `uploadId`、字节偏移 `offset`、`dataBase64`。每块原始数据最多 1 MiB；相同分块可以重试。
3. `media_upload_finish`：核对完整大小、可选 SHA-256 和媒体类型。单文件最多 2 GiB，最多 16 个未完成上传；这些限制不替代音色库 3–30 秒、20 MB 等用途限制。
4. 向 `asset_import`、`avatar_create/import`、`voice_create/import` 或 `sfx_import` 传 `uploadId`。形象和音色完成后进入 App 资源库并持久化，作品素材进入指定作品。
5. 完成入库后可用 `media_upload_discard` 清理暂存；已复制到作品或库的文件不受影响。`media_upload_status` 查询持久化字节进度，可在重新连接后续传；不传 ID 可查看最近上传与未完成上传。

例如上传图片后直接创建形象：

```json
{"name":"avatar_create","arguments":{"name":"小林","uploadId":"上传返回的 ID","requestKey":"remote-avatar-01"}}
```

返回的作品素材、形象和音色都附带 `media` 信息：`downloadUrl`、`previewUrl`、`mimeType`、`size`、`filename`、`expiresAt`。生成任务结果同样附带这些信息。

- **图片**：`media_preview` 按 `source: {workId, assetId}`、`uploadId` 或 `avatarId` 返回原生 MCP `image` 内容块，最长边 1280 像素。原图从媒体链接下载，预览图不替代原文件。
- **音视频**：媒体链接支持 `GET`、`HEAD`、单段 `Range: bytes=…`，可用于支持链接的播放器，包括跳转进度。链接不需要再附完整 MCP 访问凭证，可用于前端展示。
- **下载到 Agent 本机**：可直接下载 `downloadUrl`；也可用 `media_read` 分块取得 `dataBase64`，解码并按顺序写入本机文件，直到 `eof=true`。这也适用于不能直接读取媒体 URL 的 Agent 工具环境。
- **对话页面显示**：图片内容块或媒体链接是否自动显示为图片/播放器取决于客户端。有些聊天页面会拦截私有 IP、HTTP 混合内容或不支持音视频工具结果；此时 Agent 应下载后使用自己的本地文件展示能力，而不是把 App 机器的 `file://` 路径直接贴到另一台机器。

媒体 URL 是仅允许读取对应文件的随机访问链接，最长有效一小时，App 重启后失效；服务关闭期间也不可访问。它不包含完整 MCP 凭证，但持有链接的人可以读取该文件，不应公开分享。过期后重新调用 `asset_get`、`avatar_list`、`voice_list`、`job_result` 或 `media_upload_finish` 取得新链接。列表中的本地路径只用于 App 机器，跨机器使用 `media` 链接或 ID。

## 不管理目录的资源创建

外部 Agent 可以直接按用户意图创建命名资源。接口负责生成或导入、复制到应用管理的资源库、持久化登记；完成后形象和音色直接出现在 App 中。

```json
{"name":"avatar_create","arguments":{"name":"小林","description":"成年女性，黑色短发，正面半身肖像，浅蓝衬衫，简洁背景","requestKey":"avatar-xiaolin-01"}}
```

```json
{"name":"voice_create","arguments":{"name":"温暖讲述者","description":"温暖自然的成年女声，普通话清晰，语速适中","requestKey":"voice-narrator-01"}}
```

两者返回 `data.jobId`。用 `job_get` 查询，完成后 `job_result` 返回 `data.result.avatarId` / `voiceId` 及完整资源元数据。`voice_create` 可省略试听文案，使用内置中文文案；也可传 `text`。参考语音必须满足现有音色库的 3–30 秒限制。

已有作品素材也可以直接入库，无需解析文件路径：

```json
{"name":"avatar_create","arguments":{"name":"小林","source":{"workId":"作品 ID","assetId":"图片素材 ID"},"requestKey":"portrait-import-01"}}
```

`voice_create` 的音频素材导入用相同的 `source`，并提供录音的准确 `transcript`。本机外部文件可传 `path`（输入文件），无需指定输出目录。`description`、`source`、`path`、`uploadId` 四者必须且只能提供一个。`path` 始终指运行 App 的机器；另一台机器的文件应先上传并使用 `uploadId`。

使用已创建形象时，调用 `asset_import`，传 `workId`、`avatarId` 和 `purpose: "target"`，得到作品内 `assetId`；使用音色时，给 `conversation_update` 或 `generation_start` 传 `voiceId`。跨作品引用素材可用 `asset_import.source`。这些流程都不要求 Agent 管理应用目录。

## 接口范围

`tools/list` 是完整参数定义的权威来源。名称、描述、JSON Schema、读写提示和可复制说明统一定义在 `src/McpCatalog.cpp`；业务执行通过 `src/McpService.cpp` 调用现有应用服务。当前有 70 个工具。

| 用途 | 工具 |
| --- | --- |
| 上传与媒体传输 | `media_upload_begin/chunk/status/finish/discard`、`media_read/preview` |
| 状态与诊断 | `app_status`、`app_logs` |
| 作品 | `work_list/create/get/rename/duplicate/remove/restore/save` |
| 对话与素材 | `conversation_update`、`message_reuse/delete`、`asset_import/get/export` |
| 图像、编辑、数字人视频、配音、音效、打包成片 | `generation_start`，通过 `task` 选择 |
| 音色 | `voice_list/create/import/design/update/remove/restore` |
| 形象 | `avatar_list/create/import/design/rename/remove/restore` |
| 音效 | `sfx_list/import/design/remove` |
| 模型 | `model_list/terms/accept_terms/configure/download/verify/remove/clean_partials` |
| 构图 | `template_list/apply`、`generation_settings`、`layout_settings`、`region_add/update/action` |
| 局部编辑 | `edit_settings`、`mask_rectangle/stroke/action`、`edit_history` |
| 时间线打包 | `pack_select/move/arrange` |
| 进度与结果 | `job_list/get/result/cancel` |
| 页面及媒体控制 | `ui_control`，包含显示作品、详情、编辑工具、打包、参数、日志、设置、声音、资源、主题及播放/暂停/跳转 |

音效沿用作品素材存储，通过作品和消息 ID 查询、移除，没有另外创建独立的全局音效库。音色、形象使用现有可恢复移除机制，内置资源只读。作品移除保留生成文件。模型删除仅处理模型目录中登记的文件；先查询删除说明。文件导入不下载任意远程 URL。沙盒版本的外部文件访问仍受用户已授予的系统权限约束。

## 异步约定

- 提交生成、设计、资源创建、模型下载/校验时传 `requestKey`；相同键和相同参数返回原任务，参数不同则返回 `IDEMPOTENCY_CONFLICT`。重启后仍可查询任务及结果。
- 返回 `queued → running → completed / failed / cancelled / interrupted`。建议每秒查询一次 `job_get`。`job_result` 未完成时返回 `NOT_READY`，失败时返回 `JOB_FAILED`。完整错误保留在 `job_get`。
- `stage` 是当前阶段，`stageProgress` 为该阶段 0–1 进度，阶段切换时可能归零；不是整个流程的精确完成比例。`elapsedMs` 是任务执行耗时。
- 共用应用推理引擎串行运行，最多 32 个等待任务；同一作品只接受一个未完成 MCP 生成任务。MCP 保存提交时的输入快照；排队期间不要在 UI 修改该作品的待发送输入，以免执行快照覆盖编辑。修改与运行冲突时返回 `BUSY` / `WORK_BUSY`。
- `job_cancel` 只取消对应的 MCP 任务，不会取消其它 UI 任务。停止接入不等于取消任务。
- 生成前显式检查资源就绪，不会弹出隐式下载流程。用 `model_list` 发现模型组和下载源，然后 `model_download`。音效模型条款须由用户阅读并明确接受；不自动代为同意。
- 以上是普通 MCP 工具实现的持久化任务接口，不宣称实现 MCP 原生 Tasks 扩展。

## 传输与实现

采用 [MCP Streamable HTTP](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports) 的无会话 JSON 响应方式，`POST /mcp`，不提供 GET SSE 流。支持协议协商 `2025-03-26`、`2025-06-18`、`2025-11-25`，包含初始化、工具发现/调用、提示词发现/读取和 ping。

默认仅绑定 `127.0.0.1`；用户开启局域网后监听 IPv4 接口，接收本机及私有/链路本地地址的连接。默认端口 39871，占用时选择可用端口并保存，以面板显示为准。连接配置带 `Authorization: Bearer …`，复制 MCP 地址本身不带凭证。MCP 请求校验认证、Host、Origin（若提供）、请求大小及参数结构。媒体端点使用独立的短期只读访问链接，支持跨源媒体读取；不接受任意文件路径。凭证及任务记录放在应用管理目录，完整 MCP 凭证和上传/下载 base64 内容不输出到调用日志。

`McpHttpServer` 处理传输和认证，`McpCatalog` 管理契约，`McpService` 处理调度和业务，`McpMediaStore` 管理分块上传及短期媒体链接，`McpCallModel` 提供活动记录，`HomeMcp.qml` 显示面板。新增接口时同时补充契约、执行分支和有意义的边界测试。

## 验证

普通桌面构建可运行：

```sh
cmake --build build-mcp --target MossStudio mcp_tests creation_pages_tests -j 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build-mcp -R '^(mcp|creation_pages)$' --output-on-failure
```

测试使用临时输出根目录，覆盖首次关闭与生命周期、真实 HTTP 认证和工具发现、参数验证、资源操作、复制入库与重启、任务幂等/取消/中断恢复、活动列表上限。MCP 测试目标仅用于允许隔离输出目录的非 Store 构建。

运行证据保存在本地 `.local/evidence/mcp-20261023/`，不包含访问凭证：桌面 MCP SDK 实际连接、图像生成、配音生成、8.52 秒时间线视频导出；命名形象和音色的真实设计、按素材 ID 导入、自动入库，以及重启后通过 SDK 重新读取同一资源 ID 和任务结果（重启读取在 offscreen 进程完成）；界面检查覆盖中英文、深浅色、最小窗口、键盘开关/Tab 与复制。模型下载、数字人视频和音效生成不能仅凭这些证据认定已完成全流程验收；需要相应资源与独立运行验证。Store 沙盒发布和 Windows 尚未运行验收。

局域网与传输新增验证：`mcp_tests` 覆盖本机/局域网切换与持久化、认证和 Host/Origin 拒绝、上传重试/校验/入库、上传跨重启续传与错误 SHA-256 拒绝、MCP 图片内容、HTTP Range 和重启后媒体链接失效。独立 SDK 客户端通过本机实际局域网 IP 传输 PNG/WAV/MP4，下载 SHA-256 一致；在浏览器中直接读取这些局域网媒体链接，真实图片解码、7.3 秒音频及 8.52 秒视频的静音播放与跳转均通过。新增局域网面板有中英文、深浅色、普通/最小窗口的 8 组 offscreen 检查，另有中文浅色和英文深色最小窗口两组 Cocoa 实窗检查，包含键盘开关、Tab 和复制提示词。证据位于 `.local/evidence/mcp-lan-20261023/`（`transfer-report.json`、`browser-playback.json/jpg`、`ui/`、`native-ui/`）。这证明服务和媒体传输实现；两台物理机器之间的防火墙、网络隔离，以及具体第三方聊天软件的显示/播放效果仍需在目标环境验收。
