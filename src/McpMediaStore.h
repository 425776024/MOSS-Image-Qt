#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>

// App-owned upload staging and short-lived, read-only media capabilities.
// Callers never choose a destination path. Imports copy staged files into
// libraries.
class McpMediaStore {
public:
  explicit McpMediaStore(QString root) : m_root(std::move(root)) {}
  QJsonObject begin(const QJsonObject &args);
  QJsonObject chunk(const QJsonObject &args);
  QJsonObject finish(QString id);
  QJsonObject status(QString id = {}) const;
  QJsonObject discard(QString id);
  QString uploadedPath(QString id) const;
  QJsonObject describe(QString path, QString endpoint);
  QString resolveLink(QString token) const;
  static QString mimeType(QString path);
  static QJsonObject readChunk(QString path, qint64 offset, int maximum);
  static QJsonObject preview(QString path);

private:
  QString directory(QString id) const;
  QJsonObject manifest(QString id) const;
  QJsonObject uploadInfo(QString id, const QJsonObject &meta) const;
  struct Link {
    QString path;
    qint64 expires;
  };
  QString m_root;
  QMap<QString, Link> m_links;
};
