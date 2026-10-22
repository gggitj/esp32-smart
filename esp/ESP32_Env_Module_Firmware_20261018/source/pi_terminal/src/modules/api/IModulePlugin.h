#pragma once
// Optional C++ implementation interface. Only pi_module_api.h crosses the DSO boundary.
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>
#include <QByteArray>

struct TransportInfo {
    QStringList available;
    QString bleName, bleServiceUuid, lanService;
    quint16 lanPort = 0;
};
struct DisconnectRequest {
    QString reason;
    bool stopRequired = true;
    bool autoReconnect = false;
};
class IModulePlugin {
public:
    virtual ~IModulePlugin() = default;
    virtual QString id() const = 0;
    virtual quint8 wireModuleId() const = 0;
    virtual QVariantMap capabilities() const = 0;
    virtual QUrl qmlPage() const = 0;
    virtual QVariantList actions() const = 0;
    virtual bool onAttach(const TransportInfo &) = 0;
    virtual void onTransportConnected(quint8 transport) = 0;
    virtual void onData(quint8 transport, const QByteArray &payload) = 0;
    virtual void onDisconnectRequest(const DisconnectRequest &) = 0;
    virtual void onDetach() = 0;
    virtual void stop() = 0;
};
