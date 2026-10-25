#pragma once
#include <QByteArray>
#include <QtEndian>
// CRC-32/ISO-HDLC (same as Python binascii.crc32), little-endian trailer.
inline QByteArray dataChecksum(const QByteArray &data) {
    quint32 crc = 0xffffffffu;
    for (const char c : data) {
        crc ^= quint8(c);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    QByteArray result(4, '\0');
    qToLittleEndian(crc ^ 0xffffffffu, reinterpret_cast<uchar *>(result.data()));
    return result;
}
