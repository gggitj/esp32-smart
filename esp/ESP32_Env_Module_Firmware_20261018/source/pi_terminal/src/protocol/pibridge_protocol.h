#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QtGlobal>

namespace PiBridge {
enum Kind : quint8 { ControlEvent = 1, ControlCommand = 2, Error = 3 };
QByteArray encode(quint8 direction, Kind kind, quint32 sequence, const QByteArray &payload);
bool decode(QByteArray &buffer, quint8 &direction, Kind &kind, quint32 &sequence, QByteArray &payload);
QJsonObject event(const QByteArray &payload);
}
