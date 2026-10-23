#include "McpMediaStore.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUrl>
#include <QUuid>

namespace {
constexpr qint64 maxFile = 2LL * 1024 * 1024 * 1024;
constexpr int chunkSize = 1024 * 1024;
QJsonObject ok(QJsonObject data) { return {{"ok", true}, {"data", data}}; }
QJsonObject fail(QString message) {
  return {
      {"ok", false},
      {"error", QJsonObject{{"code", "MEDIA_ERROR"}, {"message", message}}}};
}
bool save(QString path, QJsonObject object) {
  QSaveFile f(path);
  const auto data = QJsonDocument(object).toJson();
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  return f.write(data) == data.size() && f.commit();
}
} // namespace
QString McpMediaStore::mimeType(QString path) {
  const auto type =
      QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent).name();
  // Qt's content-only detection can classify MP4 as its QuickTime parent.
  // Keep content validation, while sending canonical browser media types.
  const auto extension = QFileInfo(path).suffix().toLower();
  if ((type == "video/quicktime" || type == "video/mp4" ||
       type == "audio/mp4") &&
      (extension == "mp4" || extension == "m4v" || extension == "m4a"))
    return extension == "m4a" ? "audio/mp4" : "video/mp4";
  if (type == "audio/vnd.wave" || type == "audio/x-wav")
    return "audio/wav";
  return type;
}
QString McpMediaStore::directory(QString id) const {
  static const QRegularExpression pattern("^[a-f0-9]{32}$");
  return pattern.match(id).hasMatch() ? m_root + "/" + id : QString();
}
QJsonObject McpMediaStore::manifest(QString id) const {
  const auto dir = directory(id);
  if (dir.isEmpty() || QFileInfo(dir).isSymLink())
    return {};
  QFile f(dir + "/upload.json");
  return f.open(QIODevice::ReadOnly)
             ? QJsonDocument::fromJson(f.readAll()).object()
             : QJsonObject();
}
QJsonObject McpMediaStore::uploadInfo(QString id,
                                      const QJsonObject &meta) const {
  const auto path = directory(id) + "/" + meta["file"].toString();
  return {{"uploadId", id},
          {"filename", meta["filename"]},
          {"size", meta["size"]},
          {"received", QFileInfo(path).size()},
          {"complete", meta["complete"].toBool()},
          {"chunkBytes", chunkSize},
          {"mimeType", meta["mimeType"]}};
}
QJsonObject McpMediaStore::begin(const QJsonObject &args) {
  const auto name = args["filename"].toString();
  static const QRegularExpression invalid("[\\x00-\\x1f/\\\\]");
  const auto size = args["size"].toInteger();
  const auto sha = args["sha256"].toString();
  static const QRegularExpression shaPattern("^[a-fA-F0-9]{64}$");
  const QStringList extensions{"png",  "jpg",  "jpeg", "webp", "gif", "bmp",
                               "tif",  "tiff", "wav",  "mp3",  "m4a", "aac",
                               "flac", "ogg",  "opus", "mp4",  "mov", "m4v",
                               "webm", "mkv",  "avi"};
  const auto extension = QFileInfo(name).suffix().toLower();
  if (name.isEmpty() || name.size() > 200 || invalid.match(name).hasMatch() ||
      !extensions.contains(extension))
    return fail("Use a media filename without directories (PNG/JPEG/WebP, "
                "WAV/MP3, MP4/MOV, etc.)");
  if (size <= 0 || size > maxFile ||
      (!sha.isEmpty() && !shaPattern.match(sha).hasMatch()))
    return fail("Size must be 1 byte to 2 GiB; sha256, when given, must "
                "contain 64 hex characters");
  if (!QDir().mkpath(m_root))
    return fail("Cannot create upload storage");
  QFile::setPermissions(m_root,
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  qint64 reserved = 0;
  int pending = 0;
  for (const auto &id :
       QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const auto meta = manifest(id);
    if (!meta.isEmpty() && !meta["complete"].toBool()) {
      ++pending;
      reserved +=
          std::max<qint64>(0, meta["size"].toInteger() -
                                  uploadInfo(id, meta)["received"].toInteger());
    }
  }
  QStorageInfo storage(m_root);
  if (pending >= 16 || !storage.isValid() ||
      storage.bytesAvailable() < size + reserved + 64 * 1024 * 1024)
    return fail("Too many pending uploads or insufficient disk space; discard "
                "unused uploads first");
  const auto id = QUuid::createUuid().toString(QUuid::Id128),
             dir = directory(id);
  if (!QDir().mkpath(dir))
    return fail("Cannot create upload");
  QJsonObject meta{{"filename", name},
                   {"file", "media." + extension},
                   {"size", size},
                   {"sha256", sha.toLower()},
                   {"complete", false}};
  QFile file(dir + "/" + meta["file"].toString());
  if (!file.open(QIODevice::WriteOnly)) {
    QDir(dir).removeRecursively();
    return fail("Cannot create upload file");
  }
  file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  file.close();
  if (!save(dir + "/upload.json", meta)) {
    QDir(dir).removeRecursively();
    return fail("Cannot save upload record");
  }
  return ok(uploadInfo(id, meta));
}
QJsonObject McpMediaStore::chunk(const QJsonObject &args) {
  const auto id = args["uploadId"].toString();
  const auto meta = manifest(id);
  if (meta.isEmpty() || meta["complete"].toBool())
    return fail("Unknown or already completed upload");
  const auto encoded = args["dataBase64"].toString().toLatin1();
  const auto decoded = QByteArray::fromBase64Encoding(
      encoded, QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || decoded.decoded.isEmpty() ||
      decoded.decoded.size() > chunkSize)
    return fail("Invalid base64 chunk or chunk exceeds 1 MiB");
  QFile file(directory(id) + "/" + meta["file"].toString());
  if (!file.open(QIODevice::ReadWrite))
    return fail("Cannot open upload");
  const auto offset = args["offset"].toInteger(-1),
             length = qint64(decoded.decoded.size());
  if (offset < 0 || offset > file.size() ||
      offset + length > meta["size"].toInteger())
    return fail(
        "Offset or chunk length exceeds the upload; send chunks sequentially");
  file.seek(offset);
  if (offset < file.size()) {
    if (offset + length > file.size() || file.read(length) != decoded.decoded)
      return fail("Retried chunk differs from previously uploaded bytes");
  } else if (file.write(decoded.decoded) != length || !file.flush()) {
    file.resize(offset);
    return fail("Cannot write upload chunk");
  }
  file.close();
  return ok(uploadInfo(id, meta));
}
QJsonObject McpMediaStore::finish(QString id) {
  auto meta = manifest(id);
  if (meta.isEmpty())
    return fail("Unknown uploadId");
  const auto path = directory(id) + "/" + meta["file"].toString();
  if (QFileInfo(path).size() != meta["size"].toInteger())
    return fail("Upload is incomplete");
  if (!meta["complete"].toBool()) {
    if (!meta["sha256"].toString().isEmpty()) {
      QFile file(path);
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file) ||
          QString::fromLatin1(hash.result().toHex()) !=
              meta["sha256"].toString())
        return fail("SHA-256 verification failed");
    }
    const auto type = mimeType(path);
    if (!type.startsWith("image/") && !type.startsWith("audio/") &&
        !type.startsWith("video/"))
      return fail("Uploaded content is not a supported media file");
    meta["mimeType"] = type;
    meta["complete"] = true;
    if (!save(directory(id) + "/upload.json", meta))
      return fail("Cannot save completed upload");
  }
  return ok(uploadInfo(id, meta));
}
QJsonObject McpMediaStore::discard(QString id) {
  if (manifest(id).isEmpty())
    return fail("Unknown uploadId");
  return QDir(directory(id)).removeRecursively()
             ? ok({{"discarded", true}})
             : fail("Cannot discard upload");
}
QJsonObject McpMediaStore::status(QString id) const {
  if (!id.isEmpty()) {
    const auto meta = manifest(id);
    return meta.isEmpty() ? fail("Unknown uploadId") : ok(uploadInfo(id, meta));
  }
  QJsonArray rows;
  int total = 0;
  for (const auto &entry :
       QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
    const auto meta = manifest(entry);
    if (meta.isEmpty())
      continue;
    ++total;
    if (rows.size() < 100 || !meta["complete"].toBool())
      rows.append(uploadInfo(entry, meta));
  }
  return ok({{"uploads", rows}, {"total", total}});
}
QString McpMediaStore::uploadedPath(QString id) const {
  const auto meta = manifest(id);
  if (!meta["complete"].toBool())
    return {};
  const auto path = directory(id) + "/" + meta["file"].toString();
  return QFileInfo(path).isFile() && !QFileInfo(path).isSymLink()
             ? QFileInfo(path).canonicalFilePath()
             : QString();
}
QJsonObject McpMediaStore::describe(QString path, QString endpoint) {
  const QFileInfo info(path);
  if (!info.isFile() || endpoint.isEmpty())
    return {};
  path = info.canonicalFilePath();
  const auto now = QDateTime::currentSecsSinceEpoch();
  QString token;
  for (auto it = m_links.begin(); it != m_links.end();) {
    if (it.value().expires <= now)
      it = m_links.erase(it);
    else {
      if (it.value().path == path && it.value().expires > now + 60)
        token = it.key();
      ++it;
    }
  }
  if (token.isEmpty()) {
    if (m_links.size() >= 1024) {
      auto oldest = m_links.begin();
      for (auto it = m_links.begin(); it != m_links.end(); ++it)
        if (it.value().expires < oldest.value().expires)
          oldest = it;
      m_links.erase(oldest);
    }
    token = QUuid::createUuid().toString(QUuid::Id128) +
            QUuid::createUuid().toString(QUuid::Id128);
    m_links[token] = {path, now + 3600};
  }
  QUrl url(endpoint);
  url.setPath("/media/" + token);
  return {{"downloadUrl", url.toString()},
          {"previewUrl", url.toString()},
          {"mimeType", mimeType(path)},
          {"size", info.size()},
          {"filename", info.fileName()},
          {"expiresAt", QDateTime::fromSecsSinceEpoch(m_links[token].expires)
                            .toUTC()
                            .toString(Qt::ISODate)}};
}
QString McpMediaStore::resolveLink(QString token) const {
  const auto it = m_links.constFind(token);
  return it != m_links.cend() &&
                 it->expires > QDateTime::currentSecsSinceEpoch()
             ? it->path
             : QString();
}
QJsonObject McpMediaStore::readChunk(QString path, qint64 offset, int maximum) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return fail("Media is unavailable");
  if (offset < 0 || offset > file.size() || maximum < 1 || maximum > chunkSize)
    return fail("Invalid offset or chunk size");
  file.seek(offset);
  const auto data = file.read(maximum);
  if (data.isEmpty() && offset < file.size())
    return fail("Cannot read media");
  return ok({{"dataBase64", QString::fromLatin1(data.toBase64())},
             {"offset", offset},
             {"bytes", data.size()},
             {"size", file.size()},
             {"eof", offset + data.size() == file.size()},
             {"mimeType", mimeType(path)}});
}
QJsonObject McpMediaStore::preview(QString path) {
  QImageReader reader(path);
  const auto size = reader.size();
  if (!size.isValid())
    return fail("An image is required for an inline preview; use the media URL "
                "for audio/video");
  reader.setAutoTransform(true);
  reader.setScaledSize(size.scaled(1280, 1280, Qt::KeepAspectRatio));
  const auto image = reader.read();
  if (image.isNull())
    return fail("Cannot decode image preview");
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "JPEG", 85);
  return ok({{"mimeType", "image/jpeg"},
             {"dataBase64", QString::fromLatin1(bytes.toBase64())},
             {"width", image.width()},
             {"height", image.height()}});
}
