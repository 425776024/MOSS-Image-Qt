#include "McpCatalog.h"
#include <QJsonDocument>
#include <cmath>

namespace {
QJsonObject str(QString description = {}, int maximum = 16000) {
  return {
      {"type", "string"}, {"description", description}, {"maxLength", maximum}};
}
QJsonObject id(QString description) {
  auto s = str(description, 200);
  s["minLength"] = 1;
  return s;
}
QJsonObject number(double minimum, double maximum, bool integer = false) {
  return {{"type", integer ? "integer" : "number"},
          {"minimum", minimum},
          {"maximum", maximum}};
}
QJsonObject choice(QStringList values) {
  return {{"type", "string"}, {"enum", QJsonArray::fromStringList(values)}};
}
QJsonObject boolean() { return {{"type", "boolean"}}; }
QJsonObject object(QJsonObject properties = {}, QStringList required = {}) {
  return {{"type", "object"},
          {"properties", properties},
          {"required", QJsonArray::fromStringList(required)},
          {"additionalProperties", false}};
}
QJsonObject array(QJsonObject items, int maximum = 100) {
  return {{"type", "array"}, {"items", items}, {"maxItems", maximum}};
}
QJsonObject work(QJsonObject properties = {}) {
  properties["workId"] = id("Stable work ID from work_list/work_create. Never "
                            "inferred from the visible page.");
  return properties;
}
} // namespace
QJsonArray McpCatalog::tools() {
  static const QJsonArray catalog = [] {
    QJsonArray list;
    auto add = [&](QString name, QString title, QString description,
                   QJsonObject properties, QStringList required = {},
                   bool read = false, bool destructive = false) {
      list.append(QJsonObject{
          {"name", name},
          {"title", title},
          {"description", description},
          {"inputSchema", object(properties, required)},
          {"outputSchema",
           QJsonObject{
               {"type", "object"},
               {"properties",
                QJsonObject{{"ok", boolean()},
                            {"data", QJsonObject{{"type", "object"}}},
                            {"error", QJsonObject{{"type", "object"}}}}},
               {"required", QJsonArray{"ok"}}}},
          {"annotations", QJsonObject{{"readOnlyHint", read},
                                      {"destructiveHint", destructive},
                                      {"idempotentHint", read},
                                      {"openWorldHint", false}}}});
    };
    add("app_status", "应用状态",
        "Read engine readiness, busy state, active MCP job and service "
        "capabilities. No downloads are started.",
        {}, {}, true);
    const auto sourceAsset =
        object(work({{"assetId", id("Existing work asset ID")}}),
               {"workId", "assetId"});
    const QJsonObject mediaSource{{"source", sourceAsset},
                                  {"uploadId", id("Completed upload ID")},
                                  {"avatarId", id("Avatar library ID")},
                                  {"voiceId", id("Voice library ID")}};
    add("media_upload_begin", "开始上传素材",
        "Upload media FROM THE AGENT MACHINE without choosing a server "
        "directory. "
        "Start with filename and byte size (max 2 GiB), optionally SHA-256. "
        "Returns uploadId and chunkBytes. Send sequential base64 chunks with "
        "media_upload_chunk, then media_upload_finish. At most 16 unfinished "
        "uploads.",
        {{"filename", str("Original media filename, no directories", 200)},
         {"size", number(1, 2147483648., true)},
         {"sha256", str("Optional full-file SHA-256", 64)}},
        {"filename", "size"});
    add("media_upload_chunk", "上传素材分块",
        "Write up to 1 MiB of raw bytes encoded as base64 at offset. "
        "Exact retries of already written chunks are accepted; "
        "changed/overlapping "
        "or out-of-order chunks are rejected. Returns received byte count.",
        {{"uploadId", id("Upload ID")},
         {"offset", number(0, 2147483648., true)},
         {"dataBase64", str("Base64-encoded raw file chunk", 1398104)}},
        {"uploadId", "offset", "dataBase64"});
    add("media_upload_finish", "完成素材上传",
        "Verify completed size, optional SHA-256 and media type. Returns "
        "uploadId "
        "and one-hour media preview/download URLs. Pass uploadId to "
        "asset_import, "
        "avatar_create/import or voice_create/import or sfx_import. No server "
        "path needed.",
        {{"uploadId", id("Upload ID")}}, {"uploadId"});
    add("media_upload_status", "查询上传进度",
        "With uploadId, read durable received bytes and completion state to "
        "resume "
        "an upload after reconnect/restart. Without uploadId, list recent "
        "uploads "
        "and all unfinished uploads, including IDs for discarding abandoned "
        "staging files.",
        {{"uploadId", id("Optional upload ID")}}, {}, true);
    add("media_upload_discard", "清理上传暂存",
        "Delete this upload's staging file, including an incomplete upload. "
        "Imported library/work copies remain. Pending jobs using the upload "
        "must finish first.",
        {{"uploadId", id("Upload ID")}}, {"uploadId"}, false, true);
    auto download = mediaSource;
    download["offset"] = number(0, 2147483648., true);
    download["maxBytes"] = number(1, 1048576, true);
    add("media_read", "下载素材分块",
        "Read media bytes as base64 using exactly one of source, uploadId, "
        "avatarId "
        "or voiceId. Default offset=0/maxBytes=1 MiB. Save decoded bytes on "
        "the agent "
        "machine and continue until eof=true. Results also offer temporary "
        "HTTP "
        "media links; those support GET/HEAD and byte ranges for audio/video "
        "playback.",
        download, {}, true);
    add("media_preview", "预览图像",
        "Return a JPEG thumbnail as a native MCP image content block (max 1280 "
        "px), "
        "using exactly one source, uploadId or avatarId. Use media URLs or "
        "media_read "
        "for audio/video. Whether images/players display in a conversation "
        "depends "
        "on the agent client; download to its local renderer when needed.",
        mediaSource, {}, true);
    add("work_list", "作品列表", "List work IDs, names and timestamps.", {}, {},
        true);
    add("work_create", "创建作品",
        "Create a new conversation work and return its stable workId.",
        {{"name", str("Optional work name", 120)}});
    add("work_get", "读取作品",
        "Read messages, assets, selected media, clips, draft, generation and "
        "layout settings. Includes persisted results after reconnect.",
        work(), {"workId"}, true);
    add("work_rename", "重命名作品",
        "Rename a work. Returns BUSY during generation.",
        work({{"name", id("New name (up to 120 characters)")}}),
        {"workId", "name"});
    add("work_duplicate", "复制作品",
        "Duplicate a work including its conversation and references.", work(),
        {"workId"});
    add("work_remove", "移除作品",
        "Remove a work from the list, preserving generated files. The most "
        "recently removed work can be restored.",
        work(), {"workId"}, false, true);
    add("work_restore", "恢复作品", "Restore the most recently removed work.",
        {});
    add("work_save", "保存工程",
        "Save a .moss.json project to an absolute local path. Existing files "
        "require overwrite=true; sandboxed builds require filesystem access "
        "already granted by the user.",
        work({{"path", id("Absolute destination path")},
              {"overwrite", boolean()}}),
        {"workId", "path"});
    const auto voiceOptions = object(
        {{"emotion",
          choice({"中性", "高兴", "悲伤", "愤怒", "恐惧", "厌恶", "惊讶"})},
         {"dialect", choice({"standard", "chuanyu", "cantonese"})}});
    const QJsonObject conversation = {
        {"task", choice({"image", "edit", "avatar", "voice", "sfx", "pack"})},
        {"prompt", str("Prompt or speech text")},
        {"targetId", str("Image asset ID; empty selects latest image", 200)},
        {"audioId", str("Audio asset ID; empty uses text-to-speech", 200)},
        {"voiceId", id("Voice from voice_list")},
        {"voiceOptions", voiceOptions},
        {"editScope", choice({"whole", "local"})},
        {"sfxDuration", number(1, 30, true)}};
    add("conversation_update", "编辑创作输入",
        "Update draft, task, target, audio and voice options. Omitted fields "
        "are preserved. Cannot modify a work with a queued/running MCP job.",
        work(conversation), {"workId"});
    add("message_reuse", "复用消息",
        "Restore a user message's text, task, voice and media references into "
        "the draft.",
        work({{"messageId", id("User or assistant message ID")}}),
        {"workId", "messageId"});
    add("message_delete", "删除消息",
        "Delete a message. cleanup=true also removes eligible unreferenced "
        "temporary media using the same ownership checks as the UI.",
        work({{"messageId", id("Message ID")}, {"cleanup", boolean()}}),
        {"workId", "messageId"}, false, true);
    add("asset_import", "导入素材",
        "Import media from exactly one of uploadId, path, avatarId, or source "
        "(an "
        "existing "
        "work asset). Library IDs avoid filesystem handling. Returns assetId. "
        "No remote URL fetching. Wait for valid media duration before packing.",
        work({{"path", str("Absolute local source path", 4096)},
              {"uploadId", id("Completed remote upload ID")},
              {"avatarId", id("Portrait ID from avatar_list/avatar_create")},
              {"source", object(work({{"assetId", id("Source asset ID")}}),
                                {"workId", "assetId"})},
              {"purpose", choice({"target", "audio", "pack", ""})}}),
        {"workId"});
    add("asset_get", "读取素材",
        "Return asset metadata and media.downloadUrl/previewUrl valid for one "
        "hour. "
        "HTTP links support byte ranges for audio/video. The file URL is local "
        "to the app machine; remote agents should use the HTTP links or "
        "media_read.",
        work({{"assetId", id("Asset ID")}}), {"workId", "assetId"}, true);
    add("asset_export", "导出素材",
        "Copy a work asset to an absolute local path. Existing files require "
        "overwrite=true; sandbox access must already be granted.",
        work({{"assetId", id("Asset ID")},
              {"path", id("Absolute destination path")},
              {"overwrite", boolean()}}),
        {"workId", "assetId", "path"});
    add("voice_list", "读取音色",
        "List available voice IDs, voice descriptions and supported "
        "emotion/dialect options.",
        {}, {}, true);
    add("voice_create", "创建并保存音色",
        "Preferred named voice creation: supply exactly one of description "
        "(generate), source (existing audio asset), uploadId, or path (WAV on "
        "the app machine). "
        "Automatically imports and persists the voice in the app library; no "
        "save call or output directory needed. Returns jobId; job_result "
        "returns "
        "voiceId and voice. For generation, text is optional with a built-in "
        "Chinese sample. For imports, transcript is required; WAV must be "
        "3–30 seconds and at most 20 MB.",
        {{"name", str("Saved voice name", 120)},
         {"uploadId", id("Completed remote upload ID")},
         {"description", str("Voice description", 1000)},
         {"text", str("Optional generation sample speech", 120)},
         {"source", object(work({{"assetId", id("Audio asset ID")}}),
                           {"workId", "assetId"})},
         {"path", str("Absolute local source WAV path", 4096)},
         {"transcript", str("Exact words spoken in the imported recording")},
         {"requestKey", id("Idempotency key")}},
        {"name", "requestKey"});
    add("voice_import", "导入音色",
        "Import a custom voice from exactly one path (app machine) or "
        "uploadId, a WAV (3–30 seconds, at "
        "most 20 MB) and its exact transcript. Returns the saved voice.",
        {{"path", id("Absolute WAV path")},
         {"uploadId", id("Completed remote upload ID")},
         {"name", str("Voice name", 120)},
         {"transcript", str("Words spoken in the recording")}},
        {"name", "transcript"});
    add("voice_update", "修改音色",
        "Rename an imported/designed voice or correct its reference "
        "transcript. Built-in voices are read-only.",
        {{"voiceId", id("Custom voice ID")},
         {"name", str("Voice name", 120)},
         {"transcript", str()}},
        {"voiceId", "name", "transcript"});
    add("voice_remove", "移除音色",
        "Archive a custom voice using the app's recoverable removal. Built-in "
        "voices cannot be removed.",
        {{"voiceId", id("Custom voice ID")}}, {"voiceId"}, false, true);
    add("voice_restore", "恢复音色",
        "Restore the most recently removed custom voice.", {});
    add("voice_design", "设计音色",
        "Generate a voice from a description and sample text, then save it to "
        "the voice library under name. Returns jobId. Sample should be long "
        "enough to produce 3–30 seconds of speech; saving can fail if outside "
        "those limits.",
        {{"description", str("Voice description", 1000)},
         {"text", str("Sample speech", 120)},
         {"name", str("Saved voice name", 120)},
         {"requestKey", id("Idempotency key")}},
        {"description", "text", "name", "requestKey"});
    add("avatar_list", "读取数字人形象",
        "List imported/designed and built-in portrait assets. A portrait is "
        "the image input for an avatar video.",
        {}, {}, true);
    add("avatar_create", "创建并保存数字人形象",
        "Preferred named portrait creation: supply exactly one of description "
        "(text-to-image), source (existing image asset), uploadId, or path "
        "(app-local "
        "image). "
        "Automatically copies the result into the app avatar library and "
        "persists it. No output directory or separate save call needed. "
        "Returns "
        "jobId; job_result returns avatarId and avatar. Use asset_import with "
        "avatarId to use the portrait in a work.",
        {{"name", str("Saved avatar name", 120)},
         {"uploadId", id("Completed remote upload ID")},
         {"description", str("Portrait description", 1000)},
         {"source", object(work({{"assetId", id("Image asset ID")}}),
                           {"workId", "assetId"})},
         {"path", str("Absolute local source image path", 4096)},
         {"requestKey", id("Idempotency key")}},
        {"name", "requestKey"});
    add("avatar_import", "导入数字人形象",
        "Copy a portrait from exactly one path (app machine) or uploadId into "
        "the avatar library (at "
        "most 50 MB).",
        {{"path", id("Absolute image path")},
         {"uploadId", id("Completed remote upload ID")},
         {"name", str("Avatar name", 120)}},
        {"name"});
    add("avatar_rename", "重命名数字人形象", "Rename a custom avatar.",
        {{"avatarId", id("Custom avatar ID")},
         {"name", str("Avatar name", 120)}},
        {"avatarId", "name"});
    add("avatar_remove", "移除数字人形象",
        "Archive a custom avatar. Imported source files and existing works are "
        "preserved; built-in avatars are read-only.",
        {{"avatarId", id("Custom avatar ID")}}, {"avatarId"}, false, true);
    add("avatar_restore", "恢复数字人形象",
        "Restore the most recently removed custom avatar.", {});
    add("avatar_design", "设计数字人形象",
        "Generate a portrait and save it to the avatar library. Returns jobId.",
        {{"description", str("Portrait description", 1000)},
         {"name", str("Avatar name", 120)},
         {"requestKey", id("Idempotency key")}},
        {"description", "name", "requestKey"});
    add("sfx_list", "查询音效",
        "List sound effect assets and their owning work/message IDs. Sound "
        "effects are stored in works, not a separate global library.",
        {{"workId", id("Optional work filter")}}, {}, true);
    add("sfx_import", "导入音效",
        "Import audio from exactly one path (app machine) or completed "
        "uploadId into a work as a sound effect.",
        work({{"path", id("Absolute audio path")},
              {"uploadId", id("Completed remote upload ID")}}),
        {"workId"});
    add("sfx_design", "设计音效",
        "Generate a sound effect into the specified work and return jobId. "
        "Requires accepted model terms and installed sfx resources.",
        work({{"prompt", str("Sound description")},
              {"sfxDuration", number(1, 30, true)},
              {"requestKey", id("Idempotency key")}}),
        {"workId", "prompt", "requestKey"});
    add("sfx_remove", "移除音效",
        "Remove the sound effect message from its work; optional cleanup "
        "reclaims only eligible unreferenced app-owned files. Referenced audio "
        "remains available to existing messages.",
        work({{"messageId", id("Sound effect result message ID from sfx_list")},
              {"cleanup", boolean()}}),
        {"workId", "messageId"}, false, true);
    add("model_list", "查询模型资源",
        "Read model groups, readiness, sizes, source choices, current download "
        "progress and terms state.",
        {}, {}, true);
    add("model_terms", "读取模型条款",
        "Read the model terms before asking the human user to accept them.",
        {{"english", boolean()}}, {}, true);
    add("model_accept_terms", "接受音效模型条款",
        "Record the human user's explicit informed acceptance of the current "
        "sound-model terms. Never call autonomously; first show model_terms "
        "and obtain acceptance.",
        {{"version", choice({"2026-10-22"})}, {"userAccepted", boolean()}},
        {"version", "userAccepted"});
    add("model_configure", "配置模型资源",
        "Set download source, image quantization or an already accessible "
        "absolute model directory. Does not start downloads.",
        {{"source", str("Value from model_list.sources", 200)},
         {"imageQuant", choice({"Q4_K", "Q8_0"})},
         {"root", id("Absolute directory (non-Store builds only)")}});
    add("model_download", "下载模型资源",
        "Download/resume catalogued groups using the app downloader. Returns "
        "jobId; poll job_get. No implicit model-term acceptance.",
        {{"groups", array(id("Group/capability ID from model_list"), 16)},
         {"requestKey", id("Idempotency key")}},
        {"groups", "requestKey"});
    add("model_verify", "校验模型资源",
        "Verify catalogued model files using their checksums. Returns jobId.",
        {{"group", id("Group ID")}, {"requestKey", id("Idempotency key")}},
        {"group", "requestKey"});
    add("model_remove", "删除模型资源",
        "Delete only files registered for this model group. Existing works and "
        "unregistered files remain. Read model_list and the deletion "
        "description first.",
        {{"group", id("Group ID")}}, {"group"}, false, true);
    add("model_clean_partials", "清理下载残留",
        "Remove catalogued partial downloads while no generation/download is "
        "active.",
        {}, {}, false, true);
    add("template_list", "读取构图模板",
        "List built-in and imported layout templates with IDs and editable "
        "documents.",
        {}, {}, true);
    add("template_apply", "应用构图模板",
        "Apply a template to an existing work; does not start generation.",
        work({{"templateId", id("ID from template_list")}}),
        {"workId", "templateId"});
    add("generation_settings", "设置生成参数",
        "Update canvas size and sampling parameters. Width/height must be "
        "multiples of 16, 256–4096, at most 4194304 pixels total.",
        work({{"width", number(256, 4096, true)},
              {"height", number(256, 4096, true)},
              {"steps", number(1, 100, true)},
              {"seed", number(0, 2147483647, true)},
              {"cfg", number(1, 10)},
              {"flowShift", number(1, 10)}}),
        {"workId"});
    add("layout_settings", "设置布局",
        "Update layout enablement, negative prompt, style and layout metadata.",
        work({{"enabled", boolean()},
              {"negativePrompt", str()},
              {"style", str()},
              {"layoutMeta", str()}}),
        {"workId"});
    QJsonObject region = {{"x", number(0, 4096)},
                          {"y", number(0, 4096)},
                          {"width", number(4, 4096)},
                          {"height", number(4, 4096)},
                          {"type", choice({"object", "text"})},
                          {"name", str()},
                          {"caption", str()},
                          {"text", str()}};
    add("region_add", "添加构图区域",
        "Add an object or text region in canvas pixels. Geometry must fit the "
        "canvas.",
        work(region), {"workId", "x", "y", "width", "height"});
    region["regionId"] = id("Stable region ID from work_get");
    add("region_update", "修改构图区域",
        "Update region properties or geometry in canvas pixels.", work(region),
        {"workId", "regionId"});
    add("region_action", "操作构图区域",
        "Select, duplicate or remove a region by ID.",
        work({{"regionId", id("Region ID")},
              {"action", choice({"select", "duplicate", "remove"})}}),
        {"workId", "regionId", "action"});
    add("edit_settings", "设置图像编辑",
        "Configure whole/local image editing for the selected image. Source "
        "prompt describes the original image when using FlowEdit.",
        work({{"scope", choice({"whole", "local"})},
              {"method", choice({"flowedit", "standard"})},
              {"sourcePrompt", str()},
              {"strength", number(0.01, 1)},
              {"flowStrength", number(0.01, 1)},
              {"feather", number(0, 128, true)},
              {"blendMode", choice({"feather", "direct"})}}),
        {"workId"});
    add("mask_rectangle", "框选蒙版",
        "Add a mask rectangle in source-image pixels.",
        work({{"x", number(0, 32768)},
              {"y", number(0, 32768)},
              {"width", number(1, 32768)},
              {"height", number(1, 32768)}}),
        {"workId", "x", "y", "width", "height"});
    add("mask_stroke", "涂抹蒙版",
        "Paint or erase one undoable stroke using source-image pixel "
        "coordinates and brush diameter.",
        work({{"points",
               array(object({{"x", number(0, 32768)}, {"y", number(0, 32768)}},
                            {"x", "y"}),
                     2048)},
              {"diameter", number(1, 1024)},
              {"erase", boolean()}}),
        {"workId", "points", "diameter"});
    add("mask_action", "操作蒙版", "Clear or invert the local editing mask.",
        work({{"action", choice({"clear", "invert"})}}), {"workId", "action"});
    add("edit_history", "撤销或重做",
        "Undo/redo layout or mask changes in the selected work.",
        work({{"target", choice({"layout", "mask"})},
              {"action", choice({"undo", "redo"})}}),
        {"workId", "target", "action"});
    add("pack_select", "选择打包素材",
        "Explicitly include/exclude a media asset (safe to retry); returns "
        "current clips.",
        work({{"assetId", id("Asset ID")}, {"selected", boolean()}}),
        {"workId", "assetId", "selected"});
    add("pack_move", "调整片段时间",
        "Move a clip to a start time in seconds. Linked audio follows the "
        "visual clip as in the UI.",
        work({{"clipId", id("Clip ID from work_get")},
              {"start", number(0, 600)}}),
        {"workId", "clipId", "start"});
    add("pack_arrange", "顺序排列",
        "Arrange selected visuals/audio sequentially using the same rules as "
        "the UI.",
        work(), {"workId"});
    auto generation = conversation;
    generation["requestKey"] = id("Client-generated idempotency key. Reuse "
                                  "only for an identical submission.");
    add("generation_start", "提交生成任务",
        "Snapshot this work and enqueue image/edit/avatar/voice/sfx/pack. "
        "Returns immediately with jobId; poll job_get (about once per second), "
        "then job_result. Requires existing models; does not auto-download. "
        "One MCP job per work. requestKey prevents duplicate generation on "
        "retries.",
        work(generation), {"workId", "requestKey"});
    add("job_list", "任务列表",
        "List up to 100 recent MCP jobs, optionally filtered by work.",
        {{"workId", id("Work ID")}}, {}, true);
    add("job_get", "查询任务进度",
        "Read durable task state, stage, stageProgress (0–1 or null), "
        "timestamps and results. stageProgress may restart on stage changes; "
        "it is not overall progress.",
        {{"jobId", id("Job ID")}}, {"jobId"}, true);
    add("job_result", "读取生成结果",
        "Read final asset metadata after completion; returns NOT_READY for "
        "pending jobs and a structured error for failed/cancelled/interrupted "
        "jobs.",
        {{"jobId", id("Job ID")}}, {"jobId"}, true);
    add("job_cancel", "取消生成任务",
        "Cancel only the specified MCP job, never an unrelated UI job. Queued "
        "jobs cancel immediately.",
        {{"jobId", id("Job ID")}}, {"jobId"});
    add("app_logs", "读取运行日志",
        "Read current engine logs for diagnostics (bounded tail). Use job_get "
        "for stable task status.",
        {}, {}, true);
    add("ui_control", "控制作品界面",
        "Show a work, select/preview a message or asset, play/pause/seek "
        "media, show/hide details, open editing/packing tools, or navigate to "
        "settings. Requires the live app window. UI actions are distinct from "
        "generation.",
        {{"workId", id("Work ID")},
         {"action",
          choice({"show", "details", "hide_details", "edit", "pack", "play",
                  "pause", "seek", "expand", "settings", "models", "voices",
                  "logs", "parameters", "theme"})},
         {"assetId", id("Asset ID")},
         {"messageId", id("Message ID")},
         {"seconds", number(0, 86400)},
         {"theme", choice({"light", "dark", "system"})}},
        {"action"});
    return list;
  }();
  return catalog;
}
QJsonObject McpCatalog::tool(const QString &name) {
  for (const auto &value : tools())
    if (value.toObject()["name"] == name)
      return value.toObject();
  return {};
}
QString McpCatalog::validate(const QJsonValue &value, const QJsonObject &schema,
                             QString path) {
  const auto type = schema["type"].toString();
  if ((type == "object" && !value.isObject()) ||
      (type == "array" && !value.isArray()) ||
      (type == "string" && !value.isString()) ||
      (type == "boolean" && !value.isBool()) ||
      ((type == "number" || type == "integer") && !value.isDouble()))
    return path + ": expected " + type;
  if (schema.contains("enum") && !schema["enum"].toArray().contains(value))
    return path + ": unsupported value";
  if (value.isDouble()) {
    const auto n = value.toDouble();
    if (!std::isfinite(n) || (type == "integer" && std::floor(n) != n) ||
        (schema.contains("minimum") && n < schema["minimum"].toDouble()) ||
        (schema.contains("maximum") && n > schema["maximum"].toDouble()))
      return path + ": number outside allowed range";
  }
  if (value.isString()) {
    const auto n = value.toString().size();
    if ((schema.contains("minLength") && n < schema["minLength"].toInt()) ||
        (schema.contains("maxLength") && n > schema["maxLength"].toInt()))
      return path + ": invalid string length";
  }
  if (value.isObject()) {
    const auto obj = value.toObject(),
               properties = schema["properties"].toObject();
    for (const auto &required : schema["required"].toArray())
      if (!obj.contains(required.toString()))
        return path + "." + required.toString() + ": required";
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      if (!properties.contains(it.key()))
        return path + "." + it.key() + ": unknown parameter";
      const auto error = validate(it.value(), properties[it.key()].toObject(),
                                  path + "." + it.key());
      if (!error.isEmpty())
        return error;
    }
  }
  if (value.isArray()) {
    const auto items = value.toArray();
    if (schema.contains("maxItems") &&
        items.size() > schema["maxItems"].toInt())
      return path + ": too many items";
    for (int i = 0; i < items.size(); ++i) {
      const auto error = validate(items[i], schema["items"].toObject(),
                                  path + QString("[%1]").arg(i));
      if (!error.isEmpty())
        return error;
    }
  }
  return {};
}
QString McpCatalog::reference() {
  QString text = "# MCP tool reference\n\nGenerated from McpCatalog; input "
                 "validation uses these same schemas.\n\n";
  for (const auto &value : tools()) {
    const auto tool = value.toObject();
    text += "## " + tool["name"].toString() + " — " + tool["title"].toString() +
            "\n\n" + tool["description"].toString() + "\n\n```json\n" +
            QString::fromUtf8(
                QJsonDocument(tool["inputSchema"].toObject()).toJson()) +
            "```\n\n";
  }
  return text;
}
