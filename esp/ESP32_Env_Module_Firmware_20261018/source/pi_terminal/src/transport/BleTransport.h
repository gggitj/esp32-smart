#pragma once
#include "TransportBase.h"
#include <QBluetoothDeviceDiscoveryAgent>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QTimer>

// Each session owns its controller and characteristics. An application message is
// fragmented into <=20-byte GATT writes (works even at the default ATT MTU).
class BleTransport : public TransportBase {
    Q_OBJECT
public:
    explicit BleTransport(QObject *parent = nullptr);
    void start(const QJsonObject &config);
    bool sendData(const QByteArray &payload) override { return send(payload); }
    void close() override;
    bool send(const QByteArray &payload);
private:
    void found(const QBluetoothDeviceInfo &info);
    void receive(const QByteArray &fragment);
    QBluetoothDeviceDiscoveryAgent discovery_;
    QLowEnergyController *controller_ = nullptr;
    QLowEnergyService *service_ = nullptr;
    QLowEnergyCharacteristic tx_, rx_;
    QJsonObject config_;
    QTimer timer_, pump_;
    QTimer discoveryTimeout_;
    QList<QByteArray> pending_;
    QTimer writeTimeout_;
    bool writeInFlight_ = false;
    int writeFailures_ = 0;
    QByteArray assembling_;
    quint16 sendId_ = 0, receiveId_ = 0, next_ = 0, count_ = 0;
    bool ready_ = false;
};
