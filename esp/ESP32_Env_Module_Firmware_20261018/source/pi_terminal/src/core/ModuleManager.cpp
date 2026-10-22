#include "ModuleManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <QStandardPaths>

ModuleManager::~ModuleManager() { for (const auto &key : instances_.keys()) unload(key); }
bool ModuleManager::load(const QString &session, int id, const QString &directory, const QJsonObject &transport) {
    auto fail = [this, &session](const QString &s) { emit error(session, s); return false; };
    if (instances_.contains(session)) return fail("duplicate session");
    QFile file(directory + "/manifest.json"); if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto m = doc.object();
    if (parse.error != QJsonParseError::NoError || m.value("module_id").toDouble(-1) != id || id < 1 || id > 255 ||
        m.value("id").toString().isEmpty() || m.value("abi_version").toDouble(-1) != 1 ||
        !m.value("host_arch").toArray().contains(QSysInfo::currentCpuArchitecture()) || m.value("qt_major").toInt() != 6 ||
        m.value("unload_contract").toString() != "synchronous-stop-v1") return fail("manifest incompatible");
    for (const auto &key : {"library", "qml_page"}) {
        const QString relative = m.value(key).toString();
        const QString path = QFileInfo(directory + '/' + relative).canonicalFilePath();
        if (relative.isEmpty() || QDir::isAbsolutePath(relative) || !path.startsWith(QDir(directory).canonicalPath() + '/')) return fail("plugin path escapes package");
    }
    auto i = QSharedPointer<Instance>::create(); i->owner = this; i->session = session; i->directory = directory; i->manifest = m;
    i->library.setFileName(directory + '/' + m.value("library").toString());
    i->library.setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!i->library.load()) return fail(i->library.errorString());
    for (const auto *name : {"pi_module_api_version", "pi_module_manifest_json", "pi_module_create", "pi_module_destroy",
                            "pi_module_on_attach", "pi_module_on_detach", "pi_module_stop", "pi_module_on_data",
                            "pi_module_on_transport_connected", "pi_module_qml_page", "pi_module_action", "pi_module_on_disconnect_request"}) {
        if (!i->library.resolve(name)) { i->library.unload(); return fail(QString("missing entry: ") + name); }
    }
    auto version = reinterpret_cast<uint32_t (*)()>(i->library.resolve("pi_module_api_version"));
    auto embedded = reinterpret_cast<const char *(*)()>(i->library.resolve("pi_module_manifest_json"));
    if (version() != 1 || QJsonDocument::fromJson(embedded()).object() != m) {
        i->library.unload(); return fail("embedded manifest/ABI differs");
    }
    i->services = {1, i.data(), sendControl, sendData, publishAction, publishEvent, dataDirectory,
                   [](int, const char *s) { qInfo("plugin: %s", s ? s : ""); }, emitEvent};
    auto create = reinterpret_cast<pi_module_handle_t *(*)(const pi_host_services_t *)>(i->library.resolve("pi_module_create"));
    i->handle = create(&i->services);
    if (!i->handle) { i->library.unload(); return fail("create returned null"); }
    instances_.insert(session, i);
    auto qmlPage = reinterpret_cast<const char *(*)(pi_module_handle_t *)>(i->library.resolve("pi_module_qml_page"));
    if (QString::fromUtf8(qmlPage(i->handle)) != m.value("qml_page").toString()) { unload(session); return fail("page getter differs from verified manifest"); }
    auto attach = reinterpret_cast<int (*)(pi_module_handle_t *, const char *)>(i->library.resolve("pi_module_on_attach"));
    if (attach(i->handle, QJsonDocument(transport).toJson(QJsonDocument::Compact).constData()) != 0) { unload(session); return fail("attach rejected"); }
    return true;
}
void ModuleManager::publishAction(const char *action, const char *text) {
    qInfo("plugin action: %s (%s)", action ? action : "", text ? text : "");
}
void ModuleManager::publishEvent(const char *type, const char *json) {
    qInfo("plugin event: %s %s", type ? type : "", json ? json : "");
}
const char *ModuleManager::dataDirectory() {
    static const QByteArray path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toUtf8();
    QDir().mkpath(QString::fromUtf8(path));
    return path.constData();
}
void ModuleManager::unload(const QString &session) {
    auto i = instances_.value(session); if (!i || i->stopping) return; i->stopping = true;
    for (const auto *name : {"pi_module_on_detach", "pi_module_stop", "pi_module_destroy"})
        reinterpret_cast<void (*)(pi_module_handle_t *)>(i->library.resolve(name))(i->handle);
    i->handle = nullptr;
    instances_.remove(session);
    const auto libraryPath = i->library.fileName();
    // QLibrary objects for the same file share native state. Keep all library
    // leases until every instance has completed its plugin cleanup callbacks.
    stoppedLibraries_[libraryPath].append(i);
    for (const auto &active : instances_)
        if (active->library.fileName() == libraryPath) return;
    const auto stopped = stoppedLibraries_.take(libraryPath);
    for (const auto &instance : stopped)
        if (instance->library.isLoaded() && !instance->library.unload())
            emit error(instance->session, instance->library.errorString());
}
void ModuleManager::data(const QString &session, quint8 t, const QByteArray &p) {
    auto i = instances_.value(session); if (!i || i->stopping) return;
    reinterpret_cast<int (*)(pi_module_handle_t *, uint8_t, const uint8_t *, uint32_t)>(i->library.resolve("pi_module_on_data"))
        (i->handle, t, reinterpret_cast<const uint8_t *>(p.constData()), uint32_t(p.size()));
}
void ModuleManager::connected(const QString &s, quint8 t) {
    auto i = instances_.value(s); if (i && !i->stopping)
        reinterpret_cast<void (*)(pi_module_handle_t *, uint8_t)>(i->library.resolve("pi_module_on_transport_connected"))(i->handle, t);
}
void ModuleManager::action(const QString &s, const QString &a) {
    auto i = instances_.value(s); if (i && !i->stopping && i->manifest.value("actions").toArray().contains(a))
        reinterpret_cast<int (*)(pi_module_handle_t *, const char *)>(i->library.resolve("pi_module_action"))(i->handle, a.toUtf8().constData());
}
void ModuleManager::disconnectRequest(const QString &s, const QJsonObject &request) {
    auto i = instances_.value(s); if (!i || i->stopping) return;
    reinterpret_cast<void (*)(pi_module_handle_t *, const char *)>(i->library.resolve("pi_module_on_disconnect_request"))
        (i->handle, QJsonDocument(request).toJson(QJsonDocument::Compact).constData());
}
QJsonObject ModuleManager::manifest(const QString &s) const { auto i = instances_.value(s); return i ? i->manifest : QJsonObject{}; }
QString ModuleManager::page(const QString &s) const { auto i = instances_.value(s); return i ? QUrl::fromLocalFile(i->directory + '/' + i->manifest.value("qml_page").toString()).toString() : QString{}; }
int ModuleManager::sendControl(void *c, uint8_t cmd, const uint8_t *p, uint16_t n, uint16_t *seq) {
    auto *i = static_cast<Instance *>(c);
    if (!i || i->stopping || (!p && n) || QThread::currentThread() != i->owner->thread() || !i->owner->controlSender) return -1;
    return i->owner->controlSender(i->session, cmd, QByteArray(reinterpret_cast<const char *>(p), n), seq) ? 0 : -1;
}
int ModuleManager::sendData(void *c, uint8_t t, const uint8_t *p, uint32_t n) {
    auto *i = static_cast<Instance *>(c);
    if (!i || i->stopping || (!p && n) || n > 65536 || QThread::currentThread() != i->owner->thread() || !i->owner->sender) return -1;
    return i->owner->sender(i->session, t, QByteArray(reinterpret_cast<const char *>(p), int(n))) ? 0 : -1;
}
void ModuleManager::emitEvent(void *c, const char *type, const char *json) {
    auto *i = static_cast<Instance *>(c);
    if (i && !i->stopping && type && json && QThread::currentThread() == i->owner->thread()) emit i->owner->event(i->session, QString::fromUtf8(type), QString::fromUtf8(json));
}
