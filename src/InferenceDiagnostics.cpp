#include "InferenceDiagnostics.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace {
QJsonObject select(const QJsonObject &source, const QStringList &keys) {
    QJsonObject out;
    for (const auto &key : keys) if (source.contains(key)) out[key] = source[key];
    return out;
}
}
InferenceDiagnostics::InferenceDiagnostics(DiagnosticLog *log) : m_log(log ? log : &DiagnosticLog::instance()) {}
void InferenceDiagnostics::event(QString level, QString name, QJsonObject data) {
    data["job"] = m_id;
    if (m_elapsed.isValid()) data["elapsedMs"] = m_elapsed.elapsed();
    m_log->record(level, name, data);
}
void InferenceDiagnostics::begin(QString kind, QString worker, bool remote, const QJsonObject &parameters) {
    m_id = QUuid::createUuid().toString(QUuid::Id128); m_active = true;
    m_pending.clear(); m_backends = {}; m_discardLine = false;
    m_stage.clear(); m_stageStart = 0; m_lastProgress = -1000; m_elapsed.start();
    const auto config = select(parameters, {"steps", "width", "height", "inferenceWidth", "inferenceHeight", "model", "modelSha256", "textEncoder", "editMethod", "flowSteps", "cfg", "frames", "fps", "durationSeconds", "imageQuant", "offloadToCpu", "vaeTiling"});
    event("info", "inference-start", {{"kind", kind}, {"worker", worker}, {"execution", remote ? "remote" : "local"}, {"parameters", config}, {"gpu", "unknown"}});
    stage("startup-and-loading");
}
void InferenceDiagnostics::stage(QString name) {
    if (name == m_stage) return;
    const auto now = m_elapsed.elapsed();
    if (!m_stage.isEmpty()) event("info", "stage-finished", {{"stage", m_stage}, {"wallMs", now - m_stageStart}});
    m_stage = name; m_stageStart = now;
    event("info", "stage-started", {{"stage", name}});
}
void InferenceDiagnostics::consume(const QByteArray &bytes) {
    if (!m_active) return;
    // Handle split UTF-8, CR-only progress and the last unterminated line.
    // A worker printing a huge prompt/line cannot grow the UI's diagnostic buffer.
    for (const char ch : bytes) {
        if (ch == '\r' || ch == '\n') {
            if (!m_discardLine && !m_pending.isEmpty()) line(m_pending);
            m_pending.clear(); m_discardLine = false;
        } else if (!m_discardLine) {
            m_pending += ch;
            if (m_pending.size() > 16384) {
                m_pending.clear(); m_discardLine = true;
                event("warning", "worker-line-omitted", {{"reason", "line exceeds 16 KiB"}});
            }
        }
    }
}
void InferenceDiagnostics::line(const QByteArray &bytes) {
    QString text = QString::fromUtf8(bytes).trimmed();
    text.remove(QRegularExpression("\x1b\\[[0-9;]*[A-Za-z]"));
    const auto json = QJsonDocument::fromJson(text.toUtf8()).object();
    const auto name = json["event"].toString();
    const bool error = name == "error" || QRegularExpression("\\b(error|failed|failure|exception|fatal|out of memory)\\b", QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
    const bool warning = QRegularExpression("\\b(warn|warning|fallback|falling back)\\b", QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
    if (name == "progress") {
        stage(json["stage"].toString("unknown"));
        if (m_elapsed.elapsed() - m_lastProgress >= 1000 || json["done"] == json["total"]) {
            event("info", "worker-progress", select(json, {"stage", "done", "total"}));
            m_lastProgress = m_elapsed.elapsed();
        }
        return;
    }
    QJsonObject backend;
    QString component = "default";
    if (name == "backend") {
        component = json["component"].toString("default");
        backend = select(json, {"backend", "device", "gpu", "evidence", "availableProviders", "threads", "phase", "loadSeconds", "runSeconds", "runs", "providerExecution", "gpuUtilization"});
        // Worker protocols must explicitly report GPU evidence; a provider list is insufficient.
        if (!backend.contains("gpu")) backend["gpu"] = "unknown";
    } else if (!error && !warning) {
        static const QRegularExpression used(R"(Using\s+(CPU|CUDA\d*|Metal|Vulkan\d*|SYCL\d*|ROCm\d*)\s+backend)", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression compute(R"((diffusion|vae|tae|text_encoder|te|llm)\s*(?:using|:)?\s*(?:compute\s+)?backend\s*[:=]\s*(CPU|CUDA\d*|Metal|Vulkan\d*|SYCL\d*|ROCm\d*))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression buffer(R"((\S+) compute buffer size:.*\((RAM|VRAM)\) on (\S+))", QRegularExpression::CaseInsensitiveOption);
        const auto module = compute.match(text), generic = used.match(text), allocated = buffer.match(text);
        QString selected;
        if (module.hasMatch()) { component = module.captured(1); selected = module.captured(2); }
        else if (generic.hasMatch()) selected = generic.captured(1);
        if (!selected.isEmpty()) backend = {{"backend", selected}, {"gpu", selected.compare("CPU", Qt::CaseInsensitive) == 0 ? "no" : "yes"}, {"evidence", "worker-selected-backend"}};
        else if (allocated.hasMatch()) {
            component=allocated.captured(1)+":"+allocated.captured(3);
            backend={{"backend",allocated.captured(3)},{"gpu",allocated.captured(2).compare("VRAM",Qt::CaseInsensitive)==0?"yes":"no"},{"evidence","worker-compute-buffer"}};
        }
        else if (text.startsWith("[inference]") && text.contains("CoreML"))
            backend = {{"backend", "CoreML"}, {"gpu", "unknown"}, {"evidence", "provider-configured; per-operation placement not measured"}};
    }
    if (!backend.isEmpty()) {
        m_backends[component] = backend;
        backend["component"] = component;
        event("info", "backend-selected", backend);
    }
    if (name == "error") { event("error", "worker-error", {{"message", json["message"]}}); return; }
    if (name == "backend-unavailable") { event("warning", "backend-unavailable", select(json,{"backend","message","fallback"})); return; }
    if (!name.isEmpty()) {
        event("info", "worker-event", select(json, {"event", "stage", "durationSeconds", "sampleRate", "engine", "route", "backend", "device", "deviceId", "gpu", "component", "availableProviders", "evidence", "message", "fallback", "gpuOperations", "aneOperations", "cpuOrUnknownOperations"}));
        return;
    }
    if (text.contains("generating image", Qt::CaseInsensitive)) stage("sampling");
    if (text.contains("decoding", Qt::CaseInsensitive)) stage("decoding");
    // Keep hardware, memory, timing and errors, not arbitrary stdout or prompt dumps.
    static const QRegularExpression diagnostic(R"(backend|device|CUDA|Vulkan|Metal|CoreML|CPU|GPU|inference|memory|VRAM|alloc|loading|loaded|load_s=|taking\s|elapsed|\btime\b|timing|\bfps\b|[ms]s/it|it/s|_ms=|driver)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression input(R"(prompt|reference[_ ]?text|^\s*(text|description)\s*[:=])", QRegularExpression::CaseInsensitiveOption);
    if (!input.match(text).hasMatch() && (error || warning || diagnostic.match(text).hasMatch()))
        event(error ? "error" : warning ? "warning" : "info", "worker-log", {{"message", text}});
}
QJsonObject InferenceDiagnostics::summary() const {
    bool cpu = false, gpu = false, unknown = false;
    for (const auto &item : m_backends) {
        const auto state = item.toObject()["gpu"].toString();
        cpu |= state == "no" || state == "mixed"; gpu |= state == "yes" || state == "mixed"; unknown |= state == "unknown";
    }
    return {{"gpu", gpu ? (cpu ? "mixed" : "yes") : cpu && !unknown ? "no" : "unknown"}, {"components", m_backends}, {"gpuUtilization", "not_measured"}};
}
void InferenceDiagnostics::finish(bool success, bool cancelled, int exitCode, bool crashed, QString error, const QJsonObject &result) {
    if (!m_active) return;
    if (!m_discardLine && !m_pending.isEmpty()) line(m_pending);
    m_pending.clear();
    const auto now = m_elapsed.elapsed();
    event("info", "stage-finished", {{"stage", m_stage}, {"wallMs", now - m_stageStart}});
    auto data = summary();
    data["success"] = success; data["cancelled"] = cancelled; data["exitCode"] = exitCode;
    data["crashed"] = crashed; data["error"] = error;
    data["performance"] = select(result, {"elapsedSeconds", "timingsSeconds", "timingsOverlap", "motionTimingsSeconds", "denoiseSubtimingsSeconds", "firstFrameSeconds", "frames", "fpsNum", "fpsDen", "durationSeconds", "motionCacheReused"});
    event(success || cancelled ? "info" : "error", "inference-finished", data);
    m_active = false;
}
void InferenceDiagnostics::processError(int code, QString message) {
    if (m_active) event("error", "process-error", {{"code", code}, {"message", message}});
}
void InferenceDiagnostics::heartbeat() {
    if (m_active) { auto data = summary(); data["stage"] = m_stage; data["stageElapsedMs"] = m_elapsed.elapsed() - m_stageStart; event("info", "inference-heartbeat", data); }
}
