#pragma once
#include <QObject>
#include <QElapsedTimer>

// StoreKit is the sole source of Store-edition entitlements. No writable local
// preference, model file, command-line option or MCP call can grant access.
class PurchaseManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool storeEdition READ storeEdition CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool lifetimeUnlocked READ lifetimeUnlocked NOTIFY changed)
    Q_PROPERTY(bool accessAllowed READ accessAllowed NOTIFY changed)
    Q_PROPERTY(bool trialStarted READ trialStarted NOTIFY changed)
    Q_PROPERTY(bool canStartTrial READ canStartTrial NOTIFY changed)
    Q_PROPERTY(int trialDaysRemaining READ trialDaysRemaining NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canPurchase READ canPurchase NOTIFY changed)
    Q_PROPERTY(QString price READ price NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    explicit PurchaseManager(QObject *parent=nullptr);
    ~PurchaseManager() override;
    bool storeEdition() const;
    bool ready() const { return !storeEdition() || m_ready; }
    bool lifetimeUnlocked() const { return !storeEdition() || (m_ready && m_unlocked); }
    bool accessAllowed() const;
    bool trialStarted() const { return m_trialStarted; }
    bool canStartTrial() const { return ready() && !m_unlocked && !m_trialStarted && m_trialAvailable && m_available && !m_busy; }
    int trialDaysRemaining() const;
    bool busy() const { return m_busy; }
    bool canPurchase() const { return storeEdition() && ready() && !m_unlocked && m_available && !m_busy; }
    QString price() const { return m_price; }
    QString message() const { return m_message; }
    Q_INVOKABLE bool requireAccess();
    Q_INVOKABLE void showUnlock();
    Q_INVOKABLE void purchase();
    Q_INVOKABLE void startTrial();
    Q_INVOKABLE void restore();
    Q_INVOKABLE void refresh();
signals:
    void changed();
    void unlockRequested();
private:
    friend class PurchaseManagerTests;
    void receive(const QByteArray &json);
    static void callback(void *context,const char *json);
    void *m_store=nullptr;
    bool m_ready=false,m_unlocked=false,m_busy=false,m_available=false;
    bool m_trialStarted=false,m_trialAvailable=false;
    qint64 m_trialExpires=0,m_referenceNow=0;
    QElapsedTimer m_clock;
    qint64 effectiveNow() const;
    QString m_price,m_message;
};
