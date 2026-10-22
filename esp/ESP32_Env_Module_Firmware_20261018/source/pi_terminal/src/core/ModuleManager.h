#pragma once
#include <QObject>
#include <QLibrary>
#include <QJsonObject>
#include <QHash>
#include <QSharedPointer>
#include <functional>
#include "modules/api/pi_module_api.h"

// All callbacks run serially on the host thread. Plugins must be nonblocking;
// stop() joins their own workers and guarantees no callbacks survive its return.
class ModuleManager : public QObject {
    Q_OBJECT
public:
    explicit ModuleManager(QObject *parent = nullptr) : QObject(parent) {}
    ~ModuleManager() override;
    bool load(const QString &session, int id, const QString &directory, const QJsonObject &transport);
    void unload(const QString &session); // Caller has synchronously destroyed the QML Loader.
    void data(const QString &session, quint8 transport, const QByteArray &payload);
    void connected(const QString &session, quint8 transport);
    void action(const QString &session, const QString &action);
    void disconnectRequest(const QString &session, const QJsonObject &request);
    QJsonObject manifest(const QString &session) const;
    QString page(const QString &session) const;
    std::function<bool(const QString &, quint8, const QByteArray &)> sender;
    std::function<bool(const QString &, quint8, const QByteArray &, quint16 *)> controlSender;
signals:
    void error(QString session, QString message);
    void event(QString session, QString type, QString json);
private:
    struct Instance {
        ModuleManager *owner;
        QString session, directory;
        QJsonObject manifest;
        QLibrary library;
        pi_module_handle_t *handle = nullptr;
        pi_host_services_t services{};
        bool stopping = false;
    };
    QHash<QString, QSharedPointer<Instance>> instances_;
    QHash<QString, QList<QSharedPointer<Instance>>> stoppedLibraries_;
    static int sendControl(void *, uint8_t, const uint8_t *, uint16_t, uint16_t *);
    static int sendData(void *, uint8_t, const uint8_t *, uint32_t);
    static void publishAction(const char *, const char *);
    static void publishEvent(const char *, const char *);
    static const char *dataDirectory();
    static void emitEvent(void *, const char *, const char *);
};
