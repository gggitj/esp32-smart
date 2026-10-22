#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSerialPort>
#include "protocol/pibridge_protocol.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QSerialPort serial;
    serial.setPortName(qEnvironmentVariable("MOCK_DEVICE", "/tmp/orange-pi-mock"));
    serial.setBaudRate(115200);
    if (!serial.open(QIODevice::ReadWrite)) return 1;
    const QJsonObject attach{{"event", "attach"}, {"module_id", 80}, {"module_instance", "mock-1"}, {"transport_mode", "fixed_ble_and_hotspot_lan"}};
    serial.write(PiBridge::encode(0, PiBridge::ControlEvent, 1, QJsonDocument(attach).toJson(QJsonDocument::Compact)));
    while (serial.bytesToWrite() > 0)
        if (!serial.waitForBytesWritten(1000)) return 2;
    return 0;
}
