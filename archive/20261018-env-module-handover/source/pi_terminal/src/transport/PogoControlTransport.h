#pragma once

#include "TransportBase.h"
#include <QSerialPort>
#include <QTimer>

class PogoControlTransport : public TransportBase {
    Q_OBJECT
public:
    explicit PogoControlTransport(QObject *parent = nullptr);
    bool open(const QString &device, qint32 baud = 115200);
    bool sendControl(const QJsonObject &event) override { return sendCommand(event); }
    void close() override;
    bool sendCommand(const QJsonObject &command);
    bool sendRawControl(quint8 command, const QByteArray &payload);
private slots:
    void readAvailable();
private:
    QSerialPort serial_;
    QByteArray buffer_;
    quint32 sequence_ = 1;
    quint32 lastReceiveSequence_ = 0;
    bool haveReceiveSequence_ = false;
    QTimer reconnect_, frameTimeout_;
    QString device_;
    qint32 baud_ = 115200;
    bool enabled_ = false;
};
