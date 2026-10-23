#include "McpHttpServer.h"
#include "McpMediaStore.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
constexpr qsizetype maxBody = 4 * 1024 * 1024, maxHeader = 16 * 1024;
const QSet<QByteArray> versions = {"2025-03-26", "2025-06-18", "2025-11-25"};
void respond(QTcpSocket *socket, int code, const QByteArray &body = {},
             const QByteArray &allow = "POST") {
  const auto reason = QMap<int, QByteArray>{{200, "OK"},
                                            {202, "Accepted"},
                                            {400, "Bad Request"},
                                            {401, "Unauthorized"},
                                            {403, "Forbidden"},
                                            {404, "Not Found"},
                                            {405, "Method Not Allowed"},
                                            {413, "Content Too Large"},
                                            {415, "Unsupported Media Type"}}
                          .value(code, "Error");
  QByteArray header = "HTTP/1.1 " + QByteArray::number(code) + " " + reason +
                      "\r\nConnection: close\r\nCache-Control: "
                      "no-store\r\nX-Content-Type-Options: "
                      "nosniff\r\nContent-Type: application/json\r\n";
  if (code == 405)
    header += "Allow: " + allow + "\r\n";
  if (code == 401)
    header += "WWW-Authenticate: Bearer\r\n";
  socket->write(header + "Content-Length: " + QByteArray::number(body.size()) +
                "\r\n\r\n" + body);
  socket->disconnectFromHost();
}
void serveMedia(QTcpSocket *socket, QString path, QByteArray range, bool head) {
  auto file = std::make_shared<QFile>(path);
  if (path.isEmpty() || !file->open(QIODevice::ReadOnly)) {
    respond(socket, 404);
    return;
  }
  const auto size = file->size();
  qint64 start = 0, end = size - 1;
  if (!range.isEmpty()) {
    static const QRegularExpression syntax("^bytes=(\\d*)-(\\d*)$");
    const auto match = syntax.match(QString::fromLatin1(range));
    bool firstOk = false, lastOk = false;
    const auto first = match.captured(1).toLongLong(&firstOk),
               last = match.captured(2).toLongLong(&lastOk);
    bool valid = match.hasMatch() && size > 0;
    if (match.captured(1).isEmpty()) {
      valid &= lastOk && last > 0;
      start = std::max<qint64>(0, size - last);
    } else {
      valid &= firstOk;
      start = first;
      if (!match.captured(2).isEmpty()) {
        valid &= lastOk;
        end = std::min(last, size - 1);
      }
    }
    valid &= start >= 0 && start < size && end >= start;
    if (!valid) {
      socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nConnection: "
                    "close\r\nContent-Range: bytes */" +
                    QByteArray::number(size) + "\r\nContent-Length: 0\r\n\r\n");
      socket->disconnectFromHost();
      return;
    }
  }
  const auto count = std::max<qint64>(0, end - start + 1);
  QByteArray headers = range.isEmpty() ? "HTTP/1.1 200 OK\r\n"
                                       : "HTTP/1.1 206 Partial Content\r\n";
  headers +=
      "Connection: close\r\nContent-Security-Policy: sandbox\r\nCache-Control: "
      "private, "
      "no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: "
      "nosniff\r\nAccess-Control-Allow-Origin: "
      "*\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, "
      "Accept-Ranges\r\nAccept-Ranges: bytes\r\nContent-Type: ";
  headers += McpMediaStore::mimeType(path).toUtf8();
  headers += "\r\nContent-Disposition: inline; filename*=UTF-8''" +
             QUrl::toPercentEncoding(QFileInfo(path).fileName());
  headers += "\r\nContent-Length: " + QByteArray::number(count) + "\r\n";
  if (!range.isEmpty())
    headers += "Content-Range: bytes " + QByteArray::number(start) + "-" +
               QByteArray::number(end) + "/" + QByteArray::number(size) +
               "\r\n";
  socket->write(headers + "\r\n");
  if (head || !count) {
    socket->disconnectFromHost();
    return;
  }
  file->seek(start);
  auto remaining = std::make_shared<qint64>(count);
  const auto pump = [socket, file, remaining] {
    if (socket->state() != QAbstractSocket::ConnectedState)
      return;
    while (*remaining > 0 && socket->bytesToWrite() < 256 * 1024) {
      const auto bytes = file->read(std::min<qint64>(*remaining, 64 * 1024));
      if (bytes.isEmpty() || socket->write(bytes) != bytes.size()) {
        socket->abort();
        return;
      }
      *remaining -= bytes.size();
    }
    if (!*remaining)
      socket->disconnectFromHost();
  };
  QObject::connect(socket, &QTcpSocket::bytesWritten, socket, pump);
  pump();
}
} // namespace
bool McpHttpServer::isLanAddress(const QHostAddress &address) {
  if (address.protocol() != QAbstractSocket::IPv4Protocol)
    return false;
  const auto ip = address.toIPv4Address();
  return (ip >> 24) == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8 ||
         (ip >> 16) == 0xa9fe;
}
QStringList McpHttpServer::lanAddresses() {
  QStringList preferred, other;
  for (const auto &nic : QNetworkInterface::allInterfaces()) {
    if (!nic.flags().testFlag(QNetworkInterface::IsUp) ||
        !nic.flags().testFlag(QNetworkInterface::IsRunning) ||
        nic.flags().testFlag(QNetworkInterface::IsLoopBack))
      continue;
    for (const auto &entry : nic.addressEntries())
      if (isLanAddress(entry.ip())) {
        auto &list = nic.type() == QNetworkInterface::Ethernet ||
                             nic.type() == QNetworkInterface::Wifi
                         ? preferred
                         : other;
        list << entry.ip().toString();
      }
  }
  preferred.sort();
  other.sort();
  preferred += other;
  preferred.removeDuplicates();
  return preferred;
}
McpHttpServer::McpHttpServer(QObject *parent)
    : QObject(parent), m_server(this) {
  m_server.setMaxPendingConnections(32);
  connect(&m_server, &QTcpServer::newConnection, this, [this] {
    while (m_server.hasPendingConnections()) {
      auto socket = m_server.nextPendingConnection();
      socket->setParent(this);
      if (!socket->peerAddress().isLoopback() &&
          (!m_allowLan || !isLanAddress(socket->peerAddress()) ||
           !isLanAddress(socket->localAddress()))) {
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
        respond(socket, 403);
        continue;
      }
      if (findChildren<QTcpSocket *>(QString(), Qt::FindDirectChildrenOnly)
              .size() > 32) {
        socket->abort();
        socket->deleteLater();
        continue;
      }
      socket->setReadBufferSize(maxBody + maxHeader + 1);
      auto buffer = std::make_shared<QByteArray>();
      auto done = std::make_shared<bool>(false);
      auto timeout = new QTimer(socket);
      timeout->setSingleShot(true);
      timeout->start(15000);
      connect(timeout, &QTimer::timeout, socket, [socket] { socket->abort(); });
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      connect(
          socket, &QTcpSocket::readyRead, this,
          [this, socket, buffer, done, timeout] {
            if (*done)
              return;
            buffer->append(socket->readAll());
            const auto reject = [&](int status) {
              *done = true;
              respond(socket, status);
            };
            const auto end = buffer->indexOf("\r\n\r\n");
            if ((end < 0 && buffer->size() > maxHeader) || end > maxHeader) {
              reject(413);
              return;
            }
            if (end < 0)
              return;
            const auto lines = buffer->left(end).split('\n');
            const auto request = lines.first().trimmed().split(' ');
            if (request.size() != 3 || request[2] != "HTTP/1.1") {
              reject(400);
              return;
            }
            QMap<QByteArray, QByteArray> headers;
            for (int i = 1; i < lines.size(); ++i) {
              const auto line = lines[i].trimmed();
              const auto colon = line.indexOf(':');
              if (colon < 1) {
                reject(400);
                return;
              }
              const auto key = line.left(colon).toLower();
              if (headers.contains(key)) {
                reject(400);
                return;
              }
              headers[key] = line.mid(colon + 1).trimmed();
            }
            QSet<QString> hosts{"127.0.0.1", "localhost"};
            if (m_allowLan && isLanAddress(socket->localAddress()))
              hosts.insert(socket->localAddress().toString());
            bool validHost = false;
            for (const auto &host : hosts)
              validHost |= headers.value("host").toLower() ==
                           host.toUtf8() + ":" + QByteArray::number(port());
            if (!validHost) {
              reject(403);
              return;
            }
            if (request[1].startsWith("/media/")) {
              const auto token = QString::fromLatin1(request[1].mid(7));
              const auto path = mediaPath ? mediaPath(token) : QString();
              if (path.isEmpty()) {
                reject(404);
                return;
              }
              if (request[0] != "GET" && request[0] != "HEAD") {
                *done = true;
                respond(socket, 405, {}, "GET, HEAD");
                return;
              }
              if (headers.contains("transfer-encoding") ||
                  (headers.contains("content-length") &&
                   headers["content-length"] != "0")) {
                reject(400);
                return;
              }
              *done = true;
              // Read-only capability URLs can be embedded by remote agent
              // frontends. The server credential is never part of these URLs.
              timeout->start(30000);
              connect(socket, &QTcpSocket::bytesWritten, timeout,
                      [timeout] { timeout->start(30000); });
              serveMedia(socket, path, headers.value("range"),
                         request[0] == "HEAD");
              return;
            }
            if (headers.contains("origin")) {
              const QUrl origin(QString::fromUtf8(headers["origin"]));
              if (origin.scheme() != "http" || !hosts.contains(origin.host()) ||
                  origin.port() != port() || !origin.userInfo().isEmpty()) {
                reject(403);
                return;
              }
            }
            if (request[1] != "/mcp") {
              reject(404);
              return;
            }
            // Compare all credential bytes; never log headers or credentials.
            const auto expected = "Bearer " + m_token.toUtf8(),
                       actual = headers.value("authorization");
            unsigned mismatch = unsigned(expected.size() ^ actual.size());
            for (qsizetype i = 0; i < expected.size(); ++i)
              mismatch |= unsigned(uchar(expected[i]) ^
                                   uchar(i < actual.size() ? actual[i] : 0));
            if (mismatch) {
              reject(401);
              return;
            }
            if (request[0] != "POST") {
              reject(405);
              return;
            }
            if (headers.contains("transfer-encoding")) {
              reject(400);
              return;
            }
            if (headers.value("content-type").split(';').first().trimmed() !=
                "application/json") {
              reject(415);
              return;
            }
            if (headers.contains("mcp-protocol-version") &&
                !versions.contains(headers["mcp-protocol-version"])) {
              reject(400);
              return;
            }
            bool ok = false;
            const auto length = headers.value("content-length").toLongLong(&ok);
            if (!ok || length < 0) {
              reject(400);
              return;
            }
            if (length > maxBody || buffer->size() > maxBody + maxHeader) {
              reject(413);
              return;
            }
            if (buffer->size() < end + 4 + length)
              return;
            if (buffer->size() != end + 4 + length) {
              reject(400);
              return;
            }
            QJsonParseError error;
            const auto json =
                QJsonDocument::fromJson(buffer->mid(end + 4, length), &error);
            *done = true;
            if (error.error != QJsonParseError::NoError || !json.isObject()) {
              respond(socket, 200,
                      QJsonDocument(
                          QJsonObject{
                              {"jsonrpc", "2.0"},
                              {"id", QJsonValue::Null},
                              {"error",
                               QJsonObject{{"code", -32700},
                                           {"message",
                                            "Expected one JSON-RPC object"}}}})
                          .toJson(QJsonDocument::Compact));
              return;
            }
            const auto result =
                dispatch ? dispatch(json.object()) : QJsonObject();
            respond(socket, result.isEmpty() ? 202 : 200,
                    result.isEmpty()
                        ? QByteArray()
                        : QJsonDocument(result).toJson(QJsonDocument::Compact));
          });
    }
  });
}
bool McpHttpServer::start(quint16 port, const QString &token, bool allowLan) {
  stop();
  m_token = token;
  m_allowLan = allowLan;
  return !token.isEmpty() && m_server.listen(allowLan ? QHostAddress::AnyIPv4
                                                      : QHostAddress::LocalHost,
                                             port);
}
void McpHttpServer::stop() {
  m_server.close();
  for (auto socket :
       findChildren<QTcpSocket *>(QString(), Qt::FindDirectChildrenOnly)) {
    socket->abort();
    socket->deleteLater();
  }
  m_token.clear();
}
