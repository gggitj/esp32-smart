#include "BleTransport.h"
#include <QtEndian>
#include "protocol/data_checksum.h"

BleTransport::BleTransport(QObject *parent) : TransportBase(parent), discovery_(this) {
    timer_.setSingleShot(true); timer_.setInterval(10000);
    connect(&timer_, &QTimer::timeout, this, [this] { assembling_.clear(); next_ = count_ = 0; });
    discoveryTimeout_.setSingleShot(true); discoveryTimeout_.setInterval(10000);
    connect(&discoveryTimeout_, &QTimer::timeout, this, [this] { if (discovery_.isActive()) discovery_.stop(); emit error("BLE device discovery timeout"); emit disconnected("BLE device not found"); });
    connect(&discovery_, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &BleTransport::found);
    connect(&discovery_, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](auto) { emit error(discovery_.errorString()); });
    pump_.setInterval(20);
    writeTimeout_.setSingleShot(true); writeTimeout_.setInterval(3000);
    connect(&writeTimeout_, &QTimer::timeout, this, [this] {
        writeInFlight_ = false; emit error("BLE write acknowledgement timeout");
        if (++writeFailures_ >= 3) { close(); emit disconnected("BLE write failed"); }
        else pump_.start();
    });
    connect(&pump_, &QTimer::timeout, this, [this] {
        if (!ready_ || pending_.isEmpty() || writeInFlight_) return;
        const auto fragment = pending_.first();
        // The ESP module exposes both Write and Write Without Response. Prefer
        // no-response writes so bidirectional notification bursts (especially
        // BLE package_get/package_chunk) cannot stall the ATT write-ack timer.
        // The pump still sends fragments in order with a 20 ms inter-fragment
        // gap; peers that lack this property retain Write With Response.
        const bool response = !tx_.properties().testFlag(QLowEnergyCharacteristic::WriteNoResponse);
        if (!service_) return;
        service_->writeCharacteristic(tx_, fragment, response ? QLowEnergyService::WriteWithResponse : QLowEnergyService::WriteWithoutResponse);
        if (!response) { pending_.removeFirst(); pump_.start(); }
        else { writeInFlight_ = true; writeTimeout_.start(); pump_.stop(); }
    });
}
void BleTransport::start(const QJsonObject &config) {
    close(); config_ = config;
    for (const auto &key : {"service_uuid", "write_uuid", "notify_uuid"})
        if (QBluetoothUuid(config.value(key).toString()).isNull()) { emit error("BLE UUID missing"); return; }
    if (config.value("device_name").toString().isEmpty()) { emit error("BLE name missing"); return; }
    if (config.value("address").toString().isEmpty()) { emit error("BLE address binding missing"); return; }
    discoveryTimeout_.start(); discovery_.start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}
