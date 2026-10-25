#include "ConnectionManager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <QStandardPaths>
#include <QSysInfo>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSettings>
#include <QDir>

static qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
ConnectionManager::ConnectionManager(QObject *parent) : QObject(parent), modules_(this), pogo_(this) {
    connect(this, &ConnectionManager::cardsChanged, this, [this] { cards_.synchronize(cards()); });
    connect(&pogo_, &TransportBase::connected, &hostEvents_, &HostEventDetector::reset);
    connect(&pogo_, &TransportBase::controlEvent, &hostEvents_, &HostEventDetector::consume);
    connect(&hostEvents_, &HostEventDetector::detected, this, &ConnectionManager::attach);
    connect(&pogo_, &TransportBase::disconnected, this, [this](const auto &reason) {
        status_ = "控制桥断开：" + reason;
        emit cardsChanged();
    });
    connect(&pogo_, &TransportBase::error, this, [this](const auto &s) { status_ = s; emit cardsChanged(); });
    connect(&modules_, &ModuleManager::error, this, [this](const auto &id, const auto &m) {
        auto s = sessions_.value(id); if (s) state(s, "error", m);
    });
    connect(&modules_, &ModuleManager::event, this, [this](const auto &id, const auto &, const auto &json) {
        auto s = sessions_.value(id); if (s && !s->stopping) {
            pages_.update(id, QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap());
            s->message = json.left(2048); emit cardsChanged();
        }
    });
    modules_.sender = [this](const QString &id, quint8 t, const QByteArray &data) {
        auto s = sessions_.value(id);
        return s && s->loaded && s->ready && !s->stopping && send(s, "data", {{"payload", QString::fromLatin1(data.toBase64())}}, t);
    };
    modules_.controlSender = [this](const QString &id, quint8 cmd, const QByteArray &payload, quint16 *seq) {
        auto s = sessions_.value(id); if (!s || s->stopping) return false;
        if (seq) *seq = quint16(++s->tx);
        return pogo_.sendControl({{"module_instance", id}, {"module_id", s->id}, {"command", cmd},
                                  {"sequence", int(s->tx)}, {"payload", QString::fromLatin1(payload.toBase64())}});
    };
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, [this] {
        for (auto s : sessions_) {
            if (s->stopping) continue;
            if (s->ready) {
                if (s->closePending && now() >= s->closeDeadline) {
                    if (s->awaitingPeerClose) { stop(s, "等待模块关闭确认超时，已安全关闭本地会话"); continue; }
                    if (s->closeRetries >= 3) { stop(s, "断开确认超时，已安全关闭本地会话"); continue; }
                    ++s->closeRetries;
                    sendCloseRequest(s);
                    continue;
                }
                if (now() - s->lastSeen > 6000) lost(s->instance, "数据超时，正在重连");
                else if (!s->closePending) send(s, "ping");
            } else if (s->lostAt && now() - s->lostAt > 30000) stop(s, "重连超时");
            else if (s->retryAt && now() >= s->retryAt) { s->retryAt = now() + 3000; connectData(s); }
        }
    });
    tick_.start();
}
ConnectionManager::~ConnectionManager() {
    tick_.stop();
    for (auto s : sessions_) {
        s->stopping = true;
        if (s->receiver) s->receiver->cancel();
        if (s->installer) { s->installer->disconnect(this); s->installer->kill(); s->installer->waitForFinished(2000); }
        if (s->lan) s->lan->close();
        if (s->ble) s->ble->close();
        modules_.unload(s->instance);
    }
}
bool ConnectionManager::configure(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) { status_ = f.errorString(); return false; }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { status_ = "配置 JSON 无效: " + error.errorString(); return false; }
    config_ = doc.object(); profiles_ = config_.value("modules").toObject();
    if (profiles_.isEmpty()) { status_ = "配置缺少 modules"; return false; }
    const QDir base = QFileInfo(path).absoluteDir();
    for (const auto *key : {"public_key", "package_tool", "cache"}) {
        const auto value = config_.value(key).toString();
        if (!value.isEmpty() && QDir::isRelativePath(value)) config_[key] = base.absoluteFilePath(value);
    }
    return true;
}
bool ConnectionManager::start(const QString &device) { return pogo_.open(device); }
QVariantList ConnectionManager::cards() const {
    QVariantList list;
    for (auto s : sessions_) {
        list.append(QVariantMap{{"instance", s->instance}, {"moduleId", s->id}, {"state", s->state},
            {"message", s->message}, {"progress", s->progress}, {"contact", s->contact},
            {"ready", s->ready && s->loaded && !s->stopping}, {"page", modules_.page(s->instance)},
            {"title", modules_.manifest(s->instance).value("name").toString(QString("模块 %1").arg(s->id))},
            {"icon", modules_.manifest(s->instance).value("icon").toString()},
            {"actions", modules_.manifest(s->instance).value("actions").toArray().toVariantList()}});
    }
    return list;
}
void ConnectionManager::state(const QSharedPointer<Session> &s, const QString &value, const QString &text) {
    qInfo().noquote() << "Module session state:" << s->id << s->instance << value << text;
    s->machine->request(value, text);
}
void ConnectionManager::attach(const QJsonObject &e) {
    if (!HostEventDetector::valid(e)) return;
    const auto instance = e.value("module_instance").toString();
    const auto type = e.value("event").toString();
    if (type == "detach") {
        suppressedInstances_.remove(instance);
        auto s = sessions_.value(instance);
        if (s && e.value("module_id").toInt(-1) == s->id) {
            if (s->contact) emit deviceDetached(instance);
            s->contact = false;
            if (s->loaded && s->ready) state(s, "contact_detached", "触点已拔出，数据会话继续");
            emit cardsChanged();
        }
        return;
    }
    if (suppressedInstances_.contains(instance)) return;
    const double numericId = e.value("module_id").toDouble(-1);
    if (type != "attach" || numericId < 1 || numericId > 255 || numericId != int(numericId) ||
        !QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(instance).hasMatch()) return;
    if (sessions_.contains(instance)) {
        auto s = sessions_[instance];
        if (s->id != int(numericId) || s->stopping) return;
        if (s->contact) return; // Periodic live-state announcements are idempotent.
        s->contact = true;
        if (s->loaded && s->ready) state(s, "ready", "模块触点已恢复");
        emit cardsChanged(); return;
    }
    if (sessions_.size() >= 8) { status_ = "模块数量已达上限"; emit cardsChanged(); return; }
    auto s = QSharedPointer<Session>::create(); s->id = int(numericId); s->instance = instance;
    s->profile = profiles_.value(QString::number(s->id)).toObject(); sessions_.insert(instance, s);
    QTimer::singleShot(0, this, [this, instance] {
        auto session = sessions_.value(instance);
        if (session && !session->stopping) emit deviceAttached(instance, session->id);
    });
    s->machine = new SessionStateMachine(this);
    connect(s->machine, &SessionStateMachine::changed, this, [this, instance](const QString &value, const QString &text) {
        auto session = sessions_.value(instance); if (!session) return;
        session->state = value; session->message = text; status_ = text;
        emit cardsChanged(); emit announcement(text);
        if (value == "unloading" && session->releasing) {
            modules_.unload(instance); pages_.remove(instance); session->loaded = false;
            state(session, "closed", "模块已断开");
        } else if (value == "closed" && session->releasing) finishRelease(session);
    });
    state(s, "attached", "检测到模块，Host 已完成唤醒确认");
    if (s->profile.isEmpty() || s->profile.value("token").toString().isEmpty()) { state(s, "error", "缺少预置模块配置/认证信息"); return; }
    const auto mode = s->profile.value("transport_mode").toString();
    if (mode != "fixed_ble" && mode != "fixed_hotspot_lan" && mode != "fixed_ble_and_hotspot_lan") { state(s, "error", "未知传输配置"); return; }
    if (mode != "fixed_ble") {
        s->lan = new HotspotLanTransport(this);
        connect(s->lan, &TransportBase::connected, this, [this, instance] {
            auto s = sessions_.value(instance); if (!s || s->stopping) return; s->lanConnected = true;
            send(s, "session_open", {{"token", s->profile.value("token")}, {"transport_choice", "hotspot_lan"}}, 2);
        });
        connect(s->lan, &TransportBase::payloadReceived, this, [this, instance](quint8 t, const auto &p) { receive(instance, t, p); });
        connect(s->lan, &TransportBase::disconnected, this, [this, instance](const auto &m) {
            auto s = sessions_.value(instance); if (s) transportLost(s, 2, m);
        });
        connect(s->lan, &TransportBase::error, this, [this, instance](const auto &m) {
            auto s = sessions_.value(instance); if (s) transportLost(s, 2, m);
        });
    }
    if (mode != "fixed_hotspot_lan") {
        s->ble = new BleTransport(this);
        connect(s->ble, &TransportBase::connected, this, [this, instance] {
            auto s = sessions_.value(instance); if (!s || s->stopping) return; s->bleConnected = true;
            send(s, "session_open", {{"token", s->profile.value("token")}, {"transport_choice", "ble"}}, 1);
        });
        connect(s->ble, &TransportBase::payloadReceived, this, [this, instance](quint8 t, const auto &p) { receive(instance, t, p); });
        connect(s->ble, &TransportBase::error, this, [this, instance](const auto &m) { auto s = sessions_.value(instance); if (s) transportLost(s, 1, m); });
        connect(s->ble, &TransportBase::disconnected, this, [this, instance](const auto &m) { auto s = sessions_.value(instance); if (s) transportLost(s, 1, m); });
    }
    s->lostAt = now(); s->retryAt = now() + 10000;
    state(s, "connecting", "正在连接模块"); connectData(s);
}
void ConnectionManager::connectData(const QSharedPointer<Session> &s) {
    // Establish one candidate at a time: LAN first for packages, then BLE if
    // the configured dual-link mode cannot establish the LAN session. Never
    // let two competing session_open messages select the main data stream.
    const bool dual = s->lan && s->ble;
    if (!s->candidateTransport) s->candidateTransport = s->lan ? 2 : 1;
    else if (dual && !s->ready) s->candidateTransport = s->candidateTransport == 2 ? 1 : 2;
    if (dual) {
        // Retire the other candidate before authenticating a new transport.
        // Its deliberate close callback must not trigger a competing retry.
        if (s->candidateTransport == 1) s->lan->close();
        else s->ble->close();
    }
    qInfo().noquote() << "Session handshake candidate:" << s->instance
                      << (s->candidateTransport == 2 ? "hotspot_lan" : "ble");
    if (s->candidateTransport == 2 && s->lan)
        s->lan->connectToModule(QHostAddress(s->profile.value("address").toString()), quint16(s->profile.value("port").toInt()));
    else if (s->ble) s->ble->start(s->profile.value("ble").toObject());
}
bool ConnectionManager::send(const QSharedPointer<Session> &s, const QString &type, const QJsonObject &body, quint8 t) {
    QJsonObject e = body; e["event"] = type; e["module_id"] = s->id; e["module_instance"] = s->instance;
    e["seq"] = double(++s->tx); e["version"] = 1;
    const auto p = QJsonDocument(e).toJson(QJsonDocument::Compact);
    if (!t) t = s->activeTransport ? s->activeTransport : (s->lanConnected ? 2 : s->bleConnected ? 1 : s->lan ? 2 : 1);
    if (t == 2) return s->lan && s->lan->sendData(p);
    return t == 1 && s->ble && s->ble->sendData(p);
}
void ConnectionManager::receive(const QString &instance, quint8 t, const QByteArray &p) {
    auto s = sessions_.value(instance); if (!s || s->stopping) return;
    const auto e = QJsonDocument::fromJson(p).object();
    if (e.value("module_id").toDouble(-1) != s->id || e.value("module_instance").toString() != instance || e.value("version").toInt() != 1) return;
    const auto seq = e.value("seq").toDouble(-1);
    if (seq < 1 || seq > 4294967295.0 || seq != quint32(seq)) return;
    const auto type = e.value("event").toString();
    if (type == "session_ready") {
        if (s->stopping || (t == 1 && !s->bleConnected) || (t == 2 && !s->lanConnected)) return;
        if (e.value("token").toString() != s->profile.value("token").toString()) { lost(instance, "模块身份验证失败"); return; }
        const auto selected = e.value("selected_transport").toString();
        if (!selected.isEmpty() && selected != (t == 1 ? "ble" : "hotspot_lan")) {
            lost(instance, "模块连接方式确认不匹配"); return;
        }
        if (s->candidateTransport && s->candidateTransport != t) return;
        if (s->ready && s->activeTransport != t) return;
        if (s->ready) return; // A replay must not reset the receive sequence or reload a plugin.
        s->rx = quint32(seq); s->ready = true; s->lastSeen = now(); s->lostAt = s->retryAt = 0;
        s->activeTransport = t;
        qInfo().noquote() << "Session handshake selected:" << instance << (t == 1 ? "ble" : "hotspot_lan");
        if (!s->loaded && s->packagePath.isEmpty()) install(s);
        else if (s->loaded) { modules_.connected(instance, t); state(s, s->contact ? "ready" : "contact_detached", "模块连接已恢复"); emit dataRestored(instance); }
        else state(s, "awaiting_trust", "等待确认模块权限");
        return;
    }
    if (!s->ready || (s->activeTransport && t != s->activeTransport)) return;
    const quint32 incoming = quint32(seq);
    const quint32 distance = incoming - s->rx;
    if (distance == 0 || distance >= 0x80000000u) return;
    QByteArray decodedPayload;
    if (type == "package_chunk") {
        if (s->receiver && s->receiver->receive(e)) { s->rx = incoming; s->lastSeen = now(); }
        return;
    } else if (type == "data") {
        if (!e.value("payload").isString()) return;
        const auto decoded = QByteArray::fromBase64Encoding(e.value("payload").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.size() > 65536) return;
        decodedPayload = decoded.decoded;
    } else if (type == "module_disconnect_request") {
        if (!e.value("stop_required").isBool() || !e.value("auto_reconnect").isBool()) return;
    } else if (type == "disconnect_ack") {
        if (!e.value("accepted").isBool()) return;
    } else if (type != "pong" && type != "session_close") return;
    s->rx = incoming; s->lastSeen = now();
    if (type == "disconnect_ack") {
        if (!s->closePending || s->awaitingPeerClose) return;
        if (e.value("accepted").toBool()) stop(s, "模块已确认断开");
        else { s->closePending = false; s->closeRetries = 0; state(s, s->contact ? "ready" : "contact_detached", "模块拒绝断开"); }
    } else if (type == "session_close") {
        if (s->closePending && s->awaitingPeerClose) stop(s, "模块已完成断开");
    } else if (type == "module_disconnect_request") {
        const QStringList reasons{"user_button", "low_battery", "fault", "enter_sleep", "reboot", "maintenance"};
        if (!reasons.contains(e.value("reason").toString())) {
            send(s, "disconnect_ack", {{"accepted",false},{"reason","invalid_reason"}}); return;
        }
        const bool accepted = s->profile.value("allow_disconnect").toBool(true);
        if (s->loaded) modules_.disconnectRequest(instance, e);
        // Periodic attach announcements must not undo an explicit disconnect
        // while contact is still present. If DET was already removed, however,
        // the next attach is a new insertion: there will be no further detach
        // to clear a newly inserted suppression entry.
        if (accepted && !e.value("auto_reconnect").toBool() && s->contact)
            suppressedInstances_.insert(instance);
        send(s, "disconnect_ack", {{"accepted", accepted}, {"reason", accepted ? "accepted" : "host_policy"}});
        if (accepted) { s->closePending = true; s->awaitingPeerClose = true; s->closeDeadline = now() + 3000; }
    } else if (type == "data" && s->loaded) {
        if (!s->firstDataLogged) {
            qInfo().noquote() << "Module data received: module_id=" << s->id
                              << "instance=" << instance
                              << "transport=" << (t == 1 ? "BLE" : "LAN")
                              << "payload_bytes=" << decodedPayload.size();
            s->firstDataLogged = true;
        }
        modules_.data(instance, t, decodedPayload);
    }
}
void ConnectionManager::install(const QSharedPointer<Session> &s) {
    if (s->installer) return;
    const QString packageTransport = s->profile.value("package_transport").toString();
    const bool fetchPackageOverData = packageTransport == "data" ||
        (s->profile.value("transport_mode").toString() == "fixed_ble" && packageTransport != "http");
    if (fetchPackageOverData && s->incoming.isEmpty()) {
        if (s->receiver) return;
        s->progress = 5; state(s, "downloading_package", "正在通过数据链路接收插件包");
        auto *receiver = s->receiver = new PackageReceiver(this);
        const auto id = s->instance;
        connect(receiver, &PackageReceiver::request, this, [this, id](const QJsonObject &body) { auto s = sessions_.value(id); if (s && !s->stopping) send(s, "package_get", body); });
        connect(receiver, &PackageReceiver::progress, this, [this, id](int percent) { auto s = sessions_.value(id); if (s) { s->progress = percent; emit cardsChanged(); } });
        connect(receiver, &PackageReceiver::verifying, this, [this, id] { auto s = sessions_.value(id); if (s) state(s, "verifying_package", "正在验证发布者签名"); });
        connect(receiver, &PackageReceiver::failed, this, [this, id, receiver](const QString &message) {
            auto s = sessions_.value(id); if (s) { s->receiver = nullptr; state(s, "error", message); } receiver->deleteLater();
        });
        connect(receiver, &PackageReceiver::finished, this, [this, id](const QString &folder) { auto s = sessions_.value(id); if (s && !s->stopping) { s->incoming = folder; install(s); } });
        receiver->start(config_.value("python").toString("python3"), config_.value("package_tool").toString(PI_PACKAGE_TOOL), config_.value("public_key").toString(), s->id);
        return;
    }
    s->progress = 10; state(s, "downloading_package", "正在下载并验证模块功能");
    s->installer = new QProcess(this);
    auto *process = s->installer; const auto id = s->instance;
    connect(process, &QProcess::errorOccurred, this, [this, id, process](auto error) {
        auto s = sessions_.value(id);
        if (error == QProcess::FailedToStart) {
            if (s && s->installer == process) s->installer = nullptr;
            process->deleteLater();
        }
        if (s && !s->stopping) state(s, "error", "插件包验证进程失败: " + process->errorString());
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, id, process](int code, auto exit) {
        process->deleteLater();
        auto s = sessions_.value(id); if (!s) return;
        s->installer = nullptr;
        if (s->receiver) { s->receiver->cancel(); s->receiver->deleteLater(); s->receiver = nullptr; }
        if (s->stopping) return;
        if (code || exit != QProcess::NormalExit) { state(s, "error", "插件包验证失败: " + QString::fromUtf8(process->readAllStandardError()).right(1024)); return; }
        s->packagePath = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        QFile manifest(s->packagePath + "/manifest.json");
        if (!manifest.open(QIODevice::ReadOnly)) { state(s, "error", "无法读取已验证清单"); return; }
        s->verifiedManifest = QJsonDocument::fromJson(manifest.readAll()).object();
        QFile key(config_.value("public_key").toString()); key.open(QIODevice::ReadOnly);
        s->trustKey = QString::fromLatin1(QCryptographicHash::hash(key.readAll() + QJsonDocument(s->verifiedManifest).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        QSettings settings;
        if (settings.value("trusted/" + s->trustKey, false).toBool()) loadVerified(s);
        else {
            state(s, "awaiting_trust", "签名验证通过，请确认模块权限");
            QStringList permissions;
            for (const auto &v : s->verifiedManifest.value("permissions").toArray()) permissions.append(v.toString());
            emit trustRequired(id, s->verifiedManifest.value("name").toString(), permissions);
        }
    });
    process->start(config_.value("python").toString("python3"), {config_.value("package_tool").toString(PI_PACKAGE_TOOL),
        s->incoming.isEmpty() ? "install" : "install-local", s->incoming.isEmpty() ? s->profile.value("package_url").toString() : s->incoming, config_.value("public_key").toString(),
        config_.value("cache").toString(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/modules"),
        QString::number(s->id), QSysInfo::currentCpuArchitecture()});
    QTimer::singleShot(60000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
}
void ConnectionManager::trust(const QString &id, bool always) {
    auto s = sessions_.value(id);
    if (!s || s->stopping || s->loaded || s->packagePath.isEmpty() || s->trustKey.isEmpty()) return;
    if (always) { QSettings settings; settings.setValue("trusted/" + s->trustKey, true); settings.sync(); }
    loadVerified(s);
}
void ConnectionManager::loadVerified(const QSharedPointer<Session> &s) {
    s->progress = 90; state(s, "loading_plugin", "正在准备功能界面");
    pages_.registerPage(s->instance, {});
    auto transport = s->profile; transport["module_instance"] = s->instance;
    if (!modules_.load(s->instance, s->id, s->packagePath, transport)) { pages_.remove(s->instance); return; }
    pages_.registerPage(s->instance, modules_.page(s->instance));
    s->loaded = true; s->progress = 100;
    // Only shut down the AP when this package was received over BLE and no
    // other live session needs LAN. HTTP packages must not trigger this policy.
    if (s->activeTransport == 1 && !s->incoming.isEmpty() &&
        s->profile.value("transport_mode").toString() == "fixed_ble") {
        bool lanRequired = false;
        for (const auto &other : sessions_)
            if (!other->stopping && other->lan) { lanRequired = true; break; }
        if (!lanRequired) emit bleOnlyPackageReady(s->instance);
    }
    if (s->ready) { modules_.connected(s->instance, s->activeTransport ? s->activeTransport : (s->lanConnected ? 2 : 1)); state(s, "ready", "模块已连接"); }
    else state(s, "data_lost", "数据连接中断，功能已缓存");
}
void ConnectionManager::retry(const QString &id) {
    auto s = sessions_.value(id); if (!s || s->stopping || s->installer || s->state != "error") return;
    s->packagePath.clear(); s->trustKey.clear(); s->incoming.clear();
    if (s->receiver) { s->receiver->cancel(); s->receiver->deleteLater(); s->receiver = nullptr; }
    if (s->ready) install(s);
    else { s->lostAt = now(); s->retryAt = now() + 10000; state(s, "connecting", "正在重试连接"); connectData(s); }
}
void ConnectionManager::lost(const QString &id, const QString &reason) {
    auto s = sessions_.value(id); if (!s || s->stopping) return;
    if (s->ready) emit dataLost(id);
    s->ready = false; s->activeTransport = 0; s->lanConnected = false;
    if (!s->lostAt) s->lostAt = now();
    s->retryAt = now() + 3000;
    state(s, "data_lost", reason);
}
void ConnectionManager::transportLost(const QSharedPointer<Session> &s, quint8 transport, const QString &reason) {
    if (s->stopping) return;
    if (transport == 1) s->bleConnected = false; else s->lanConnected = false;
    if (!s->ready && s->candidateTransport && s->candidateTransport != transport) return;
    if (s->activeTransport != transport && s->ready) return;
    if (s->activeTransport == transport) {
        const bool wasReady = s->ready;
        s->ready = false; s->activeTransport = 0;
        if (wasReady) emit dataLost(s->instance);
    }
    if (s->bleConnected || s->lanConnected) {
        s->lostAt = now(); s->retryAt = now();
        state(s, "data_lost", reason + "，切换备用链路");
        const quint8 fallback = s->lanConnected ? 2 : 1;
        s->candidateTransport = fallback;
        send(s, "session_open", {{"token", s->profile.value("token")},
             {"transport_choice", fallback == 1 ? "ble" : "hotspot_lan"}}, fallback);
        return;
    }
    lost(s->instance, reason);
}
void ConnectionManager::disconnectModule(const QString &id) {
    auto s = sessions_.value(id); if (!s || s->stopping || s->closePending) return;
    if (!s->ready) { stop(s, "数据链路不可用，关闭本地会话"); return; }
    s->closePending = true; s->closeRetries = 0; sendCloseRequest(s);
}
void ConnectionManager::sendCloseRequest(const QSharedPointer<Session> &s) {
    s->closeDeadline = now() + 1000;
    if (!send(s, "stop") || !send(s, "session_close")) s->closeDeadline = now();
}
void ConnectionManager::stop(const QSharedPointer<Session> &s, const QString &reason) {
    if (s->stopping) return;
    s->stopping = true; s->ready = false;
    s->closePending = false;
    s->awaitingPeerClose = false;
    if (s->receiver) s->receiver->cancel();
    if (s->installer) s->installer->kill();
    if (s->lan) s->lan->close();
    if (s->ble) s->ble->close();
    state(s, "disconnecting", reason); emit pageMustClose(s->instance);
}
void ConnectionManager::releasePage(const QString &id) {
    auto s = sessions_.value(id); if (!s || !s->stopping || s->releasing) return;
    s->releasing = true; state(s, "unloading", "正在卸载模块");
}
void ConnectionManager::finishRelease(const QSharedPointer<Session> &s) {
    s->machine->deleteLater();
    if (s->receiver) s->receiver->deleteLater();
    if (s->lan) s->lan->deleteLater();
    if (s->ble) s->ble->deleteLater();
    sessions_.remove(s->instance); status_ = "模块已断开"; emit cardsChanged();
    emit sessionDisconnected(s->instance);
}
void ConnectionManager::action(const QString &id, const QString &name) {
    auto s = sessions_.value(id); if (s && s->ready && !s->stopping) modules_.action(id, name);
}
