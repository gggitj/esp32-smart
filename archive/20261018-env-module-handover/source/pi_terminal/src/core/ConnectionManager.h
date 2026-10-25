#pragma once
#include <QObject>
#include <QVariantList>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QSharedPointer>
#include <QProcess>
#include <QTimer>
#include "ModuleManager.h"
#include "ModuleCardModel.h"
#include "PageProvider.h"
#include "SessionStateMachine.h"
#include "PackageReceiver.h"
#include "HostEventDetector.h"
#include "transport/PogoControlTransport.h"
#include "transport/BleTransport.h"
#include "transport/HotspotLanTransport.h"

class ConnectionManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cards READ cards NOTIFY cardsChanged)
    Q_PROPERTY(QString status READ status NOTIFY cardsChanged)
    Q_PROPERTY(ModuleCardModel *cardModel READ cardModel CONSTANT)
    Q_PROPERTY(PageProvider *pageProvider READ pageProvider CONSTANT)
public:
    explicit ConnectionManager(QObject *parent = nullptr);
    ~ConnectionManager() override;
    bool configure(const QString &path);
    bool start(const QString &device);
    QVariantList cards() const;
    QString status() const { return status_; }
    ModuleCardModel *cardModel() { return &cards_; }
    PageProvider *pageProvider() { return &pages_; }
    Q_INVOKABLE void attach(const QJsonObject &event);
    Q_INVOKABLE void disconnectModule(const QString &instance);
    Q_INVOKABLE void releasePage(const QString &instance);
    Q_INVOKABLE void action(const QString &instance, const QString &name);
    Q_INVOKABLE void retry(const QString &instance);
    Q_INVOKABLE void trust(const QString &instance, bool always);
signals:
    void cardsChanged();
    void pageMustClose(QString instance);
    void announcement(QString text);
    void trustRequired(QString instance, QString title, QStringList permissions);
    void deviceAttached(QString instance, int moduleId);
    void deviceDetached(QString instance);
    void dataLost(QString instance);
    void dataRestored(QString instance);
    void sessionDisconnected(QString instance);
    void bleOnlyPackageReady(QString instance);
private:
    struct Session {
        int id = 0;
        QString instance, state, message, packagePath;
        QJsonObject profile;
        QJsonObject verifiedManifest;
        QString trustKey;
        SessionStateMachine *machine = nullptr;
        PackageReceiver *receiver = nullptr;
        QString incoming;
        HotspotLanTransport *lan = nullptr;
        BleTransport *ble = nullptr;
        QProcess *installer = nullptr;
        quint32 tx = 0, rx = 0;
        qint64 lastSeen = 0, lostAt = 0, retryAt = 0;
        int progress = 0;
        bool contact = true, ready = false, loaded = false, stopping = false, bleConnected = false, lanConnected = false;
        bool firstDataLogged = false;
        quint8 activeTransport = 0;
        quint8 candidateTransport = 0;
        bool releasing = false;
        bool closePending = false;
        bool awaitingPeerClose = false;
        int closeRetries = 0;
        qint64 closeDeadline = 0;
    };
    ModuleManager modules_;
    ModuleCardModel cards_;
    PageProvider pages_;
    PogoControlTransport pogo_;
    HostEventDetector hostEvents_;
    QTimer tick_;
    QHash<QString, QSharedPointer<Session>> sessions_;
    QSet<QString> suppressedInstances_;
    QJsonObject profiles_, config_;
    QString status_ = QStringLiteral("等待模块");
    void state(const QSharedPointer<Session> &, const QString &, const QString &);
    void connectData(const QSharedPointer<Session> &);
    void receive(const QString &, quint8, const QByteArray &);
    bool send(const QSharedPointer<Session> &, const QString &, const QJsonObject & = {}, quint8 transport = 0);
    void lost(const QString &, const QString &);
    void install(const QSharedPointer<Session> &);
    void loadVerified(const QSharedPointer<Session> &);
    void stop(const QSharedPointer<Session> &, const QString &);
    void finishRelease(const QSharedPointer<Session> &);
    void sendCloseRequest(const QSharedPointer<Session> &);
    void transportLost(const QSharedPointer<Session> &, quint8 transport, const QString &reason);
};
