#pragma once
#include "TransportBase.h"
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
class HotspotLanTransport : public TransportBase {
    Q_OBJECT
public:
    explicit HotspotLanTransport(QObject *parent = nullptr);
    bool connectToModule(const QHostAddress &address, quint16 port);
    bool send(const QByteArray &payload);
    bool sendData(const QByteArray &payload) override { return send(payload); }
    void close() override;
private:
    void flushWrites();
    QTcpSocket socket_;
    QByteArray buffer_;
    QByteArray outgoing_;
    QTimer timer_;
};
