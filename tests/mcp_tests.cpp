#include "McpCatalog.h"
#include "McpService.h"
#include "Studio.h"
#include "WorkspaceLibrary.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class McpTests : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
  struct HttpResult {
    int status;
    QByteArray body, range, type;
  };
  static HttpResult http(QString url, QByteArray method, QByteArray data = {},
                         QMap<QByteArray, QByteArray> headers = {}) {
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest req{QUrl(url)};
    for (auto it = headers.begin(); it != headers.end(); ++it)
      req.setRawHeader(it.key(), it.value());
    auto reply = network.sendCustomRequest(req, method, data);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return {reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
            reply->readAll(), reply->rawHeader("Content-Range"),
            reply->rawHeader("Content-Type")};
  }
  static QJsonObject call(McpService &service, QString name,
                          QJsonObject arguments = {}) {
    return service
        .dispatch({{"jsonrpc", "2.0"},
                   {"id", 1},
                   {"method", "tools/call"},
                   {"params", QJsonObject{{"name", name},
                                          {"arguments", arguments}}}})["result"]
        .toObject()["structuredContent"]
        .toObject();
  }
  static void wave(QString path) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    f.write("RIFF");
    out << quint32(36 + 16000 * 2 * 4);
    f.write("WAVEfmt ");
    out << quint32(16) << quint16(1) << quint16(1) << quint32(16000)
        << quint32(32000) << quint16(2) << quint16(16);
    f.write("data");
    out << quint32(16000 * 2 * 4);
    f.write(QByteArray(16000 * 2 * 4, 0));
  }
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("MossMcpTests");
    QCoreApplication::setApplicationName("MossMcpTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settings.path());
  }
  void catalogValidatesNestedInputsAndRejectsUnknownFields() {
    QSet<QString> names;
    for (const auto &v : McpCatalog::tools()) {
      const auto t = v.toObject();
      QVERIFY(!t["description"].toString().isEmpty());
      QVERIFY(!names.contains(t["name"].toString()));
      names.insert(t["name"].toString());
    }
    QVERIFY(names.size() > 50);
    auto schema = McpCatalog::tool("mask_stroke")["inputSchema"].toObject();
    QVERIFY(!McpCatalog::validate(
                 QJsonObject{
                     {"workId", "work"},
                     {"diameter", 10},
                     {"points", QJsonArray{QJsonObject{{"x", 0}, {"y", -1}}}}},
                 schema)
                 .isEmpty());
    QVERIFY(
        !McpCatalog::validate(
             QJsonObject{
                 {"workId", "work"}, {"width", 300}, {"surprise", true}},
             McpCatalog::tool("generation_settings")["inputSchema"].toObject())
             .isEmpty());
  }
  void disabledByDefaultPersistsOptInAndStopsWithOwner() {
    QTemporaryDir root;
    qputenv("MOSS_OUTPUT_ROOT", root.path().toUtf8());
    QString url;
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(!server.enabled());
      QVERIFY(!server.running());
      server.setEnabled(true);
      QVERIFY2(server.running(), qPrintable(server.error()));
      url = server.url();
      QVERIFY(server.configuration().contains("Authorization"));
      server.shutdown();
      QVERIFY(!server.running());
    }
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(server.running());
      QCOMPARE(server.url(), url);
      server.setEnabled(false);
      QVERIFY(!server.running());
      QVERIFY(server.configuration().isEmpty());
    }
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(!server.enabled());
      QVERIFY(!server.running());
    }
  }
  void realHttpAuthenticationOriginAndDiscovery() {
    QTemporaryDir root;
    qputenv("MOSS_OUTPUT_ROOT", root.path().toUtf8());
    Studio studio;
    WorkspaceLibrary library(root.path());
    McpService server(&studio, &library);
    server.setEnabled(true);
    QVERIFY(server.running());
    const auto config = QJsonDocument::fromJson(server.configuration().toUtf8())
                            .object()["mcpServers"]
                            .toObject()["xinshen"]
                            .toObject();
    QNetworkAccessManager network;
    auto request = [&](QJsonObject body, bool auth, QString origin = {}) {
      QNetworkRequest req(QUrl(server.url()));
      req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
      req.setRawHeader("Accept", "application/json, text/event-stream");
      if (auth)
        req.setRawHeader(
            "Authorization",
            config["headers"].toObject()["Authorization"].toString().toUtf8());
      if (!origin.isEmpty())
        req.setRawHeader("Origin", origin.toUtf8());
      auto reply = network.post(req, QJsonDocument(body).toJson());
      QEventLoop loop;
      connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
      QTimer::singleShot(5000, &loop, &QEventLoop::quit);
      loop.exec();
      const auto result = qMakePair(
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
          QJsonDocument::fromJson(reply->readAll()).object());
      reply->deleteLater();
      return result;
    };
    const QJsonObject list{
        {"jsonrpc", "2.0"}, {"id", 5}, {"method", "tools/list"}};
    QCOMPARE(request(list, false).first, 401);
    QCOMPARE(request(list, true, "https://evil.example").first, 403);
    auto init =
        request({{"jsonrpc", "2.0"},
                 {"id", 1},
                 {"method", "initialize"},
                 {"params",
                  QJsonObject{{"protocolVersion", "2025-11-25"},
                              {"capabilities", QJsonObject()},
                              {"clientInfo", QJsonObject{{"name", "test"},
                                                         {"version", "1"}}}}}},
                true);
    QCOMPARE(init.first, 200);
    QCOMPARE(init.second["result"].toObject()["protocolVersion"].toString(),
             QString("2025-11-25"));
    QCOMPARE(
        request({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}},
                true)
            .first,
        202);
    const auto tools = request(list, true);
    QCOMPARE(tools.first, 200);
    QCOMPARE(tools.second["result"].toObject()["tools"].toArray().size(),
             server.toolCount());
    const auto before = studio.creationPages().size();
    request({{"jsonrpc", "2.0"},
             {"method", "tools/call"},
             {"params", QJsonObject{{"name", "work_create"},
                                    {"arguments", QJsonObject()}}}},
            true);
    QCOMPARE(studio.creationPages().size(), before);
    QVERIFY(server.calls()->rowCount() >= 2);
    server.setEnabled(false);
    QVERIFY(server.calls()->rowCount() >= 2);
  }
  void workLayoutMaskAndResourceOperations() {
    QTemporaryDir root;
    qputenv("MOSS_OUTPUT_ROOT", root.path().toUtf8());
    Studio studio;
    WorkspaceLibrary library(root.path());
    McpService server(&studio, &library);
    server.setEnabled(true);
    const auto work =
        call(server, "work_create", {{"name", "MCP test"}})["data"]
            .toObject()["workId"]
            .toString();
    QVERIFY(!work.isEmpty());
    QVERIFY(call(server, "generation_settings",
                 {{"workId", work},
                  {"width", 512},
                  {"height", 512},
                  {"steps", 4}})["ok"]
                .toBool());
    QVERIFY(!call(server, "generation_settings",
                  {{"workId", work}, {"width", 513}})["ok"]
                 .toBool());
    QCOMPARE(studio.document()->canvasWidth(), 512);
    auto added = call(server, "region_add",
                      {{"workId", work},
                       {"x", 10},
                       {"y", 20},
                       {"width", 100},
                       {"height", 80},
                       {"caption", "a blue cube"}});
    QVERIFY(added["ok"].toBool());
    const auto region =
        studio.document()->regions().first().toMap()["id"].toString();
    QVERIFY(call(server, "region_update",
                 {{"workId", work}, {"regionId", region}, {"x", 32}})["ok"]
                .toBool());
    QCOMPARE(studio.document()->regions().first().toMap()["x"].toDouble(), 32.);
    QVERIFY(
        call(server, "edit_history",
             {{"workId", work}, {"target", "layout"}, {"action", "undo"}})["ok"]
            .toBool());
    QCOMPARE(studio.document()->regions().first().toMap()["x"].toDouble(), 10.);
    QImage image(128, 128, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const auto path = root.filePath("portrait.png");
    QVERIFY(image.save(path));
    auto imported =
        call(server, "asset_import",
             {{"workId", work}, {"path", path}, {"purpose", "target"}});
    QVERIFY(imported["ok"].toBool());
    QVERIFY(call(server, "edit_settings",
                 {{"workId", work}, {"scope", "local"}})["ok"]
                .toBool());
    QVERIFY(call(server, "mask_rectangle",
                 {{"workId", work},
                  {"x", 10},
                  {"y", 10},
                  {"width", 30},
                  {"height", 30}})["ok"]
                .toBool());
    QVERIFY(studio.editDocument()->hasMask());
    QVERIFY(call(server, "mask_action",
                 {{"workId", work}, {"action", "clear"}})["ok"]
                .toBool());
    QVERIFY(!studio.editDocument()->hasMask());
    QVERIFY(call(server, "avatar_import",
                 {{"path", path}, {"name", "Imported"}})["ok"]
                .toBool());
    const auto avatar = library.avatars().first().toMap()["id"].toString();
    QVERIFY(
        call(server, "avatar_remove", {{"avatarId", avatar}})["ok"].toBool());
    QVERIFY(library.avatars().isEmpty());
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(call(server, "avatar_restore")["ok"].toBool());
    QCOMPARE(library.avatars().size(), 1);
    const auto wav = root.filePath("voice.wav");
    wave(wav);
    QVERIFY(call(server, "voice_import",
                 {{"path", wav},
                  {"name", "Voice"},
                  {"transcript", "Test reference speech"}})["ok"]
                .toBool());
    const auto voice = studio.sound()->selectedVoice()["id"].toString();
    QVERIFY(call(server, "voice_remove", {{"voiceId", voice}})["ok"].toBool());
    QVERIFY(QFileInfo::exists(wav));
    QVERIFY(call(server, "voice_restore")["ok"].toBool());
    QVERIFY(call(server, "sfx_import", {{"workId", work}, {"path", wav}})["ok"]
                .toBool());
    auto effects = call(server, "sfx_list", {{"workId", work}})["data"]
                       .toObject()["effects"]
                       .toArray();
    QCOMPARE(effects.size(), 1);
    QVERIFY(call(server, "sfx_remove",
                 {{"workId", work},
                  {"messageId", effects[0].toObject()["messageId"]},
                  {"cleanup", true}})["ok"]
                .toBool());
    QVERIFY(QFileInfo::exists(wav));
    QVERIFY(!call(server, "model_remove", {{"group", "../outside"}})["ok"]
                 .toBool());
    QVERIFY(!call(server, "model_accept_terms",
                  {{"version", "2026-10-22"}, {"userAccepted", false}})["ok"]
                 .toBool());
  }
  void jobsAreIdempotentCancellableAndDurable() {
    QTemporaryDir root, models;
    qputenv("MOSS_OUTPUT_ROOT", root.path().toUtf8());
    qputenv("MOSS_MODEL_ROOT", models.path().toUtf8());
    QString failed, cancelled, interrupted;
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      server.setEnabled(true);
      auto work = studio.conversation()->id();
      QJsonObject args{{"workId", work},
                       {"task", "image"},
                       {"prompt", "blue cube"},
                       {"requestKey", "one"}};
      const auto first = call(server, "generation_start", args);
      QVERIFY2(first["ok"].toBool(),
               qPrintable(QString::fromUtf8(QJsonDocument(first).toJson())));
      failed = first["data"].toObject()["jobId"].toString();
      QCOMPARE(call(server, "generation_start", args)["data"]
                   .toObject()["jobId"]
                   .toString(),
               failed);
      args["prompt"] = "different";
      QCOMPARE(call(server, "generation_start", args)["error"]
                   .toObject()["code"]
                   .toString(),
               QString("IDEMPOTENCY_CONFLICT"));
      QTRY_COMPARE(call(server, "job_get", {{"jobId", failed}})["data"]
                       .toObject()["status"]
                       .toString(),
                   QString("failed"));
      args["requestKey"] = "two";
      cancelled = call(server, "generation_start", args)["data"]
                      .toObject()["jobId"]
                      .toString();
      QVERIFY(!cancelled.isEmpty());
      QCOMPARE(call(server, "job_cancel", {{"jobId", cancelled}})["data"]
                   .toObject()["status"]
                   .toString(),
               QString("cancelled"));
      args["requestKey"] = "three";
      interrupted = call(server, "generation_start", args)["data"]
                        .toObject()["jobId"]
                        .toString();
      server.shutdown();
    }
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(server.running());
      QCOMPARE(call(server, "job_get", {{"jobId", failed}})["data"]
                   .toObject()["status"]
                   .toString(),
               QString("failed"));
      QCOMPARE(call(server, "job_get", {{"jobId", cancelled}})["data"]
                   .toObject()["status"]
                   .toString(),
               QString("cancelled"));
      QCOMPARE(call(server, "job_get", {{"jobId", interrupted}})["data"]
                   .toObject()["status"]
                   .toString(),
               QString("interrupted"));
    }
    qunsetenv("MOSS_MODEL_ROOT");
  }
  void namedResourcesCopyIntoLibraryAndSurviveRestart() {
    QTemporaryDir root;
    qputenv("MOSS_OUTPUT_ROOT", root.path().toUtf8());
    QString avatarId, voiceId, avatarJob, voiceJob, portraitCopy, voiceCopy;
    const auto portrait = root.filePath("source.png"),
               wav = root.filePath("source.wav");
    QImage image(128, 128, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(portrait));
    wave(wav);
    QJsonObject createAvatar;
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      server.setEnabled(true);
      const auto work = studio.conversation()->id();
      const auto imageAsset =
          call(server, "asset_import",
               {{"workId", work}, {"path", portrait}})["data"]
              .toObject()["assetId"]
              .toString();
      const auto audioAsset = call(server, "asset_import",
                                   {{"workId", work}, {"path", wav}})["data"]
                                  .toObject()["assetId"]
                                  .toString();
      QVERIFY(!imageAsset.isEmpty());
      QVERIFY(!audioAsset.isEmpty());
      createAvatar = {
          {"name", "My portrait"},
          {"source", QJsonObject{{"workId", work}, {"assetId", imageAsset}}},
          {"requestKey", "create-portrait"}};
      auto invalid = createAvatar;
      invalid["description"] = "Conflicting second source";
      QVERIFY(!call(server, "avatar_create", invalid)["ok"].toBool());
      avatarJob = call(server, "avatar_create", createAvatar)["data"]
                      .toObject()["jobId"]
                      .toString();
      QVERIFY(!avatarJob.isEmpty());
      QJsonObject createVoice{
          {"name", "My voice"},
          {"source", QJsonObject{{"workId", work}, {"assetId", audioAsset}}},
          {"transcript", "The exact reference speech"},
          {"requestKey", "create-voice"}};
      voiceJob = call(server, "voice_create", createVoice)["data"]
                     .toObject()["jobId"]
                     .toString();
      QVERIFY(!voiceJob.isEmpty());
      QTRY_COMPARE(call(server, "job_get", {{"jobId", avatarJob}})["data"]
                       .toObject()["status"]
                       .toString(),
                   QString("completed"));
      QTRY_COMPARE(call(server, "job_get", {{"jobId", voiceJob}})["data"]
                       .toObject()["status"]
                       .toString(),
                   QString("completed"));
      const auto avatar =
          call(server, "job_result", {{"jobId", avatarJob}})["data"]
              .toObject()["result"]
              .toObject();
      const auto voice =
          call(server, "job_result", {{"jobId", voiceJob}})["data"]
              .toObject()["result"]
              .toObject();
      avatarId = avatar["avatarId"].toString();
      voiceId = voice["voiceId"].toString();
      QVERIFY(!avatarId.isEmpty());
      QVERIFY(!voiceId.isEmpty());
      portraitCopy =
          QUrl(avatar["avatar"].toObject()["url"].toString()).toLocalFile();
      voiceCopy = voice["voice"].toObject()["reference"].toString();
      QVERIFY(portraitCopy != portrait);
      QVERIFY(QFileInfo::exists(portraitCopy));
      QVERIFY(voiceCopy != wav);
      QVERIFY(QFileInfo::exists(voiceCopy));
      QCOMPARE(call(server, "avatar_create", createAvatar)["data"]
                   .toObject()["jobId"]
                   .toString(),
               avatarJob);
      QCOMPARE(library.avatars().size(), 1);
      QVERIFY(call(server, "asset_import",
                   {{"workId", work},
                    {"avatarId", avatarId},
                    {"purpose", "target"}})["ok"]
                  .toBool());
      QVERIFY(call(server, "asset_import",
                   {{"workId", work},
                    {"source", QJsonObject{{"workId", work},
                                           {"assetId", audioAsset}}}})["ok"]
                  .toBool());
      studio.flushWorkspace();
    }
    QVERIFY(QFile::remove(portrait));
    QVERIFY(QFile::remove(wav));
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(server.running());
      QCOMPARE(library.avatars().first().toMap()["id"].toString(), avatarId);
      QVERIFY(QFileInfo::exists(portraitCopy));
      bool voiceFound = false;
      for (const auto &v : studio.sound()->voices()) {
        const auto row = v.toMap();
        if (row["id"].toString() == voiceId) {
          voiceFound = true;
          QVERIFY(QFileInfo::exists(row["reference"].toString()));
        }
      }
      QVERIFY(voiceFound);
      QCOMPARE(call(server, "avatar_create", createAvatar)["data"]
                   .toObject()["jobId"]
                   .toString(),
               avatarJob);
      QCOMPARE(call(server, "job_result", {{"jobId", voiceJob}})["data"]
                   .toObject()["result"]
                   .toObject()["voiceId"]
                   .toString(),
               voiceId);
    }
  }
  void pollingRetainsActiveCall() {
    McpCallModel model;
    const auto active = model.append({{"name", "generation_start"}});
    model.update(active, {{"state", "queued"}});
    for (int i = 0; i < 250; ++i) {
      const auto poll = model.append({{"name", "job_get"}});
      model.update(poll, {{"state", "completed"}});
    }
    QCOMPARE(model.rowCount(), 200);
    QVERIFY(!model.get(active).isEmpty());
  }
  void lanOptInKeepsAuthenticationAndPersists() {
    QVERIFY(McpHttpServer::isLanAddress(QHostAddress("10.2.3.4")));
    QVERIFY(McpHttpServer::isLanAddress(QHostAddress("172.31.0.2")));
    QVERIFY(!McpHttpServer::isLanAddress(QHostAddress("172.32.0.2")));
    QVERIFY(!McpHttpServer::isLanAddress(QHostAddress("8.8.8.8")));
    QVERIFY(!McpHttpServer::isLanAddress(QHostAddress("127.0.0.1")));
    const auto addresses = McpHttpServer::lanAddresses();
    if (addresses.isEmpty())
      QSKIP("No IPv4 LAN interface on this runner");
    QTemporaryDir root;
    qputenv("MOSS_OUTPUT_ROOT", root.path().toUtf8());
    QString endpoint;
    const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(!server.lanEnabled());
      server.setEnabled(true);
      const auto port = QUrl(server.url()).port();
      const auto lan =
          QString("http://%1:%2/mcp").arg(addresses.first()).arg(port);
      const auto cfg = QJsonDocument::fromJson(server.configuration().toUtf8())
                           .object()["mcpServers"]
                           .toObject()["xinshen"]
                           .toObject();
      QMap<QByteArray, QByteArray> headers{
          {"Content-Type", "application/json"},
          {"Authorization",
           cfg["headers"].toObject()["Authorization"].toString().toUtf8()}};
      // Another app may already own this port on the LAN interface. It must
      // never accept this service's credential while LAN access is disabled.
      QVERIFY(http(lan, "POST", body, headers).status != 200);
      server.setLanEnabled(true);
      QVERIFY(server.running());
      QVERIFY(server.lanEnabled());
      endpoint = server.url();
      QCOMPARE(http(endpoint, "POST", body).status, 401);
      QCOMPARE(http(endpoint, "POST", body, headers).status, 200);
      headers["Origin"] = "https://evil.example";
      QCOMPARE(http(endpoint, "POST", body, headers).status, 403);
      headers.remove("Origin");
      headers["Host"] =
          "evil.example:" + QByteArray::number(QUrl(endpoint).port());
      QCOMPARE(http(endpoint, "POST", body, headers).status, 403);
      headers.remove("Host");
      server.setLanEnabled(false);
      QVERIFY(server.running());
      QVERIFY(server.url().contains("127.0.0.1"));
      QVERIFY(http(endpoint, "POST", body, headers).status != 200);
      server.setLanEnabled(true);
      QVERIFY(server.connectionText(true).contains("LAN endpoint"));
      QVERIFY(server.connectionText().contains("media_upload_begin"));
    }
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(server.running());
      QVERIFY(server.lanEnabled());
      QCOMPARE(server.url(), endpoint);
    }
  }
  void remoteUploadsPreviewsRangesAndDurability() {
    QTemporaryDir root;
    qputenv("MOSS_OUTPUT_ROOT", root.path().toUtf8());
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(&buffer, "PNG"));
    const auto sha = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    QString upload, resumable, sourceWork, assetId, mediaUrl;
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      server.setEnabled(true);
      QVERIFY(
          !call(server, "media_upload_begin",
                {{"filename", "../outside.png"}, {"size", bytes.size()}})["ok"]
               .toBool());
      upload = call(server, "media_upload_begin",
                    {{"filename", "远程图片.png"},
                     {"size", bytes.size()},
                     {"sha256", sha}})["data"]
                   .toObject()["uploadId"]
                   .toString();
      QVERIFY(!upload.isEmpty());
      QCOMPARE(
          call(server, "media_upload_status", {{"uploadId", upload}})["data"]
              .toObject()["received"]
              .toInteger(),
          0);
      QVERIFY(
          !call(server, "media_upload_status", {{"uploadId", "missing"}})["ok"]
               .toBool());
      QVERIFY(!call(server, "media_upload_finish", {{"uploadId", upload}})["ok"]
                   .toBool());
      QVERIFY(!call(server, "media_upload_chunk",
                    {{"uploadId", upload},
                     {"offset", bytes.size() + 1},
                     {"dataBase64", "YWJj"}})["ok"]
                   .toBool());
      for (int offset = 0; offset < bytes.size(); offset += 64) {
        const QJsonObject chunk{
            {"uploadId", upload},
            {"offset", offset},
            {"dataBase64",
             QString::fromLatin1(bytes.mid(offset, 64).toBase64())}};
        QVERIFY(call(server, "media_upload_chunk", chunk)["ok"].toBool());
        QVERIFY(call(server, "media_upload_chunk", chunk)["ok"].toBool());
      }
      QVERIFY(!call(server, "media_upload_chunk",
                    {{"uploadId", upload},
                     {"offset", 0},
                     {"dataBase64", "YWJj"}})["ok"]
                   .toBool());
      auto finished =
          call(server, "media_upload_finish", {{"uploadId", upload}});
      QVERIFY(finished["ok"].toBool());
      mediaUrl = finished["data"]
                     .toObject()["media"]
                     .toObject()["downloadUrl"]
                     .toString();
      QVERIFY(!mediaUrl.isEmpty());
      const auto full =
          http(mediaUrl, "GET", {}, {{"Origin", "https://agent.example"}});
      QCOMPARE(full.status, 200);
      QCOMPARE(full.body, bytes);
      QCOMPARE(full.type, QByteArray("image/png"));
      const auto range = http(mediaUrl, "GET", {}, {{"Range", "bytes=8-31"}});
      QCOMPARE(range.status, 206);
      QCOMPARE(range.body, bytes.mid(8, 24));
      QCOMPARE(range.range, "bytes 8-31/" + QByteArray::number(bytes.size()));
      QCOMPARE(http(mediaUrl, "GET", {}, {{"Range", "bytes=-12"}}).body,
               bytes.right(12));
      QCOMPARE(http(mediaUrl, "GET", {}, {{"Range", "bytes=999999-"}}).status,
               416);
      QCOMPARE(http(mediaUrl, "GET", {}, {{"Range", "bytes=0-2,5-7"}}).status,
               416);
      QCOMPARE(http(mediaUrl, "HEAD").body.size(), 0);
      QCOMPARE(http(mediaUrl + "bad", "GET").status, 404);
      const auto preview =
          server
              .dispatch({{"jsonrpc", "2.0"},
                         {"id", 4},
                         {"method", "tools/call"},
                         {"params",
                          QJsonObject{
                              {"name", "media_preview"},
                              {"arguments",
                               QJsonObject{{"uploadId", upload}}}}}})["result"]
              .toObject()["content"]
              .toArray();
      QCOMPARE(preview.last().toObject()["type"].toString(), QString("image"));
      QVERIFY(!QImage::fromData(
                   QByteArray::fromBase64(
                       preview.last().toObject()["data"].toString().toLatin1()))
                   .isNull());
      sourceWork = studio.conversation()->id();
      auto imported = call(server, "asset_import",
                           {{"workId", sourceWork},
                            {"uploadId", upload},
                            {"purpose", "target"}});
      QVERIFY(imported["ok"].toBool());
      assetId = imported["data"].toObject()["assetId"].toString();
      auto job = call(server, "avatar_create",
                      {{"uploadId", upload},
                       {"name", "远程形象"},
                       {"requestKey", "upload-avatar"}})["data"]
                     .toObject()["jobId"]
                     .toString();
      QVERIFY(!job.isEmpty());
      QVERIFY(
          !call(server, "media_upload_discard", {{"uploadId", upload}})["ok"]
               .toBool());
      QTRY_COMPARE(call(server, "job_get", {{"jobId", job}})["data"]
                       .toObject()["status"]
                       .toString(),
                   QString("completed"));
      const auto read =
          call(server, "media_read",
               {{"source", QJsonObject{{"workId", sourceWork},
                                       {"assetId", assetId}}}})["data"]
              .toObject();
      QCOMPARE(QByteArray::fromBase64(read["dataBase64"].toString().toLatin1()),
               bytes);
      QVERIFY(read["eof"].toBool());
      resumable = call(server, "media_upload_begin",
                       {{"filename", "resume.png"},
                        {"size", bytes.size()},
                        {"sha256", sha}})["data"]
                      .toObject()["uploadId"]
                      .toString();
      QVERIFY(call(server, "media_upload_chunk",
                   {{"uploadId", resumable},
                    {"offset", 0},
                    {"dataBase64",
                     QString::fromLatin1(bytes.left(16).toBase64())}})["ok"]
                  .toBool());
      QCOMPARE(call(server, "media_upload_status")["data"]
                   .toObject()["uploads"]
                   .toArray()
                   .size(),
               2);
      studio.flushWorkspace();
    }
    {
      Studio studio;
      WorkspaceLibrary library(root.path());
      McpService server(&studio, &library);
      QVERIFY(server.running());
      const auto resumed =
          call(server, "media_upload_status", {{"uploadId", resumable}})["data"]
              .toObject();
      QCOMPARE(resumed["received"].toInteger(), 16);
      QVERIFY(!resumed["complete"].toBool());
      QVERIFY(call(server, "media_upload_chunk",
                   {{"uploadId", resumable},
                    {"offset", resumed["received"]},
                    {"dataBase64",
                     QString::fromLatin1(bytes.mid(16).toBase64())}})["ok"]
                  .toBool());
      QVERIFY(
          call(server, "media_upload_finish", {{"uploadId", resumable}})["ok"]
              .toBool());
      QVERIFY(
          call(server, "media_upload_status", {{"uploadId", resumable}})["data"]
              .toObject()["complete"]
              .toBool());
      QVERIFY(
          call(server, "media_upload_discard", {{"uploadId", resumable}})["ok"]
              .toBool());
      QCOMPARE(http(mediaUrl, "GET").status,
               404); // Capabilities expire with the app session.
      QVERIFY(call(server, "media_upload_finish", {{"uploadId", upload}})["ok"]
                  .toBool());
      QVERIFY(call(server, "media_upload_discard", {{"uploadId", upload}})["ok"]
                  .toBool());
      QVERIFY(call(server, "media_read",
                   {{"source", QJsonObject{{"workId", sourceWork},
                                           {"assetId", assetId}}}})["ok"]
                  .toBool());
      QCOMPARE(library.avatars().size(), 1);
      QVERIFY(QFileInfo::exists(
          QUrl(library.avatars().first().toMap()["url"].toString())
              .toLocalFile()));
      QVERIFY(
          !call(server, "media_read", {{"uploadId", upload}})["ok"].toBool());
      const auto badHash = call(server, "media_upload_begin",
                                {{"filename", "hash.png"},
                                 {"size", bytes.size()},
                                 {"sha256", QString(64, '0')}})["data"]
                               .toObject()["uploadId"]
                               .toString();
      QVERIFY(
          call(server, "media_upload_chunk",
               {{"uploadId", badHash},
                {"offset", 0},
                {"dataBase64", QString::fromLatin1(bytes.toBase64())}})["ok"]
              .toBool());
      QVERIFY(
          !call(server, "media_upload_finish", {{"uploadId", badHash}})["ok"]
               .toBool());
      QVERIFY(
          !call(server, "media_read", {{"uploadId", badHash}})["ok"].toBool());
      QVERIFY(
          call(server, "media_upload_discard", {{"uploadId", badHash}})["ok"]
              .toBool());
    }
  }
};
QTEST_MAIN(McpTests)
#include "mcp_tests.moc"
