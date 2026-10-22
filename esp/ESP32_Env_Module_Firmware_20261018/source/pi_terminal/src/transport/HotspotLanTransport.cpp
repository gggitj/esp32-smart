#include "HotspotLanTransport.h"
#include <QtEndian>
#include "protocol/data_checksum.h"
HotspotLanTransport::HotspotLanTransport(QObject *parent) : TransportBase(parent) {
    timer_.setSingleShot(true); timer_.setInterval(10000);
    connect(&timer_, &QTimer::timeout, this, [this] { socket_.abort(); buffer_.clear(); emit error("TCP partial-frame timeout"); });
    connect(&socket_, &QTcpSocket::connected, this, &TransportBase::connected);
    connect(&socket_, &QTcpSocket::connected, this, &HotspotLanTransport::flushWrites);
    connect(&socket_, &QTcpSocket::bytesWritten, this, [this](qint64) { flushWrites(); });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] { timer_.stop(); buffer_.clear(); emit disconnected("TCP disconnected"); });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](auto) { emit error(socket_.errorString()); });
    connect(&socket_, &QTcpSocket::readyRead, this, [this] {
        buffer_ += socket_.readAll();
        if (buffer_.size() > 2 * 1048576) { close(); emit error("TCP receive limit"); return; }
        while (buffer_.size() >= 4) {
            const auto n = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(buffer_.constData()));
            if (n > 1048576 || n < 4) { close(); emit error("TCP invalid length"); return; }
            if (buffer_.size() < n + 4) break;
            const auto block = buffer_.mid(4, n); buffer_.remove(0, n + 4);
            auto payload = block.chopped(4);
            if (dataChecksum(payload) != block.right(4)) { close(); emit error("TCP checksum mismatch"); return; }
            emit payloadReceived(2, payload);
        }
        if (buffer_.isEmpty()) timer_.stop(); else timer_.start();
    });
}
bool HotspotLanTransport::connectToModule(const QHostAddress &address, quint16 port) {
    close(); if (address.isNull() || port == 0) return false;
    socket_.connectToHost(address, port); return true;
}
bool HotspotLanTransport::send(const QByteArray &payload) {
    if (socket_.state() != QAbstractSocket::ConnectedState || payload.size() > 1048572 || outgoing_.size() > 2097152) return false;
    QByteArray wire(4, '\0');
    qToBigEndian(quint32(payload.size() + 4), reinterpret_cast<uchar *>(wire.data()));
    wire += payload; wire += dataChecksum(payload);
    if (outgoing_.size() + wire.size() > 2097152) return false;
    outgoing_ += wire;
    flushWrites();
    return true;
}
void HotspotLanTransport::flushWrites() {
    if (outgoing_.isEmpty() || socket_.state() != QAbstractSocket::ConnectedState) return;
    const qint64 written = socket_.write(outgoing_);
    if (written < 0) { emit error(socket_.errorString()); return; }
    if (written > 0) outgoing_.remove(0, int(written));
}
void HotspotLanTransport::close() { timer_.stop(); socket_.abort(); buffer_.clear(); outgoing_.clear(); }
