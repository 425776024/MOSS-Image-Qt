#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <functional>

// Streamable HTTP, JSON responses, no server-initiated SSE stream or sessions.
// Lifetime is owned by the application. LAN access requires explicit opt-in.
class McpHttpServer : public QObject {
  Q_OBJECT
public:
  explicit McpHttpServer(QObject *parent = nullptr);
  bool start(quint16 port, const QString &token, bool allowLan = false);
  static QStringList lanAddresses();
  static bool isLanAddress(const QHostAddress &address);
  void stop();
  quint16 port() const { return m_server.serverPort(); }
  bool listening() const { return m_server.isListening(); }
  QString error() const { return m_server.errorString(); }
  std::function<QJsonObject(const QJsonObject &)> dispatch;
  std::function<QString(const QString &)> mediaPath;

private:
  QTcpServer m_server;
  QString m_token;
  bool m_allowLan = false;
};
