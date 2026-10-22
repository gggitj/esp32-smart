#pragma once

#include <QObject>
#include <QJsonObject>

class TransportBase : public QObject {
    Q_OBJECT
public:
    explicit TransportBase(QObject *parent = nullptr) : QObject(parent) {}
    ~TransportBase() override = default;
    // Unsupported planes return false: Pogo must never carry business payloads.
    virtual bool sendData(const QByteArray &) { return false; }
    virtual bool sendControl(const QJsonObject &) { return false; }
    virtual void close() = 0;
signals:
    void connected();
    void disconnected(QString reason);
    void payloadReceived(quint8 transport, QByteArray payload);
    void controlEvent(QJsonObject event);
    void error(QString message);
};
