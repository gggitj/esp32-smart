#include "pibridge_protocol.h"

#include <QtEndian>
#include <QJsonDocument>

namespace {
quint16 crc16(const QByteArray &data) {
    quint16 crc = 0xFFFF;
    for (char c : data) {
        crc ^= quint16(quint8(c)) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}
}

namespace PiBridge {
QByteArray encode(quint8 direction, Kind kind, quint32 sequence, const QByteArray &payload) {
    if (payload.size() > 2048 || direction > 1 || kind < ControlEvent || kind > Error)
        return {};
    QByteArray out("PBR1", 4);
    out.append(char(direction));
    out.append(char(kind));
    quint32 seq = qToLittleEndian(sequence);
    quint16 len = qToLittleEndian(quint16(payload.size()));
    out.append(reinterpret_cast<const char *>(&seq), sizeof(seq));
    out.append(reinterpret_cast<const char *>(&len), sizeof(len));
    out.append(payload);
    quint16 crc = qToLittleEndian(crc16(out.mid(4)));
    out.append(reinterpret_cast<const char *>(&crc), sizeof(crc));
    return out;
}

bool decode(QByteArray &buffer, quint8 &direction, Kind &kind, quint32 &sequence, QByteArray &payload) {
    for (;;) {
    while (buffer.size() >= 4 && buffer.left(4) != "PBR1") buffer.remove(0, 1);
    if (buffer.size() < 12) return false;
    const auto *raw = reinterpret_cast<const uchar *>(buffer.constData());
    const quint16 length = qFromLittleEndian<quint16>(raw + 10);
    const int total = 12 + length + 2;
    if (length > 2048 || raw[4] > 1 || raw[5] < ControlEvent || raw[5] > Error) {
        buffer.remove(0, 1); continue;
    }
    if (buffer.size() < total) return false;
    QByteArray body = buffer.mid(4, 8 + length);
    const quint16 expected = qFromLittleEndian<quint16>(raw + 12 + length);
    if (crc16(body) != expected) { buffer.remove(0, 1); continue; }
    direction = quint8(raw[4]);
    kind = Kind(raw[5]);
    sequence = qFromLittleEndian<quint32>(raw + 6);
    payload = buffer.mid(12, length);
    buffer.remove(0, total);
    return true;
    }
}

QJsonObject event(const QByteArray &payload) {
    return QJsonDocument::fromJson(payload).object();
}
}
