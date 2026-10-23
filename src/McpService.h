#pragma once
#include "McpCallModel.h"
#include "McpHttpServer.h"
#include "McpMediaStore.h"
#include <QLockFile>
#include <QTimer>
#include <memory>
class Studio;
class WorkspaceLibrary;
class ConversationDocument;

class McpService : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(QString url READ url NOTIFY changed)
  Q_PROPERTY(bool lanEnabled READ lanEnabled WRITE setLanEnabled NOTIFY changed)
  Q_PROPERTY(QStringList lanAddresses READ lanAddresses NOTIFY changed)
  Q_PROPERTY(
      QString lanAddress READ lanAddress WRITE setLanAddress NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QAbstractItemModel *calls READ calls CONSTANT)
  Q_PROPERTY(int toolCount READ toolCount CONSTANT)
public:
  McpService(Studio *studio, WorkspaceLibrary *workspace,
             QObject *parent = nullptr);
  ~McpService() override;
  bool enabled() const { return m_enabled; }
  bool running() const { return m_http.listening(); }
  QString url() const;
  bool lanEnabled() const { return m_lanEnabled; }
  QStringList lanAddresses() const { return m_lanAddresses; }
  QString lanAddress() const { return m_lanAddress; }
  Q_INVOKABLE void setLanEnabled(bool value);
  Q_INVOKABLE void setLanAddress(QString value);
  QString error() const { return m_error; }
  QAbstractItemModel *calls() { return &m_calls; }
  int toolCount() const;
  Q_INVOKABLE void setEnabled(bool enabled);
  Q_INVOKABLE QString connectionText(bool english = false) const;
  Q_INVOKABLE QString configuration() const;
  Q_INVOKABLE QString toolReference() const;
  Q_INVOKABLE void clearCalls() { m_calls.clear(); }
  Q_INVOKABLE QVariantMap callDetails(QString id) const {
    return m_calls.get(id);
  }
  void shutdown();
  QJsonObject dispatch(const QJsonObject &request);
  std::function<QJsonObject(const QJsonObject &)> uiControl;
signals:
  void changed();
  void callChanged(QString id);

private:
  QJsonObject execute(QString name, const QJsonObject &args);
  QJsonObject enqueue(QString kind, const QJsonObject &args);
  QJsonObject publicJob(QString id) const;
  void runNext();
  void finishJob(bool success);
  void updateProgress();
  bool saveJob(QString id);
  bool saveConfig();
  bool prepareStorage();
  void refreshAddresses();
  QJsonValue withMediaLinks(QJsonValue value);
  QString mediaSource(const QJsonObject &args) const;
  void loadJobs();
  QVariantMap page(QString id) const;
  bool reserved(QString workId) const;
  bool anyJobs() const;
  QString updateConversation(ConversationDocument *thread,
                             const QJsonObject &args);
  Studio *m_studio;
  WorkspaceLibrary *m_workspace;
  McpHttpServer m_http;
  McpCallModel m_calls;
  QTimer m_tick;
  QTimer m_networkTick;
  std::unique_ptr<McpMediaStore> m_media;
  QString m_root, m_token, m_error, m_active, m_callId;
  int m_port = 39871;
  bool m_enabled = false, m_stopping = false, m_starting = false;
  bool m_lanEnabled = false;
  QString m_lanAddress;
  QStringList m_lanAddresses;
  QMap<QString, QJsonObject> m_jobs;
  QStringList m_queue;
  std::unique_ptr<QLockFile> m_lock;
};