void BleTransport::found(const QBluetoothDeviceInfo &info) {
    if (controller_ || info.name() != config_.value("device_name").toString()) return;
    const QString address = config_.value("address").toString();
    if (!address.isEmpty() && info.address().toString().compare(address, Qt::CaseInsensitive)) return;
    discovery_.stop(); discoveryTimeout_.stop(); controller_ = QLowEnergyController::createCentral(info, this);
    connect(controller_, &QLowEnergyController::connected, controller_, &QLowEnergyController::discoverServices);
    connect(controller_, &QLowEnergyController::errorOccurred, this, [this](auto) { emit error(controller_->errorString()); });
    connect(controller_, &QLowEnergyController::disconnected, this, [this] { ready_ = false; pump_.stop(); emit disconnected("BLE disconnected"); });
    connect(controller_, &QLowEnergyController::discoveryFinished, this, [this] {
        service_ = controller_->createServiceObject(QBluetoothUuid(config_.value("service_uuid").toString()), this);
        if (!service_) { emit error("BLE service not found"); return; }
        connect(service_, &QLowEnergyService::errorOccurred, this, [this](auto) {
            writeTimeout_.stop(); writeInFlight_ = false; emit error("BLE service operation failed");
            if (++writeFailures_ >= 3) { close(); emit disconnected("BLE write failed"); } else pump_.start();
        });
        connect(service_, &QLowEnergyService::characteristicWritten, this, [this](auto c, auto) {
            if (c != tx_) return;
            writeTimeout_.stop(); writeInFlight_ = false; writeFailures_ = 0;
            if (!pending_.isEmpty()) pending_.removeFirst();
            pump_.start();
        });
        connect(service_, &QLowEnergyService::characteristicChanged, this, [this](const auto &c, const auto &value) { if (c == rx_) receive(value); });
        connect(service_, &QLowEnergyService::descriptorWritten, this, [this](const auto &descriptor, const QByteArray &v) {
            const auto cccd = rx_.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (!ready_ && descriptor == cccd && (v == QByteArray::fromHex("0100") || v == QByteArray::fromHex("0200"))) { ready_ = true; pump_.start(); emit connected(); }
        });
        connect(service_, &QLowEnergyService::stateChanged, this, [this](auto state) {
            if (state != QLowEnergyService::RemoteServiceDiscovered) return;
            tx_ = service_->characteristic(QBluetoothUuid(config_.value("write_uuid").toString()));
            rx_ = service_->characteristic(QBluetoothUuid(config_.value("notify_uuid").toString()));
            const auto cccd = rx_.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (!tx_.isValid() || !rx_.isValid() || !cccd.isValid() ||
                !(rx_.properties() & (QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Indicate)) ||
                !(tx_.properties() & (QLowEnergyCharacteristic::Write | QLowEnergyCharacteristic::WriteNoResponse))) {
                emit error("BLE characteristics invalid"); return;
            }
            service_->writeDescriptor(cccd, rx_.properties().testFlag(QLowEnergyCharacteristic::Notify) ? QByteArray::fromHex("0100") : QByteArray::fromHex("0200"));
        });
        service_->discoverDetails();
    });
    controller_->connectToDevice();
}
bool BleTransport::send(const QByteArray &payload) {
    if (!ready_ || payload.size() > 65532 || pending_.size() + (payload.size() + 17) / 14 > 6000) return false;
    const QByteArray data = payload + dataChecksum(payload);
    const quint16 chunks = quint16((data.size() + 13) / 14); ++sendId_;
    for (quint16 i = 0; i < chunks; ++i) {
        QByteArray f(6, '\0'); auto *p = reinterpret_cast<uchar *>(f.data());
        qToLittleEndian(sendId_, p); qToLittleEndian(i, p + 2); qToLittleEndian(chunks, p + 4);
        pending_.append(f + data.mid(i * 14, 14));
    }
    return true;
}
void BleTransport::receive(const QByteArray &f) {
    if (f.size() < 6 || f.size() > 20) return;
    const auto *p = reinterpret_cast<const uchar *>(f.constData());
    const auto id = qFromLittleEndian<quint16>(p), index = qFromLittleEndian<quint16>(p + 2), n = qFromLittleEndian<quint16>(p + 4);
    if (!n || n > 4682 || index >= n) return;
    if (index == 0) { assembling_.clear(); receiveId_ = id; next_ = 0; count_ = n; }
    if (id != receiveId_ || index != next_ || n != count_) { assembling_.clear(); next_ = count_ = 0; return; }
    assembling_ += f.mid(6); ++next_; timer_.start();
    if (assembling_.size() > 65536) { assembling_.clear(); next_ = count_ = 0; timer_.stop(); return; }
    if (next_ == count_) {
        auto data = assembling_.chopped(4);
        if (assembling_.size() >= 4 && dataChecksum(data) == assembling_.right(4)) emit payloadReceived(1, data);
        timer_.stop(); assembling_.clear(); next_ = count_ = 0;
    }
}
void BleTransport::close() {
    discovery_.stop(); discoveryTimeout_.stop(); timer_.stop(); pump_.stop(); writeTimeout_.stop(); pending_.clear(); assembling_.clear(); writeInFlight_ = false; writeFailures_ = 0; ready_ = false;
    if (service_) { disconnect(service_, nullptr, this, nullptr); delete service_; service_ = nullptr; }
    if (controller_) { disconnect(controller_, nullptr, this, nullptr); delete controller_; controller_ = nullptr; }
}
