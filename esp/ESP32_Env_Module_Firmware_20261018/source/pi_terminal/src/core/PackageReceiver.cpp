#include "PackageReceiver.h"
#include <QJsonDocument>
#include <QSysInfo>
#include <QUuid>

PackageReceiver::PackageReceiver(QObject *parent) : QObject(parent) {
    timer_.setSingleShot(true); timer_.setInterval(15000);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (++retries_ > 3) fail("插件分片传输超时"); else ask();
    });
    connect(&verifier_, &QProcess::errorOccurred, this, [this](auto) { fail("插件元数据验证器启动失败"); });
    connect(&verifier_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, auto exit) {
        if (!active_) return;
        if (code || exit != QProcess::NormalExit) { fail("插件签名/元数据验证失败: " + QString::fromUtf8(verifier_.readAllStandardError()).right(512)); return; }
        const auto desc = QJsonDocument::fromJson(verifier_.readAllStandardOutput()).object();
        const auto size = desc.value("size").toInteger();
        if (size < 1 || size > 1024*1024) { fail("BLE 小包上限为 1 MiB，请使用热点 LAN"); return; }
        openFile("package.tar.zst", size);
    });
}
void PackageReceiver::start(const QString &python, const QString &tool, const QString &key, int module) {
    python_ = python; tool_ = tool; key_ = key; module_ = module; active_ = true;
    if (!directory_.isValid()) { fail("无法创建插件接收目录"); return; }
    openFile("package.json");
}
void PackageReceiver::openFile(const QString &name, qint64 size) {
    file_.close(); fileName_ = name; expectedSize_ = size; offset_ = 0; retries_ = 0;
    transfer_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    file_.setFileName(directory_.filePath(name));
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail(file_.errorString()); return; }
    ask();
}
void PackageReceiver::ask() {
    if (!active_) return;
    emit request({{"transfer_id", transfer_}, {"file", fileName_}, {"offset", double(offset_)}, {"length", 512}});
    timer_.start();
}
bool PackageReceiver::receive(const QJsonObject &message) {
    if (!active_ || !file_.isOpen() || message.value("transfer_id").toString() != transfer_ ||
        message.value("file").toString() != fileName_ || message.value("offset").toDouble(-1) != offset_) return false;
    const auto decoded = QByteArray::fromBase64Encoding(message.value("payload").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    const auto total = message.value("total").toDouble(-1);
    const qint64 limit = fileName_ == "package.json" ? 262144 : 1024*1024;
    if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > 512 || total < 1 || total > limit || total != qint64(total) ||
        (expectedSize_ >= 0 && total != expectedSize_) || offset_ + decoded.decoded.size() > total) {
        fail("插件分片长度或编码无效"); return false;
    }
    qInfo().noquote() << "Plugin package chunk received:" << fileName_ << "offset=" << offset_
                      << "bytes=" << decoded.decoded.size() << "total=" << qint64(total);
    expectedSize_ = qint64(total);
    if (file_.write(decoded.decoded) != decoded.decoded.size()) { fail(file_.errorString()); return false; }
    timer_.stop(); retries_ = 0; offset_ += decoded.decoded.size();
    emit progress(fileName_ == "package.json" ? 10 : 15 + int(65 * offset_ / expectedSize_));
    if (offset_ != expectedSize_) { ask(); return true; }
    file_.close();
    if (fileName_ == "package.json") {
        emit verifying();
        verifier_.start(python_, {tool_, "verify-metadata", directory_.path(), key_, QString::number(module_), QSysInfo::currentCpuArchitecture()});
        QTimer::singleShot(15000, &verifier_, [this] { if (verifier_.state() != QProcess::NotRunning) { verifier_.kill(); fail("签名验证超时"); } });
    } else { active_ = false; emit finished(directory_.path()); }
    return true;
}
void PackageReceiver::cancel() {
    active_ = false; timer_.stop(); file_.close();
    if (verifier_.state() != QProcess::NotRunning) { verifier_.kill(); verifier_.waitForFinished(2000); }
}
void PackageReceiver::fail(const QString &message) {
    if (!active_) return;
    qWarning().noquote() << "Plugin package transfer failed:" << message << "file=" << fileName_ << "offset=" << offset_;
    cancel(); emit failed(message);
}
