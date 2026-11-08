#include "PurchaseManager.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
#include <cmath>

#ifdef MOSS_APP_STORE
extern "C" {
void *moss_store_create(void *,void (*)(void *,const char *));
void moss_store_destroy(void *);
void moss_store_refresh(void *);
void moss_store_purchase(void *);
void moss_store_trial(void *);
void moss_store_restore(void *);
}
#endif

PurchaseManager::PurchaseManager(QObject *parent):QObject(parent) {
#ifdef MOSS_APP_STORE
    m_store=moss_store_create(this,&PurchaseManager::callback);
    QTimer::singleShot(0,this,&PurchaseManager::refresh);
    if(auto app=qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        connect(app,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){
            if(state==Qt::ApplicationActive)refresh();
        });
#endif
}
PurchaseManager::~PurchaseManager(){
#ifdef MOSS_APP_STORE
    moss_store_destroy(m_store);
#endif
}
bool PurchaseManager::storeEdition() const {
#ifdef MOSS_APP_STORE
    return true;
#else
    return false;
#endif
}
qint64 PurchaseManager::effectiveNow() const {return qMax(QDateTime::currentMSecsSinceEpoch(),m_referenceNow+(m_clock.isValid()?m_clock.elapsed():0));}
bool PurchaseManager::accessAllowed() const {return lifetimeUnlocked()||(m_ready&&m_trialStarted&&m_trialExpires>effectiveNow());}
int PurchaseManager::trialDaysRemaining() const {return m_trialStarted?qMax(0,int(std::ceil(double(m_trialExpires-effectiveNow())/86400000.0))):0;}
bool PurchaseManager::requireAccess(){if(accessAllowed())return true;showUnlock();return false;}
void PurchaseManager::showUnlock(){if(!storeEdition())return;emit unlockRequested();refresh();}
void PurchaseManager::refresh(){
#ifdef MOSS_APP_STORE
    if(!m_busy)moss_store_refresh(m_store);
#endif
}
void PurchaseManager::purchase(){
#ifdef MOSS_APP_STORE
    if(!canPurchase())return;
    m_busy=true;m_message.clear();emit changed();moss_store_purchase(m_store);
#endif
}
void PurchaseManager::startTrial(){
#ifdef MOSS_APP_STORE
    if(!canStartTrial())return;
    m_busy=true;m_message.clear();emit changed();moss_store_trial(m_store);
#endif
}
void PurchaseManager::restore(){
#ifdef MOSS_APP_STORE
    if(m_busy)return;
    m_busy=true;m_message.clear();emit changed();moss_store_restore(m_store);
#endif
}
void PurchaseManager::callback(void *context,const char *json){
    auto owner=static_cast<PurchaseManager *>(context);
    const QByteArray data(json);
    QMetaObject::invokeMethod(owner,[guard=QPointer<PurchaseManager>(owner),data]{if(guard)guard->receive(data);},Qt::QueuedConnection);
}
void PurchaseManager::receive(const QByteArray &json){
    const auto state=QJsonDocument::fromJson(json).object();
    if(state.isEmpty())return;
    m_ready=state["ready"].toBool();m_unlocked=state["unlocked"].toBool();
    m_trialStarted=state["trialStarted"].toBool();m_trialAvailable=state["trialAvailable"].toBool();
    m_trialExpires=qint64(state["trialExpires"].toDouble());m_referenceNow=qint64(state["now"].toDouble());m_clock.restart();
    m_busy=state["busy"].toBool();m_available=state["available"].toBool();m_price=state["price"].toString();
    const auto code=state["message"].toString();
    const QHash<QString,QString> messages={
        {"unavailable","暂时无法获取价格，请稍后重试。"},
        {"trialUnavailable","暂时无法开始免费试用，请稍后重试。"},
        {"restricted","此 Apple 账户不允许购买，请检查系统设置。"},
        {"pending","购买正在等待批准，批准后将自动解锁。"},
        {"verification","购买验证失败，请恢复购买或稍后重试。"},
        {"purchaseFailed","购买未完成，请稍后重试。"},
        {"restoreFailed","恢复失败，请检查网络和 Apple 账户后重试。"},
        {"restored","购买已恢复。"},
        {"notPurchased","此 Apple 账户没有可恢复的购买。"},
        {"window","请返回应用窗口后再购买。"}};
    m_message=messages.value(code);emit changed();
}
