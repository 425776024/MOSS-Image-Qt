#include "McpService.h"
#include "McpCatalog.h"
#include "Studio.h"
#include "UserSelectedFileExport.h"
#include "VoiceStyle.h"
#include "WorkspaceLibrary.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopeGuard>
#include <QUuid>

namespace {
QJsonObject ok(QJsonObject data = {}) { return {{"ok", true}, {"data", data}}; }
QJsonObject failure(QString code, QString message) {
  return {{"ok", false},
          {"error", QJsonObject{{"code", code}, {"message", message}}}};
}
QString uid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
bool terminal(QString status) {
  return status == "completed" || status == "failed" || status == "cancelled" ||
         status == "interrupted";
}
QString local(QString path) {
  return path.startsWith("file:") ? QUrl(path).toLocalFile() : path;
}
QJsonObject read(QString path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly)
             ? QJsonDocument::fromJson(f.readAll()).object()
             : QJsonObject();
}
bool write(QString path, QJsonObject value) {
  QSaveFile file(path);
  const auto bytes = QJsonDocument(value).toJson();
  if (!file.open(QIODevice::WriteOnly))
    return false;
  file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  return file.write(bytes) == bytes.size() && file.commit();
}
QString jsonText(QJsonValue value) {
  QString text = QString::fromUtf8(
      value.isArray() ? QJsonDocument(value.toArray()).toJson()
                      : QJsonDocument(value.toObject()).toJson());
  if (text.size() > 24000)
    text = text.left(24000) +
           "\n… (display truncated; use the result tools for full data)";
  return text;
}
QJsonValue redacted(QJsonValue value) {
  if (value.isArray()) {
    QJsonArray a;
    for (const auto &v : value.toArray())
      a.append(redacted(v));
    return a;
  }
  if (value.isObject()) {
    auto o = value.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
      const auto k = it.key().toLower();
      it.value() = (k.contains("token") || k.contains("authorization") ||
                    k.contains("password") || k == "mask" || k == "database64")
                       ? QJsonValue("[redacted]")
                       : redacted(it.value());
    }
    return o;
  }
  return value;
}
QJsonObject toolResult(QJsonObject result) {
  return {
      {"isError", !result["ok"].toBool()},
      {"structuredContent", result},
      {"content", QJsonArray{QJsonObject{
                      {"type", "text"},
                      {"text", QString::fromUtf8(QJsonDocument(result).toJson(
                                   QJsonDocument::Compact))}}}}};
}
QString checkDestination(const QJsonObject &a) {
  const auto path = a["path"].toString();
  if (!QDir::isAbsolutePath(path))
    return "Use an absolute local path";
  if (QFileInfo::exists(path) && !a["overwrite"].toBool())
    return "Destination exists; pass overwrite=true to replace it";
  if (!QFileInfo(QFileInfo(path).absolutePath()).isDir())
    return "Destination directory does not exist";
  return {};
}
} // namespace
McpService::McpService(Studio *studio, WorkspaceLibrary *workspace,
                       QObject *parent)
    : QObject(parent), m_studio(studio), m_workspace(workspace), m_http(this),
      m_calls(this), m_root(studio->outputRoot() + "/mcp") {
  const auto config = read(m_root + "/config.json");
  m_enabled = config["enabled"].toBool();
  m_lanEnabled = config["lanEnabled"].toBool();
  m_lanAddress = config["lanAddress"].toString();
  refreshAddresses();
  m_media = std::make_unique<McpMediaStore>(m_root + "/uploads");
  m_http.mediaPath = [this](const QString &token) {
    return m_media->resolveLink(token);
  };
  m_token = config["token"].toString();
  m_port = config["port"].toInt(39871);
  if (m_port < 1024 || m_port > 65535)
    m_port = 39871;
  m_http.dispatch = [this](const QJsonObject &request) {
    return dispatch(request);
  };
  connect(&m_calls, &McpCallModel::entryChanged, this,
          &McpService::callChanged);
  connect(studio, &Studio::jobFinished, this, [this](bool success) {
    if (!m_active.isEmpty() &&
        !m_jobs[m_active]["kind"].toString().startsWith("model_"))
      finishJob(success);
  });
  connect(studio->models(), &ModelManager::completed, this,
          [this](bool success) {
            if (!m_active.isEmpty() &&
                m_jobs[m_active]["kind"].toString().startsWith("model_"))
              finishJob(success);
          });
  m_tick.setInterval(500);
  connect(&m_tick, &QTimer::timeout, this, [this] {
    updateProgress();
    runNext();
  });
  m_tick.start();
  m_networkTick.setInterval(5000);
  connect(&m_networkTick, &QTimer::timeout, this,
          &McpService::refreshAddresses);
  m_networkTick.start();
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
          &McpService::shutdown);
  if (m_enabled)
    setEnabled(true);
}
McpService::~McpService() { shutdown(); }
QString McpService::url() const {
  return running()
             ? QString("http://%1:%2/mcp")
                   .arg(m_lanEnabled && !m_lanAddress.isEmpty() ? m_lanAddress
                                                                : "127.0.0.1")
                   .arg(m_http.port())
             : QString();
}
void McpService::refreshAddresses() {
  const auto addresses = McpHttpServer::lanAddresses();
  const auto selected =
      addresses.contains(m_lanAddress) ? m_lanAddress : addresses.value(0);
  if (addresses == m_lanAddresses && selected == m_lanAddress)
    return;
  m_lanAddresses = addresses;
  m_lanAddress = selected;
  emit changed();
}
void McpService::setLanAddress(QString value) {
  if (!m_lanAddresses.contains(value) || value == m_lanAddress || m_stopping)
    return;
  if (!prepareStorage()) {
    emit changed();
    return;
  }
  m_lanAddress = value;
  if (!saveConfig())
    m_error = "无法保存 MCP 设置";
  emit changed();
}
void McpService::setLanEnabled(bool value) {
  if (value == m_lanEnabled || m_stopping)
    return;
  m_error.clear();
  if (!prepareStorage()) {
    emit changed();
    return;
  }
  const bool wasRunning = running(), previous = m_lanEnabled;
  m_lanEnabled = value;
  if (wasRunning) {
    m_http.stop();
    setEnabled(true);
    if (!running()) {
      m_lanEnabled = previous;
      setEnabled(true);
      m_error = "无法切换连接范围，已尝试恢复原设置";
    }
  } else if (!saveConfig()) {
    m_lanEnabled = previous;
    m_error = "无法保存 MCP 设置";
  }
  emit changed();
}
int McpService::toolCount() const { return McpCatalog::tools().size(); }
bool McpService::saveConfig() {
  return write(m_root + "/config.json", {{"enabled", m_enabled},
                                         {"port", m_port},
                                         {"token", m_token},
                                         {"lanEnabled", m_lanEnabled},
                                         {"lanAddress", m_lanAddress}});
}
bool McpService::prepareStorage() {
  if (!QDir().mkpath(m_root + "/jobs")) {
    m_error = "无法创建 MCP 数据目录";
    return false;
  }
  QFile::setPermissions(m_root,
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  if (!m_lock) {
    auto lock = std::make_unique<QLockFile>(m_root + "/service.lock");
    if (!lock->tryLock()) {
      m_error = "另一个应用窗口正在管理 MCP，请使用已开启的窗口";
      return false;
    }
    m_lock = std::move(lock);
    loadJobs();
  }
  return true;
}
void McpService::setEnabled(bool enabled) {
  if (m_stopping)
    return;
  m_error.clear();
  if (!enabled) {
    if (!prepareStorage()) {
      emit changed();
      return;
    }
    m_http.stop();
    m_enabled = false;
    if (!saveConfig())
      m_error = "无法保存 MCP 设置";
    emit changed();
    return;
  }
  if (running())
    return;
  if (!prepareStorage()) {
    emit changed();
    return;
  }
  if (m_token.isEmpty())
    m_token = uid().remove('-') + uid().remove('-');
  if (!m_http.start(quint16(m_port), m_token, m_lanEnabled) &&
      !m_http.start(0, m_token, m_lanEnabled)) {
    m_error = "无法开启 MCP：" + m_http.error();
    emit changed();
    return;
  }
  m_port = m_http.port();
  m_enabled = true;
  if (!saveConfig()) {
    m_enabled = false;
    m_http.stop();
    m_error = "无法保存 MCP 设置，请检查目录权限";
  }
  emit changed();
}
QString McpService::configuration() const {
  if (!running())
    return {};
  return jsonText(QJsonObject{
      {"mcpServers",
       QJsonObject{
           {"xinshen",
            QJsonObject{{"url", url()},
                        {"headers", QJsonObject{{"Authorization",
                                                 "Bearer " + m_token}}}}}}}});
}
QString McpService::connectionText(bool english) const {
  if (!running())
    return {};
  const auto intro =
      english
          ? QString("Connect to XAvatar using Streamable HTTP and keep the app "
                    "open. %1 Add this MCP configuration (including its access "
                    "credential):\n\n")
                .arg(m_lanEnabled
                         ? "This is a LAN endpoint; use it only on a trusted "
                           "network."
                         : "This endpoint works only on the app's computer.")
          : QString("请连接新身数字人的 MCP 服务，传输方式为 Streamable "
                    "HTTP，调用期间保持应用打开。%1请添加以下 MCP "
                    "配置（内含访问凭证）：\n\n")
                .arg(m_lanEnabled ? "此地址供可信局域网内的 Agent 使用。"
                                  : "此地址仅限本机使用。");
  return intro + configuration() +
         (english
              ? QString(
                    "\nDiscover tools first. Use app_status and work_list; "
                    "pass explicit workId. Read schemas before editing. "
                    "For a named portrait or voice, prefer avatar_create "
                    "or voice_create with a description or source asset ID; "
                    "these generate/import and persist in the app library "
                    "automatically, without specifying output directories. "
                    "To send files from the agent machine, use "
                    "media_upload_begin, "
                    "media_upload_chunk with base64 chunks up to 1 MiB, then "
                    "media_upload_finish. "
                    "Pass uploadId to asset_import/avatar_create/voice_create. "
                    "Resume from the received offset returned by "
                    "media_upload_status; discard staging with "
                    "media_upload_discard after successful import. "
                    "All path fields refer to the app machine, never the agent "
                    "machine. "
                    "Results include media URLs valid for one hour; use "
                    "media_preview for "
                    "inline images or media_read for base64 download chunks. "
                    "Display/play "
                    "the link if supported, otherwise download and use the "
                    "agent's local "
                    "media renderer. Never claim a media preview was shown "
                    "without client support. "
                    "For generation or resource downloads, keep the "
                    "returned jobId, poll job_get about once per second "
                    "and use job_result after completion. Reuse requestKey "
                    "when retrying an identical submission. Ask the user "
                    "before deleting resources or accepting model terms. "
                    "Do not put this credential in shared documents.\n")
              : QString(
                    "\n先发现工具，再用 app_status 和 work_list "
                    "查询状态及作品，操作时明确传入 "
                    "workId，并按接口参数说明调用。创建命名形象或音色时优先用 "
                    "avatar_create / voice_create，传入描述或已有素材 ID；"
                    "接口会生成或导入并自动保存到应用资源库，无需指定保存目录。"
                    "从 Agent 机器上传文件时用 "
                    "media_upload_begin、media_upload_chunk"
                    "（每块不超过 1 MiB 原始数据的 "
                    "base64）、media_upload_finish，"
                    "然后向 asset_import/avatar_create/voice_create 传 "
                    "uploadId。"
                    "中断后用 media_upload_status 查询 received "
                    "字节偏移继续上传；"
                    "入库完成后用 media_upload_discard 清理暂存。"
                    "所有 path 都指 App 机器的路径，不是 Agent 机器的路径。"
                    "结果附带一小时有效的媒体链接；图片可用 media_preview "
                    "内联预览，"
                    "也可用 media_read "
                    "分块下载。支持的客户端可直接展示、播放链接；"
                    "否则先下载到 Agent "
                    "机器，再用其本地媒体展示能力呈现，不能假称已显示或播放。"
                    "生成或下载会返回 "
                    "jobId，每秒左右用 job_get 查询一次，完成后用 "
                    "job_result 获取结果。同一提交重试时复用 "
                    "requestKey，避免重复执行。删除资源或接受模型条款前，请"
                    "确认用户意图。不要把此访问凭证写入公开文档。\n"));
}
QString McpService::toolReference() const { return McpCatalog::reference(); }
void McpService::shutdown() {
  if (m_stopping)
    return;
  m_stopping = true;
  m_tick.stop();
  m_networkTick.stop();
  m_http.stop();
  for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
    if (!terminal(it.value()["status"].toString())) {
      it.value()["status"] = "interrupted";
      it.value()["error"] = "Application closed before the job completed";
      it.value()["finishedAt"] = now();
      saveJob(it.key());
    }
  m_lock.reset();
}
void McpService::loadJobs() {
  if (!m_jobs.isEmpty())
    return;
  QDir dir(m_root + "/jobs");
  for (const auto &file : dir.entryList({"*.json"}, QDir::Files)) {
    auto job = read(dir.filePath(file));
    const auto id = job["jobId"].toString();
    if (id.isEmpty())
      continue;
    m_jobs[id] = job;
    if (!terminal(job["status"].toString())) {
      m_jobs[id]["status"] = "interrupted";
      m_jobs[id]["error"] =
          "Previous application session ended before completion";
      m_jobs[id]["finishedAt"] = now();
      saveJob(id);
    }
  }
}
bool McpService::saveJob(QString id) {
  const bool saved = write(m_root + "/jobs/" + id + ".json", m_jobs[id]);
  if (!saved) {
    m_error = "无法保存 MCP 任务记录";
    emit changed();
  }
  return saved;
}
QVariantMap McpService::page(QString id) const {
  for (const auto &value : m_studio->creationPages())
    if (value.toMap()["id"] == id)
      return value.toMap();
  return {};
}
bool McpService::reserved(QString workId) const {
  for (const auto &job : m_jobs)
    if (job["workId"] == workId && !terminal(job["status"].toString()))
      return true;
  return false;
}
bool McpService::anyJobs() const {
  for (const auto &job : m_jobs)
    if (!terminal(job["status"].toString()))
      return true;
  return false;
}
QJsonObject McpService::publicJob(QString id) const {
  auto job = m_jobs.value(id);
  job.remove("snapshot");
  job.remove("arguments");
  job.remove("fingerprint");
  job.remove("callId");
  return job;
}
QString McpService::mediaSource(const QJsonObject &a) const {
  if (int(a.contains("uploadId")) + int(a.contains("source")) +
          int(a.contains("avatarId")) + int(a.contains("voiceId")) !=
      1)
    return {};
  if (a.contains("uploadId"))
    return m_media->uploadedPath(a["uploadId"].toString());
  if (a.contains("source")) {
    const auto source = a["source"].toObject();
    const auto work = source["workId"].toString();
    auto thread =
        page(work).isEmpty() ? nullptr : m_studio->conversationFor(work);
    return thread ? local(thread->asset(source["assetId"].toString())["url"]
                              .toString())
                  : QString();
  }
  if (a.contains("avatarId"))
    for (const auto &row :
         m_workspace->avatars() + m_workspace->builtinAvatars())
      if (row.toMap()["id"] == a["avatarId"].toString())
        return local(row.toMap()["url"].toString());
  if (a.contains("voiceId"))
    for (const auto &row : m_studio->sound()->voices())
      if (row.toMap()["id"] == a["voiceId"].toString())
        return row.toMap()["reference"].toString();
  return {};
}
QJsonValue McpService::withMediaLinks(QJsonValue value) {
  if (value.isArray()) {
    QJsonArray rows;
    for (const auto &row : value.toArray())
      rows.append(withMediaLinks(row));
    return rows;
  }
  if (!value.isObject())
    return value;
  auto object = value.toObject();
  for (auto it = object.begin(); it != object.end(); ++it)
    it.value() = withMediaLinks(it.value());
  QString path;
  const QUrl source(object["url"].toString());
  if (source.isLocalFile())
    path = source.toLocalFile();
  else if (object.contains("reference") && object.contains("transcript"))
    path = object["reference"].toString();
  if (!path.isEmpty()) {
    const auto media = m_media->describe(path, url());
    if (!media.isEmpty())
      object["media"] = media;
  }
  return object;
}
QJsonObject McpService::dispatch(const QJsonObject &request) {
  const auto id = request.value("id");
  const auto method = request["method"].toString();
  auto rpcError = [&](int code, QString message) {
    return QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
        {"error", QJsonObject{{"code", code}, {"message", message}}}};
  };
  if (request["jsonrpc"] != "2.0" || method.isEmpty() ||
      (!id.isUndefined() && !id.isString() && !id.isDouble()) ||
      (request.contains("params") && !request["params"].isObject()))
    return rpcError(-32600, "Invalid JSON-RPC request");
  if (id.isUndefined())
    return {}; // Notifications never execute tools.
  const auto params = request["params"].toObject();
  const auto name = method == "tools/call" ? params["name"].toString() : method;
  const auto callId = m_calls.append(
      {{"name", name},
       {"argumentsText",
        jsonText(redacted(method == "tools/call" ? params["arguments"]
                                                 : QJsonValue(params)))}});
  m_callId = callId;
  QJsonObject result;
  auto closeCall = qScopeGuard([&] { m_callId.clear(); });
  if (method == "initialize") {
    auto version = params["protocolVersion"].toString();
    if (!QStringList{"2025-03-26", "2025-06-18", "2025-11-25"}.contains(
            version))
      version = "2025-11-25";
    result = {
        {"protocolVersion", version},
        {"capabilities",
         QJsonObject{{"tools", QJsonObject{{"listChanged", false}}},
                     {"prompts", QJsonObject{{"listChanged", false}}}}},
        {"serverInfo",
         QJsonObject{{"name", "xinshen"},
                     {"title", "新身数字人 / XAvatar"},
                     {"version", QCoreApplication::applicationVersion()}}},
        {"instructions",
         "Local creative app. Use explicit workId and idempotency requestKey. "
         "Prefer avatar_create/voice_create for named resources: generation or "
         "import includes persistent app-library storage automatically. "
         "Remote files use media_upload_begin/chunk/finish, then uploadId. "
         "Local paths refer to this app machine. Use media_preview for inline "
         "images, media URLs for streaming or media_read to download bytes. "
         "Generation/downloads return jobId; poll job_get, then job_result. No "
         "automatic model downloads or license acceptance."}};
  } else if (method == "ping")
    result = {};
  else if (method == "tools/list")
    result = {{"tools", McpCatalog::tools()}};
  else if (method == "prompts/list")
    result = {{"prompts",
               QJsonArray{QJsonObject{{"name", "create_with_xinshen"},
                                      {"description",
                                       "Work, resource management and "
                                       "asynchronous generation workflow"}}}}};
  else if (method == "prompts/get" && params["name"] == "create_with_xinshen")
    result = {
        {"messages",
         QJsonArray{QJsonObject{
             {"role", "user"},
             {"content",
              QJsonObject{
                  {"type", "text"},
                  {"text",
                   "Use app_status, work_list and the tool schemas. Create or "
                   "select a work, import/select media and submit "
                   "generation_start with a unique requestKey. Poll job_get "
                   "and fetch job_result. Models, voices, avatars and sound "
                   "effects also have management tools. Prefer avatar_create "
                   "and voice_create for named resources; they generate or "
                   "import and save to the library automatically. Respect "
                   "cross-machine paths: upload remote files with "
                   "media_upload_begin/chunk/finish and use uploadId. Query "
                   "media_upload_status to resume. Results carry read-only "
                   "media links for download or playback; media_preview "
                   "returns inline images and media_read returns download "
                   "chunks. Use the agent client's media renderer. Respect "
                   "model terms "
                   "and explicit deletion intent."}}}}}}};
  else if (method == "tools/call") {
    const auto spec = McpCatalog::tool(name);
    if (spec.isEmpty()) {
      m_calls.update(callId,
                     {{"state", "failed"}, {"resultText", "Unknown tool"}});
      return rpcError(-32602, "Unknown tool: " + name);
    }
    const auto arguments = params.value("arguments").isUndefined()
                               ? QJsonValue(QJsonObject())
                               : params["arguments"];
    const auto invalid =
        McpCatalog::validate(arguments, spec["inputSchema"].toObject());
    auto response = invalid.isEmpty() ? execute(name, arguments.toObject())
                                      : failure("INVALID_ARGUMENT", invalid);
    QJsonObject preview;
    if (name == "media_preview" && response["ok"].toBool()) {
      preview = response["data"].toObject();
      auto data = preview;
      data.remove("dataBase64");
      response["data"] = data;
    }
    response = withMediaLinks(response).toObject();
    result = toolResult(response);
    if (!preview.isEmpty()) {
      auto content = result["content"].toArray();
      content.append(QJsonObject{{"type", "image"},
                                 {"mimeType", preview["mimeType"]},
                                 {"data", preview["dataBase64"]}});
      result["content"] = content;
    }
    const auto jobId = response["data"].toObject()["jobId"].toString();
    const bool submitted =
        !jobId.isEmpty() &&
        QStringList{"generation_start", "sfx_design",   "voice_design",
                    "avatar_design",    "voice_create", "avatar_create",
                    "model_download",   "model_verify"}
            .contains(name);
    m_calls.update(
        callId,
        {{"state", response["ok"].toBool()
                       ? (!submitted ? "completed"
                                     : publicJob(jobId)["status"].toString())
                       : "failed"},
         {"jobId", jobId},
         {"resultText", jsonText(redacted(response))},
         {"elapsedMs", QDateTime::currentMSecsSinceEpoch() -
                           m_calls.get(callId)["startedMs"].toLongLong()}});
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
  } else {
    m_calls.update(callId,
                   {{"state", "failed"}, {"resultText", "Method not found"}});
    return rpcError(-32601, "Method not found");
  }
  m_calls.update(callId, {{"state", "completed"},
                          {"resultText", jsonText(redacted(result))},
                          {"elapsedMs",
                           QDateTime::currentMSecsSinceEpoch() -
                               m_calls.get(callId)["startedMs"].toLongLong()}});
  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
QString McpService::updateConversation(ConversationDocument *thread,
                                       const QJsonObject &a) {
  if (a.contains("targetId") && !a["targetId"].toString().isEmpty() &&
      thread->asset(a["targetId"].toString())["kind"] != "image")
    return "targetId is not an image in this work";
  if (a.contains("audioId") && !a["audioId"].toString().isEmpty() &&
      thread->asset(a["audioId"].toString())["kind"] != "audio")
    return "audioId is not audio in this work";
  if (a.contains("voiceId")) {
    bool found = false;
    for (const auto &v : m_studio->sound()->voices())
      found |= v.toMap()["id"] == a["voiceId"].toString();
    if (!found)
      return "Unknown voiceId";
  }
  if (a.contains("task"))
    thread->setTask(a["task"].toString());
  if (a.contains("prompt"))
    thread->setDraft(a["prompt"].toString());
  if (a.contains("targetId"))
    thread->chooseTarget(a["targetId"].toString());
  if (a.contains("audioId"))
    thread->chooseAudio(a["audioId"].toString());
  if (a.contains("voiceId"))
    thread->setVoiceId(a["voiceId"].toString());
  if (a.contains("voiceOptions"))
    thread->setVoiceOptions(a["voiceOptions"].toObject().toVariantMap());
  if (a.contains("editScope"))
    thread->setEditScope(a["editScope"].toString());
  if (a.contains("sfxDuration"))
    thread->setSfxDuration(a["sfxDuration"].toInt());
  return {};
}
QJsonObject McpService::execute(QString name, const QJsonObject &a) {
  auto models = m_studio->models();
  auto sound = m_studio->sound();
  const auto busy = [&] {
    return failure("BUSY", "Another generation/download or queued MCP job owns "
                           "these resources; query job_get or retry later.");
  };
  const auto fromError = [](QString error, QJsonObject data = QJsonObject()) {
    return error.isEmpty() ? ok(data) : failure("OPERATION_FAILED", error);
  };
  if (name == "media_upload_begin")
    return m_media->begin(a);
  if (name == "media_upload_chunk")
    return m_media->chunk(a);
  if (name == "media_upload_status")
    return m_media->status(a["uploadId"].toString());
  if (name == "media_upload_finish") {
    auto result = m_media->finish(a["uploadId"].toString());
    if (result["ok"].toBool()) {
      auto data = result["data"].toObject();
      data["media"] = m_media->describe(
          m_media->uploadedPath(a["uploadId"].toString()), url());
      result["data"] = data;
    }
    return result;
  }
  if (name == "media_upload_discard") {
    for (const auto &job : m_jobs)
      if (!terminal(job["status"].toString()) &&
          job["arguments"].toObject()["uploadId"] == a["uploadId"])
        return busy();
    return m_media->discard(a["uploadId"].toString());
  }
  if (name == "media_read" || name == "media_preview") {
    const auto path = mediaSource(a);
    if (path.isEmpty())
      return failure(
          "NOT_FOUND",
          "Select exactly one valid source, uploadId, avatarId or voiceId");
    if (name == "media_preview") {
      auto result = m_media->preview(path);
      if (result["ok"].toBool()) {
        auto data = result["data"].toObject();
        data["media"] = m_media->describe(path, url());
        result["data"] = data;
      }
      return result;
    }
    return m_media->readChunk(path, a["offset"].toInteger(),
                              a["maxBytes"].toInt(1024 * 1024));
  }
  if (name == "app_status")
    return ok({{"busy", m_studio->busy()},
               {"endpoint", url()},
               {"lanEnabled", m_lanEnabled},
               {"status", m_studio->status()},
               {"activeJobId", m_active},
               {"queued", m_queue.size()},
               {"readiness", QJsonObject{{"image", m_studio->imageReady()},
                                         {"voice", m_studio->voiceReady()},
                                         {"avatar", m_studio->dittoReady()},
                                         {"sfx", m_studio->sfxReady()}}},
               {"tools", toolCount()},
               {"protocolVersions",
                QJsonArray{"2025-03-26", "2025-06-18", "2025-11-25"}}});
  if (name == "work_list") {
    QJsonArray rows;
    for (const auto &v : m_studio->creationPages()) {
      const auto p = v.toMap();
      rows.append(QJsonObject{{"workId", p["id"].toString()},
                              {"name", p["name"].toString()},
                              {"created", p["created"].toString()},
                              {"modified", p["modified"].toString()}});
    }
    return ok({{"works", rows}});
  }
  if (name == "voice_list")
    return ok(
        {{"voices", QJsonArray::fromVariantList(sound->voices())},
         {"emotions", QJsonArray::fromStringList(VoiceStyle::emotions())},
         {"dialects", QJsonArray::fromStringList(VoiceStyle::dialects())}});
  if (name == "avatar_list")
    return ok({{"avatars", QJsonArray::fromVariantList(m_workspace->avatars())},
               {"builtins",
                QJsonArray::fromVariantList(m_workspace->builtinAvatars())}});
  if (name == "template_list")
    return ok({{"templates",
                QJsonArray::fromVariantList(m_studio->layoutTemplates())}});
  if (name == "app_logs")
    return ok({{"log", m_studio->log()},
               {"status", m_studio->status()},
               {"error", m_studio->error()}});
  if (name == "model_list") {
    QJsonArray groups;
    for (const auto &v : models->groups()) {
      auto row = QJsonObject::fromVariantMap(v.toMap());
      row["deletionDescription"] =
          models->deletionDescription(row["id"].toString());
      groups.append(row);
    }
    return ok({{"groups", groups},
               {"resources", QJsonArray::fromVariantList(models->resources())},
               {"root", models->root()},
               {"rootSelectable", models->rootSelectable()},
               {"sources", QJsonArray::fromStringList(models->sourceValues())},
               {"source", models->endpoint()},
               {"imageQuant", models->imageQuant()},
               {"busy", models->busy()},
               {"progress", models->progress()},
               {"status", models->status()},
               {"error", models->error()},
               {"partialBytes", models->partialBytes()},
               {"sfxTermsAccepted", models->sfxTermsAccepted()},
               {"termsVersion", "2026-10-22"}});
  }
  if (name == "model_terms")
    return ok({{"version", "2026-10-22"},
               {"text", models->modelTerms(a["english"].toBool())},
               {"accepted", models->sfxTermsAccepted()}});
  if (name == "model_accept_terms") {
    if (!a["userAccepted"].toBool())
      return failure(
          "CONSENT_REQUIRED",
          "The human user must explicitly accept the displayed terms.");
    models->acceptSfxTerms();
    return ok({{"accepted", models->sfxTermsAccepted()}});
  }
  if (name == "job_list") {
    QList<QJsonObject> rows;
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
      if (!a.contains("workId") || it.value()["workId"] == a["workId"])
        rows.append(publicJob(it.key()));
    std::sort(rows.begin(), rows.end(), [](const auto &x, const auto &y) {
      return x["createdAt"].toString() > y["createdAt"].toString();
    });
    QJsonArray out;
    for (int i = 0; i < std::min(100, int(rows.size())); ++i)
      out.append(rows[i]);
    return ok({{"jobs", out}});
  }
  if (name == "job_get" || name == "job_result" || name == "job_cancel") {
    const auto id = a["jobId"].toString();
    if (!m_jobs.contains(id))
      return failure("NOT_FOUND", "Unknown jobId");
    auto &job = m_jobs[id];
    if (name == "job_cancel" && !terminal(job["status"].toString())) {
      job["cancelRequested"] = true;
      if (id == m_active) {
        job["status"] = "cancelling";
        saveJob(id);
        m_studio->cancel();
      } else {
        m_queue.removeAll(id);
        job["status"] = "cancelled";
        job["finishedAt"] = now();
        saveJob(id);
        m_calls.update(
            job["callId"].toString(),
            {{"state", "cancelled"}, {"resultText", jsonText(publicJob(id))}});
      }
    }
    if (name == "job_result" && job["status"] != "completed")
      return failure(terminal(job["status"].toString()) ? "JOB_FAILED"
                                                        : "NOT_READY",
                     job["error"].toString(job["status"].toString()));
    return ok(publicJob(id));
  }
  if (name == "voice_create" || name == "avatar_create") {
    const int sources = int(a.contains("description")) +
                        int(a.contains("source")) + int(a.contains("path")) +
                        int(a.contains("uploadId"));
    if (sources != 1)
      return failure(
          "INVALID_ARGUMENT",
          "Provide exactly one of description, source, path or uploadId");
    auto args = a;
    const bool design = a.contains("description");
    if (name == "voice_create") {
      if (design && a.contains("transcript"))
        return failure(
            "INVALID_ARGUMENT",
            "Use text for generation; transcript is for imported audio");
      if (!design && (a.contains("text") ||
                      a["transcript"].toString().trimmed().isEmpty()))
        return failure(
            "INVALID_ARGUMENT",
            "Imported voices require transcript, not generation text");
      if (design && !a.contains("text"))
        args["text"] = "你好，很高兴认识你。愿今天的声音带来温暖与灵感，让我们"
                       "一起记录生活中的美好时刻。";
    }
    return enqueue(name.section('_', 0, 0) + (design ? "_design" : "_import"),
                   args);
  }
  if (name == "generation_start" || name == "sfx_design" ||
      name == "voice_design" || name == "avatar_design" ||
      name == "model_download" || name == "model_verify") {
    auto args = a;
    if (name == "sfx_design")
      args["task"] = "sfx";
    return enqueue(name == "sfx_design" ? "generation_start" : name, args);
  }
  if (name == "sfx_list") {
    QJsonArray items;
    for (const auto &v : m_studio->creationPages()) {
      const auto id = v.toMap()["id"].toString();
      if (a.contains("workId") && id != a["workId"].toString())
        continue;
      auto thread = m_studio->conversationFor(id);
      if (!thread)
        continue;
      for (const auto &m : thread->messages()) {
        const auto row = m.toMap();
        if (row["task"] != "sfx" || row["role"] != "assistant")
          continue;
        for (const auto &assetId : row["assetIds"].toStringList()) {
          auto asset = QJsonObject::fromVariantMap(thread->asset(assetId));
          asset["workId"] = id;
          asset["messageId"] = row["id"].toString();
          items.append(asset);
        }
      }
    }
    return ok({{"effects", items}});
  }
  if (name == "ui_control") {
    if (a["action"] == "theme") {
      if (!a.contains("theme"))
        return failure("INVALID_ARGUMENT", "theme is required");
      m_workspace->setAppearanceMode(a["theme"].toString());
      return ok();
    }
    return uiControl
               ? uiControl(a)
               : failure("UI_UNAVAILABLE", "No application window is attached");
  }
  if (name.startsWith("model_") || name.startsWith("voice_") ||
      name.startsWith("avatar_")) {
    if (m_studio->busy() || anyJobs())
      return busy();
    QString error;
    if (name == "model_configure") {
      if (a.contains("source") &&
          !models->sourceValues().contains(a["source"].toString()))
        return failure("INVALID_ARGUMENT", "Unknown model source");
      if (a.contains("root") && (!models->rootSelectable() ||
                                 !QDir::isAbsolutePath(a["root"].toString())))
        return failure("INVALID_ARGUMENT",
                       "Model root cannot be changed or is not absolute");
      if (a.contains("source"))
        models->setEndpoint(a["source"].toString());
      if (a.contains("imageQuant"))
        m_studio->setImageQuant(a["imageQuant"].toString());
      if (a.contains("root"))
        m_studio->setModelRoot(QUrl::fromLocalFile(a["root"].toString()));
      return execute("model_list", {});
    }
    if (name == "model_remove") {
      bool valid = false;
      for (const auto &v : models->groups())
        valid |= v.toMap()["id"] == a["group"].toString();
      if (!valid)
        return failure("NOT_FOUND", "Unknown model group");
      models->removeGroup(a["group"].toString());
      return fromError(models->error());
    }
    if (name == "model_clean_partials") {
      models->cleanPartials();
      return fromError(models->error());
    }
    if (name == "voice_import") {
      if (int(a.contains("path")) + int(a.contains("uploadId")) != 1)
        return failure("INVALID_ARGUMENT", "Provide path or uploadId");
      const auto source = a.contains("uploadId")
                              ? m_media->uploadedPath(a["uploadId"].toString())
                              : a["path"].toString();
      if (!QDir::isAbsolutePath(source))
        return failure("INVALID_ARGUMENT", "Use an absolute local path");
      error =
          sound->createVoice(a["name"].toString(), QUrl::fromLocalFile(source),
                             a["transcript"].toString());
      return fromError(
          error,
          {{"voice", QJsonObject::fromVariantMap(sound->selectedVoice())}});
    }
    if (name == "voice_update")
      error = sound->updateVoice(a["voiceId"].toString(), a["name"].toString(),
                                 a["transcript"].toString());
    else if (name == "voice_remove")
      error = sound->removeVoice(a["voiceId"].toString());
    else if (name == "voice_restore")
      error = sound->restoreVoice();
    else if (name == "avatar_import") {
      if (int(a.contains("path")) + int(a.contains("uploadId")) != 1)
        return failure("INVALID_ARGUMENT", "Provide path or uploadId");
      const auto source = a.contains("uploadId")
                              ? m_media->uploadedPath(a["uploadId"].toString())
                              : a["path"].toString();
      if (!QDir::isAbsolutePath(source))
        return failure("INVALID_ARGUMENT", "Use an absolute local path");
      error = m_workspace->importAvatar(QUrl::fromLocalFile(source),
                                        a["name"].toString());
      return fromError(
          error,
          {{"avatars", QJsonArray::fromVariantList(m_workspace->avatars())}});
    } else if (name == "avatar_rename")
      error = m_workspace->renameAvatar(a["avatarId"].toString(),
                                        a["name"].toString());
    else if (name == "avatar_remove")
      error = m_workspace->removeAvatar(a["avatarId"].toString());
    else if (name == "avatar_restore")
      error = m_workspace->restoreAvatar();
    else
      return failure("NOT_IMPLEMENTED", name);
    return fromError(error);
  }
  if (name == "work_create") {
    if (m_studio->busy())
      return busy();
    m_studio->newConversation();
    const auto id = m_studio->conversation()->id();
    if (a.contains("name")) {
      for (int i = 0; i < m_studio->creationPages().size(); ++i)
        if (m_studio->creationPages()[i].toMap()["id"] == id)
          m_studio->renamePage(i, a["name"].toString());
    }
    return ok({{"workId", id}});
  }
  if (name == "work_restore") {
    if (m_studio->busy() || anyJobs())
      return busy();
    if (!m_studio->canRestorePage())
      return failure("NOT_FOUND", "No removed work to restore");
    m_studio->restoreDeletedPage();
    return execute("work_list", {});
  }
  const auto workId = a["workId"].toString();
  auto thread = m_studio->conversationFor(workId);
  if (!thread || page(workId).isEmpty())
    return failure("NOT_FOUND", "Unknown workId");
  if (name == "work_get") {
    m_studio->flushWorkspace();
    auto p = QJsonObject::fromVariantMap(page(workId));
    auto edit = p["edit"].toObject();
    edit.remove("mask");
    p["edit"] = edit;
    p["conversation"] = thread->json();
    p["clips"] = QJsonArray::fromVariantList(thread->clips());
    p["workId"] = workId;
    return ok(p);
  }
  const auto assetId = a["assetId"].toString();
  if (name == "asset_get" || name == "asset_export") {
    auto asset = thread->asset(assetId);
    if (asset.isEmpty())
      return failure("NOT_FOUND", "Unknown assetId");
    if (name == "asset_get") {
      asset["exists"] = QFileInfo::exists(local(asset["url"].toString()));
      return ok({{"asset", QJsonObject::fromVariantMap(asset)}});
    }
    const auto invalid = checkDestination(a);
    if (!invalid.isEmpty())
      return failure("INVALID_DESTINATION", invalid);
    QString error;
    copyToUserSelectedFile(local(asset["url"].toString()), a["path"].toString(),
                           &error);
    return fromError(error, {{"path", a["path"]}});
  }
  if (m_studio->busy() || reserved(workId))
    return busy();
  m_studio->openConversation(workId);
  thread->dismissError();
  m_studio->dismissError();
  if (name == "work_rename" || name == "work_duplicate" ||
      name == "work_remove") {
    if (name == "work_rename" && (a["name"].toString().trimmed().isEmpty() ||
                                  a["name"].toString().size() > 120))
      return failure("INVALID_ARGUMENT", "Name must contain 1–120 characters");
    for (int i = 0; i < m_studio->creationPages().size(); ++i)
      if (m_studio->creationPages()[i].toMap()["id"] == workId) {
        if (name == "work_rename")
          m_studio->renamePage(i, a["name"].toString());
        else if (name == "work_duplicate")
          m_studio->duplicatePage(i);
        else
          m_studio->deletePage(i);
        break;
      }
    return fromError(m_studio->error(),
                     {{"workId", m_studio->conversation()->id()}});
  }
  if (name == "work_save") {
    const auto invalid = checkDestination(a);
    if (!invalid.isEmpty())
      return failure("INVALID_DESTINATION", invalid);
    m_studio->saveConversation(QUrl::fromLocalFile(a["path"].toString()));
    return fromError(m_studio->error(), {{"path", a["path"]}});
  }
  if (name == "conversation_update")
    return fromError(updateConversation(thread, a),
                     {{"conversation", thread->json()}});
  if (name == "message_delete" || name == "sfx_remove") {
    if (name == "sfx_remove") {
      bool found = false;
      for (const auto &v : thread->messages()) {
        auto m = v.toMap();
        found |= m["id"] == a["messageId"].toString() && m["task"] == "sfx" &&
                 m["role"] == "assistant";
      }
      if (!found)
        return failure("NOT_FOUND", "Unknown sound-effect result message");
    }
    return m_studio->deleteConversationMessage(
               workId, a["messageId"].toString(), a["cleanup"].toBool())
               ? ok({{"warning", thread->error()}})
               : failure("DELETE_FAILED",
                         thread->error().isEmpty()
                             ? "Message not found or still in use"
                             : thread->error());
  }
  if (name == "message_reuse") {
    QVariantMap message;
    for (const auto &v : thread->messages())
      if (v.toMap()["id"] == a["messageId"].toString())
        message = v.toMap();
    if (message["role"] == "assistant")
      for (const auto &v : thread->messages())
        if (v.toMap()["id"] == message["replyTo"])
          message = v.toMap();
    if (message["role"] != "user")
      return failure("NOT_FOUND", "No source user message");
    QJsonObject input{{"task", message["task"].toString()},
                      {"prompt", message["text"].toString()}};
    for (const auto &key : {"voiceId", "voiceOptions", "editScope"})
      if (message.contains(key) && !message[key].toString().isEmpty())
        input[key] = QJsonValue::fromVariant(message[key]);
    if (message.contains("voiceOptions"))
      input["voiceOptions"] =
          QJsonObject::fromVariantMap(message["voiceOptions"].toMap());
    for (const auto &ref : message["refs"].toStringList()) {
      const auto item = thread->asset(ref);
      if (item["kind"] == "image")
        input["targetId"] = ref;
      else if (item["kind"] == "audio")
        input["audioId"] = ref;
    }
    return fromError(updateConversation(thread, input));
  }
  if (name == "asset_import" || name == "sfx_import") {
    QString path = a.contains("uploadId")
                       ? m_media->uploadedPath(a["uploadId"].toString())
                       : a["path"].toString();
    if (name == "asset_import") {
      if (int(a.contains("path")) + int(a.contains("avatarId")) +
              int(a.contains("source")) + int(a.contains("uploadId")) !=
          1)
        return failure(
            "INVALID_ARGUMENT",
            "Provide exactly one of path, avatarId, source or uploadId");
      if (a.contains("avatarId")) {
        for (const auto &v :
             m_workspace->avatars() + m_workspace->builtinAvatars())
          if (v.toMap()["id"] == a["avatarId"].toString())
            path = local(v.toMap()["url"].toString());
        if (path.isEmpty())
          return failure("NOT_FOUND", "Unknown avatarId");
      } else if (a.contains("source")) {
        const auto source = a["source"].toObject();
        const auto sourceWork = source["workId"].toString();
        auto sourceThread = page(sourceWork).isEmpty()
                                ? nullptr
                                : m_studio->conversationFor(sourceWork);
        if (!sourceThread ||
            sourceThread->asset(source["assetId"].toString()).isEmpty())
          return failure("NOT_FOUND", "Unknown source work or asset");
        path = local(sourceThread->asset(source["assetId"].toString())["url"]
                         .toString());
      }
    } else if (int(a.contains("path")) + int(a.contains("uploadId")) != 1)
      return failure("INVALID_ARGUMENT", "Provide path or uploadId");
    if (!QDir::isAbsolutePath(path))
      return failure("INVALID_ARGUMENT", "Use an absolute local path");
    const auto id = thread->importMedia(
        QUrl::fromLocalFile(path),
        name == "sfx_import" ? "sfx" : a["purpose"].toString());
    return id.isEmpty()
               ? failure("IMPORT_FAILED", thread->error())
               : ok({{"assetId", id},
                     {"asset",
                      QJsonObject::fromVariantMap(thread->asset(id))}});
  }
  if (name == "pack_select") {
    if (thread->asset(assetId).isEmpty())
      return failure("NOT_FOUND", "Unknown assetId");
    if (thread->selected().contains(assetId) != a["selected"].toBool())
      thread->toggleSelected(assetId);
    if (thread->selected().contains(assetId) != a["selected"].toBool())
      return failure("SELECTION_FAILED", thread->error());
    return ok({{"clips", QJsonArray::fromVariantList(thread->clips())}});
  }
  if (name == "pack_move") {
    bool found = false;
    for (const auto &v : thread->clips())
      found |= v.toMap()["id"] == a["clipId"].toString();
    if (!found)
      return failure("NOT_FOUND", "Unknown clipId");
    thread->moveClip(a["clipId"].toString(), a["start"].toDouble());
    return ok({{"clips", QJsonArray::fromVariantList(thread->clips())}});
  }
  if (name == "pack_arrange") {
    thread->arrange();
    return ok({{"clips", QJsonArray::fromVariantList(thread->clips())}});
  }
  if (!m_studio->prepareConversationSettings())
    return failure("BUSY", "Cannot activate work settings");
  auto doc = m_studio->document();
  auto edit = m_studio->editDocument();
  if (name == "generation_settings") {
    const int w = a["width"].toInt(doc->canvasWidth()),
              h = a["height"].toInt(doc->canvasHeight());
    if (w % 16 || h % 16 || qint64(w) * h > 4194304)
      return failure("INVALID_ARGUMENT", "Invalid canvas dimensions");
    doc->setCanvasSize(w, h);
    for (const auto &key : {"steps", "seed", "cfg", "flowShift"})
      if (a.contains(key))
        m_studio->setProperty(key, a[key].toVariant());
  } else if (name == "layout_settings") {
    if (a.contains("enabled"))
      doc->setLayout(a["enabled"].toBool());
    if (a.contains("negativePrompt"))
      doc->setNegativePrompt(a["negativePrompt"].toString());
    if (a.contains("style"))
      doc->setStyle(a["style"].toString());
    if (a.contains("layoutMeta"))
      doc->setLayoutMeta(a["layoutMeta"].toString());
  } else if (name == "template_apply") {
    QJsonObject layout;
    for (const auto &v : m_studio->layoutTemplates())
      if (v.toMap()["id"] == a["templateId"].toString())
        layout = QJsonObject::fromVariantMap(v.toMap()["document"].toMap());
    if (layout.isEmpty())
      return failure("NOT_FOUND", "Unknown templateId");
    QString error;
    if (!doc->load(layout, &error))
      return failure("INVALID_TEMPLATE", error);
    thread->setDraft(doc->prompt());
  } else if (name.startsWith("region_")) {
    int index = -1;
    for (int i = 0; i < doc->regions().size(); ++i)
      if (doc->regions()[i].toMap()["id"] == a["regionId"].toString())
        index = i;
    if (name != "region_add" && index < 0)
      return failure("NOT_FOUND", "Unknown regionId");
    if (name == "region_action") {
      doc->setSelected(index);
      if (a["action"] == "duplicate" && doc->regions().size() >= 100)
        return failure("LIMIT", "At most 100 regions");
      if (a["action"] == "duplicate")
        doc->duplicateSelected();
      else if (a["action"] == "remove")
        doc->removeSelected();
    } else {
      const auto old =
          index >= 0 ? doc->regions()[index].toMap() : QVariantMap();
      const double x = a["x"].toDouble(old["x"].toDouble()),
                   y = a["y"].toDouble(old["y"].toDouble()),
                   w = a["width"].toDouble(old["width"].toDouble()),
                   h = a["height"].toDouble(old["height"].toDouble());
      if (x + w > doc->canvasWidth() || y + h > doc->canvasHeight())
        return failure("INVALID_ARGUMENT", "Region exceeds canvas bounds");
      if (name == "region_add") {
        if (doc->regions().size() >= 100)
          return failure("LIMIT", "At most 100 regions");
        doc->addRegion(x, y, w, h, a["type"].toString("object"));
        index = doc->selected();
        doc->setLayout(true);
      } else
        doc->moveRegion(index, x, y, w, h);
      for (const auto &key : {"name", "caption", "text", "type"})
        if (a.contains(key))
          doc->updateRegion(index, key, a[key].toVariant());
    }
  } else if (name == "edit_history" && a["target"] == "layout") {
    if (a["action"] == "undo")
      doc->undo();
    else
      doc->redo();
  } else if (name == "edit_settings" || name.startsWith("mask_") ||
             name == "edit_history") {
    thread->setTask("edit");
    if (!m_studio->prepareConversationEdit())
      return failure("EDIT_NOT_READY", thread->error());
    if (name == "edit_settings") {
      if (a.contains("scope")) {
        thread->setEditScope(a["scope"].toString());
        edit->setProperty("scope", a["scope"].toString());
      }
      if (a.contains("method"))
        edit->setProperty("editMethod", a["method"].toString());
      for (const auto &key :
           {"sourcePrompt", "strength", "flowStrength", "feather", "blendMode"})
        if (a.contains(key))
          edit->setProperty(key, a[key].toVariant());
    } else if (name == "mask_rectangle") {
      if (a["x"].toDouble() + a["width"].toDouble() > edit->imageWidth() ||
          a["y"].toDouble() + a["height"].toDouble() > edit->imageHeight())
        return failure("INVALID_ARGUMENT",
                       "Rectangle exceeds source image bounds");
      edit->addRectangle(a["x"].toDouble(), a["y"].toDouble(),
                         a["width"].toDouble(), a["height"].toDouble());
    } else if (name == "mask_stroke") {
      const auto points = a["points"].toArray();
      if (points.isEmpty())
        return failure("INVALID_ARGUMENT", "points must not be empty");
      for (const auto &point : points) {
        const auto p = point.toObject();
        if (p["x"].toDouble() > edit->imageWidth() ||
            p["y"].toDouble() > edit->imageHeight())
          return failure("INVALID_ARGUMENT",
                         "Stroke exceeds source image bounds");
      }
      edit->beginStroke();
      QPointF previous(points[0].toObject()["x"].toDouble(),
                       points[0].toObject()["y"].toDouble());
      for (const auto &point : points) {
        const auto p = point.toObject();
        QPointF next(p["x"].toDouble(), p["y"].toDouble());
        edit->paintLine(previous, next, a["diameter"].toDouble(),
                        a["erase"].toBool());
        previous = next;
      }
      edit->endStroke();
    } else if (name == "mask_action") {
      if (a["action"] == "clear")
        edit->clearMask();
      else
        edit->invertMask();
    } else if (a["action"] == "undo")
      edit->undo();
    else
      edit->redo();
  } else
    return failure("NOT_IMPLEMENTED", name);
  m_studio->flushWorkspace();
  return ok({{"document", doc->json()}, {"hasMask", edit->hasMask()}});
}
QJsonObject McpService::enqueue(QString kind, const QJsonObject &args) {
  const auto key = args["requestKey"].toString();
  if (key.trimmed().isEmpty())
    return failure("INVALID_ARGUMENT", "requestKey must not be blank");
  const auto fingerprint = QString::fromLatin1(
      QCryptographicHash::hash(
          kind.toUtf8() + QJsonDocument(args).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
  for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
    if (it.value()["requestKey"] == key) {
      if (it.value()["fingerprint"] != fingerprint)
        return failure("IDEMPOTENCY_CONFLICT",
                       "requestKey already belongs to a different request");
      return ok(publicJob(it.key()));
    }
  if (m_queue.size() >= 32)
    return failure("QUEUE_FULL", "At most 32 pending jobs");
  QJsonObject snapshot;
  QString workId = args["workId"].toString();
  if (kind == "generation_start") {
    if (page(workId).isEmpty())
      return failure("NOT_FOUND", "Unknown workId");
    if (reserved(workId))
      return failure(
          "WORK_BUSY",
          "This work already has an MCP job; query or cancel it first");
    auto thread = m_studio->conversationFor(workId);
    if (thread->pending())
      return failure("WORK_BUSY", "This work has an active UI generation");
    m_studio->flushWorkspace();
    ConversationDocument candidate(workId, m_studio->outputRoot(),
                                   thread->json());
    const auto error = updateConversation(&candidate, args);
    if (!error.isEmpty())
      return failure("INVALID_ARGUMENT", error);
    if (!candidate.beginRequest())
      return failure("INVALID_REQUEST", candidate.error());
    // Store the pre-request state, never a synthesized pending message.
    ConversationDocument input(workId, m_studio->outputRoot(), thread->json());
    updateConversation(&input, args);
    const auto p = page(workId);
    snapshot = {
        {"conversation", input.json()},
        {"document", QJsonObject::fromVariantMap(p["document"].toMap())},
        {"edit", QJsonObject::fromVariantMap(p["edit"].toMap())},
        {"generation", QJsonObject::fromVariantMap(p["generation"].toMap())}};
  } else if (kind == "voice_import" || kind == "avatar_import") {
    if (args["name"].toString().trimmed().isEmpty())
      return failure("INVALID_ARGUMENT", "Name must not be blank");
    QString path = args.contains("uploadId")
                       ? m_media->uploadedPath(args["uploadId"].toString())
                       : args["path"].toString();
    if (args.contains("source")) {
      const auto source = args["source"].toObject();
      const auto sourceWork = source["workId"].toString();
      auto thread = page(sourceWork).isEmpty()
                        ? nullptr
                        : m_studio->conversationFor(sourceWork);
      const auto asset =
          thread ? thread->asset(source["assetId"].toString()) : QVariantMap();
      if (asset.isEmpty())
        return failure("NOT_FOUND", "Unknown source work or asset");
      if (asset["kind"].toString() !=
          (kind == "voice_import" ? "audio" : "image"))
        return failure("INVALID_ARGUMENT",
                       "Source asset has the wrong media type");
      path = local(asset["url"].toString());
    }
    if (!QDir::isAbsolutePath(path) || !QFileInfo(path).isFile())
      return failure("INVALID_ARGUMENT",
                     "Source must be an existing local file");
    snapshot["path"] = path;
  } else if (kind.startsWith("model_")) {
    QStringList groups;
    if (kind == "model_verify")
      groups << args["group"].toString();
    else
      for (const auto &v : args["groups"].toArray())
        groups << v.toString();
    if (groups.isEmpty())
      return failure("INVALID_ARGUMENT", "Select at least one model group");
    QStringList valid{"image"};
    for (const auto &v : m_studio->models()->groups())
      valid << v.toMap()["id"].toString();
    for (const auto &group : groups)
      if (!valid.contains(group))
        return failure("NOT_FOUND", "Unknown model group: " + group);
    if (groups.contains("sfx") && !m_studio->models()->sfxTermsAccepted())
      return failure("TERMS_REQUIRED",
                     "Read model_terms and obtain the user's acceptance before "
                     "downloading sound models");
  } else if (args["description"].toString().trimmed().isEmpty() ||
             args["name"].toString().trimmed().isEmpty() ||
             (kind == "voice_design" &&
              args["text"].toString().trimmed().isEmpty()))
    return failure("INVALID_ARGUMENT",
                   "Name, description and sample text must not be blank");
  const auto id = uid();
  m_jobs[id] = {{"jobId", id},
                {"kind", kind},
                {"workId", workId},
                {"requestKey", key},
                {"fingerprint", fingerprint},
                {"arguments", args},
                {"snapshot", snapshot},
                {"status", "queued"},
                {"createdAt", now()},
                {"stage", "等待执行"},
                {"stageProgress", QJsonValue::Null},
                {"callId", m_callId}};
  if (!saveJob(id)) {
    m_jobs.remove(id);
    return failure("STORAGE_ERROR", "Cannot save job; nothing was scheduled");
  }
  m_queue.append(id);
  QTimer::singleShot(0, this, &McpService::runNext);
  return ok(publicJob(id));
}
void McpService::runNext() {
  if (m_stopping || m_starting || !m_active.isEmpty() || m_studio->busy() ||
      m_queue.isEmpty())
    return;
  m_starting = true;
  auto guard = qScopeGuard([&] { m_starting = false; });
  const auto id = m_queue.takeFirst();
  auto &job = m_jobs[id];
  m_active = id;
  job["status"] = "running";
  job["startedAt"] = now();
  job["startedMs"] = QDateTime::currentMSecsSinceEpoch();
  job["stage"] = "准备执行";
  if (!saveJob(id)) {
    job["status"] = "failed";
    job["error"] = "Cannot save running job state";
    m_active.clear();
    return;
  }
  const auto args = job["arguments"].toObject();
  const auto kind = job["kind"].toString();
  auto models = m_studio->models();
  m_studio->dismissError();
  auto abort = [&](QString message) {
    job["error"] = message;
    finishJob(false);
  };
  if (kind == "voice_import" || kind == "avatar_import") {
    const auto source =
        QUrl::fromLocalFile(job["snapshot"].toObject()["path"].toString());
    const auto error =
        kind == "voice_import"
            ? m_studio->sound()->createVoice(args["name"].toString(), source,
                                             args["transcript"].toString())
            : m_workspace->importAvatar(source, args["name"].toString());
    if (!error.isEmpty()) {
      abort(error);
      return;
    }
    finishJob(true);
    return;
  } else if (kind == "model_download") {
    QStringList groups;
    for (const auto &v : args["groups"].toArray())
      groups << v.toString();
    models->setRequested(groups);
    models->startDownload();
  } else if (kind == "model_verify")
    models->verifyGroup(args["group"].toString());
  else if (kind == "voice_design") {
    if (!m_studio->voiceReady()) {
      abort(
          "Voice resources are not ready; use model_list/model_download first");
      return;
    }
    m_studio->generateVoiceDesign(args["description"].toString(),
                                  args["text"].toString());
  } else if (kind == "avatar_design") {
    if (!m_studio->imageReady()) {
      abort(
          "Image resources are not ready; use model_list/model_download first");
      return;
    }
    m_studio->generateAvatarImage(args["description"].toString());
  } else {
    const auto workId = job["workId"].toString();
    if (page(workId).isEmpty()) {
      abort("Work was removed before execution");
      return;
    }
    auto thread = m_studio->conversationFor(workId);
    const auto snapshot = job["snapshot"].toObject();
    ConversationDocument request(workId, m_studio->outputRoot(),
                                 snapshot["conversation"].toObject());
    const auto task = request.task();
    if ((task == "edit" || task == "avatar") &&
        thread->asset(request.target()["id"].toString()).isEmpty()) {
      abort("The submitted image was removed before execution");
      return;
    }
    if (!request.audio().isEmpty() &&
        thread->asset(request.audio()["id"].toString()).isEmpty()) {
      abort("The submitted audio was removed before execution");
      return;
    }
    if (task == "pack")
      for (const auto &assetId : request.selected()) {
        if (thread->asset(assetId.toString()).isEmpty()) {
          abort("A submitted pack asset was removed before execution");
          return;
        }
      }
    if (((task == "image" || task == "edit") && !m_studio->imageReady()) ||
        ((task == "voice" || (task == "avatar" && request.audio().isEmpty())) &&
         !m_studio->voiceReady()) ||
        (task == "avatar" && !m_studio->dittoReady()) ||
        (task == "sfx" && !m_studio->sfxReady())) {
      abort("Required resources are not ready; use app_status and "
            "model_list/model_download first");
      return;
    }
    if (task == "sfx" && !models->sfxTermsAccepted()) {
      abort("Sound model terms require user acceptance");
      return;
    }
    m_studio->openConversation(workId);
    if (!m_studio->prepareConversationSettings()) {
      abort("Could not activate work");
      return;
    }
    QString error;
    if (!m_studio->document()->load(snapshot["document"].toObject(), &error)) {
      abort(error);
      return;
    }
    const auto generation = snapshot["generation"].toObject();
    for (auto it = generation.begin(); it != generation.end(); ++it)
      if (QStringList{"steps", "seed", "cfg", "flowShift"}.contains(it.key()))
        m_studio->setProperty(it.key().toUtf8(), it.value().toVariant());
    thread->setTask(task);
    thread->setDraft(request.draft());
    thread->setEditScope(request.editScope());
    thread->setVoiceId(request.voiceId());
    thread->setVoiceOptions(request.voiceOptions());
    thread->setSfxDuration(request.sfxDuration());
    if (task == "edit" || task == "avatar")
      thread->chooseTarget(request.target()["id"].toString());
    thread->chooseAudio(request.audio()["id"].toString());
    if (task == "pack") {
      const auto selected = thread->selected();
      for (const auto &value : selected)
        thread->toggleSelected(value.toString());
      for (const auto &value : request.selected())
        thread->toggleSelected(value.toString());
      for (const auto &value : request.clips()) {
        const auto clip = value.toMap();
        thread->moveClip(clip["id"].toString(), clip["start"].toDouble());
      }
    }
    if (task == "edit") {
      auto edit = snapshot["edit"].toObject();
      if (local(request.target()["url"].toString()) !=
          edit["source"].toString())
        edit = {};
      if (!edit.isEmpty()) {
        error = m_studio->editDocument()->restoreSnapshot(edit);
        if (!error.isEmpty()) {
          abort(error);
          return;
        }
      }
      if (!m_studio->prepareConversationEdit()) {
        abort(thread->error());
        return;
      }
    }
    m_studio->flushWorkspace();
    job["messageCount"] = thread->messages().size();
    m_studio->sendConversation();
    if (m_active == id && !thread->pending() && !m_studio->busy()) {
      abort(thread->error().isEmpty() ? m_studio->error() : thread->error());
      return;
    }
  }
  if (m_active == id && !m_studio->busy()) {
    abort(kind.startsWith("model_") ? models->error() : m_studio->error());
    return;
  }
  updateProgress();
}
void McpService::updateProgress() {
  if (m_active.isEmpty())
    return;
  auto &job = m_jobs[m_active];
  const bool model = job["kind"].toString().startsWith("model_");
  const auto stage = model ? m_studio->models()->status() : m_studio->status();
  const auto progress =
      model ? m_studio->models()->progress() : m_studio->progress();
  const bool changed =
      job["stage"] != stage || job["stageProgress"] != progress;
  job["stage"] = stage;
  job["stageProgress"] = progress;
  job["elapsedMs"] =
      QDateTime::currentMSecsSinceEpoch() - job["startedMs"].toInteger();
  if (changed)
    saveJob(m_active);
  m_calls.update(job["callId"].toString(),
                 {{"state", job["status"].toString()},
                  {"stage", stage},
                  {"progress", progress},
                  {"elapsedMs", job["elapsedMs"].toVariant()},
                  {"resultText", jsonText(publicJob(m_active))}});
}
void McpService::finishJob(bool success) {
  if (m_active.isEmpty() || m_stopping)
    return;
  const auto id = m_active;
  auto &job = m_jobs[id];
  const auto kind = job["kind"].toString();
  const auto args = job["arguments"].toObject();
  const bool cancelled = job["cancelRequested"].toBool();
  QJsonObject result;
  QString error = job["error"].toString();
  if (success && !cancelled) {
    if (kind == "voice_design" || kind == "voice_import") {
      if (kind == "voice_design")
        error = m_studio->saveDesignedVoice(args["name"].toString());
      result = {{"voice", QJsonObject::fromVariantMap(
                              m_studio->sound()->selectedVoice())}};
      result["voiceId"] = result["voice"].toObject()["id"];
    } else if (kind == "avatar_design" || kind == "avatar_import") {
      if (kind == "avatar_design")
        error = m_workspace->importAvatar(QUrl(m_studio->avatarDesignImage()),
                                          args["name"].toString(),
                                          m_studio->avatarDesignRecipe());
      if (error.isEmpty() && !m_workspace->avatars().isEmpty())
        result = {{"avatar", QJsonObject::fromVariantMap(
                                 m_workspace->avatars().first().toMap())}};
      result["avatarId"] = result["avatar"].toObject()["id"];
    } else if (kind.startsWith("model_"))
      result = execute("model_list", {})["data"].toObject();
    else {
      auto thread = m_studio->conversationFor(job["workId"].toString());
      QJsonArray assets;
      if (thread)
        for (int i = job["messageCount"].toInt(); i < thread->messages().size();
             ++i) {
          const auto message = thread->messages()[i].toMap();
          if (message["role"] != "assistant")
            continue;
          job["messageId"] = message["id"].toString();
          for (const auto &assetId : message["assetIds"].toStringList())
            assets.append(QJsonObject::fromVariantMap(thread->asset(assetId)));
        }
      if (assets.isEmpty())
        error = "No generated assets were saved";
      result = {{"assets", assets}};
    }
    success = error.isEmpty();
    if (!success)
      result = {}; // A failed library save must never expose the previously
                   // selected resource as its result.
  }
  if (!success && error.isEmpty())
    error = kind.startsWith("model_") ? m_studio->models()->error()
                                      : m_studio->error();
  if (!success && error.isEmpty())
    error = "Task failed before producing a result";
  job["status"] = cancelled ? "cancelled" : success ? "completed" : "failed";
  job["stage"] = cancelled ? "已取消" : success ? "已完成" : "执行失败";
  job["error"] = cancelled ? "Task cancelled" : error;
  job["result"] = result;
  job["finishedAt"] = now();
  job["elapsedMs"] =
      QDateTime::currentMSecsSinceEpoch() - job["startedMs"].toInteger();
  if (success)
    job["stageProgress"] = 1.;
  saveJob(id);
  m_calls.update(job["callId"].toString(),
                 {{"state", job["status"].toString()},
                  {"stage", job["stage"].toString()},
                  {"elapsedMs", job["elapsedMs"].toVariant()},
                  {"progress", job["stageProgress"].toVariant()},
                  {"resultText", jsonText(publicJob(id))}});
  m_active.clear();
  QTimer::singleShot(0, this, &McpService::runNext);
}
