#include "SystemTools.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QTimer>
#include <QProcess>
#include <QDateTime>
#include <QSaveFile>
#include <QStorageInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QImageReader>
#include <QRegularExpression>
#include <QHash>
#include <cmath>
QString SystemTools::saveCameraFrame(const QString &frame, const QUrl &destination) {
    const auto path=destination.isLocalFile()?destination.toLocalFile():destination.scheme().isEmpty()?destination.toString():QString{};
    if (!QDir::isAbsolutePath(path)) return "请输入本机文件的完整路径";
    if (!frame.startsWith("data:image/png;base64,") || frame.size()>8*1024*1024) return "照片数据无效";
    const auto decoded=QByteArray::fromBase64Encoding(frame.mid(22).toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || QImage::fromData(decoded.decoded,"PNG").isNull()) return "PNG 照片无法解码";
    if (QFileInfo::exists(path)) return "同名文件已存在，请更换文件名";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(decoded.decoded)!=decoded.decoded.size() || !file.commit())
        return "保存失败："+file.errorString();
    return {};
}
QString SystemTools::cameraPhotoPath() const {
    auto directory=QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if(!QFileInfo(directory).isDir())directory=QDir::homePath();
    return QDir(directory).filePath("camera-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".png");
}
void SystemTools::setLauncherOpacity(double value) {
    if(!std::isfinite(value))return;
    settings_.setValue("launcherOpacity",qBound(0.0,value,1.0));emit changed();
}
void SystemTools::setModuleOpacity(double value) {
    if(!std::isfinite(value))return;
    settings_.setValue("moduleOpacity",qBound(0.0,value,1.0));emit changed();
}
SystemTools::SystemTools(QObject *p) : QObject(p), speech_(this) {
    // The terminal UI text is Chinese, but the host OS may use an English
    // locale. Select Mandarin explicitly instead of letting the speech
    // backend choose its default voice.
    speech_.setLocale(QLocale(QStringLiteral("zh_CN")));
    QDir().mkpath(filesRoot());
    auto *timer = new QTimer(this); timer->setInterval(5000); connect(timer,&QTimer::timeout,this,&SystemTools::refreshStatus); timer->start();
    connect(&bluetooth_,&QBluetoothLocalDevice::hostModeStateChanged,this,[this](auto) { refreshStatus(); });
    connect(&bluetooth_,&QBluetoothLocalDevice::errorOccurred,this,[this](auto error) {
        log("蓝牙操作失败：" + QString::number(static_cast<int>(error)));
        refreshStatus();
    });
    refreshStatus();
}
QString SystemTools::filesRoot() const {
    // Start the file application at the current user's home directory.
    // safePath() still rejects traversal, absolute paths and symlink escapes.
    return QDir::homePath();
}
QUrl SystemTools::monochromeIcon(const QString &name, bool white) const {
    static QHash<QString,QUrl> cache;
    static const QRegularExpression valid("^[a-z0-9_]+$");
    const auto icon=valid.match(name).hasMatch()?name:QString("extension");
    const auto key=icon+(white?"/white":"/black");
    if(cache.contains(key))return cache.value(key);
    QFile file(":/icons/material/light/"+icon+".svg");
    if(!file.exists())file.setFileName(":/icons/material/dark/"+icon+".svg");
    if(!file.exists())file.setFileName(":/icons/material/light/extension.svg");
    if(!file.open(QIODevice::ReadOnly))return {};
    QString svg=QString::fromUtf8(file.readAll());
    static const QRegularExpression paint("(fill|stroke)=\"#[0-9a-fA-F]{3,8}\"");
    svg.replace(paint,QString("\\1=\"%1\"").arg(white?"#ffffff":"#000000"));
    const QUrl source(QString("data:image/svg+xml;base64,")+QString::fromLatin1(svg.toUtf8().toBase64()));
    cache.insert(key,source);return source;
}
QUrl SystemTools::wallpaper() const {
    const auto path=settings_.value("wallpaper").toString();
    return !path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).isReadable() ? QUrl::fromLocalFile(path) : QUrl{};
}
bool SystemTools::setWallpaper(const QUrl &url) {
    sampledWallpaper_ = QUrl{}; wallpaperSample_ = QImage{};
    if (url.isEmpty()) { settings_.remove("wallpaper");settings_.sync();emit changed();return true; }
    if (!url.isLocalFile()) { log("壁纸必须是本机图片文件");return false; }
    const auto path=QFileInfo(url.toLocalFile()).canonicalFilePath();
    QImageReader reader(path);
    const auto size=reader.size();
    if (path.isEmpty() || !reader.canRead() || !size.isValid() || qint64(size.width())*size.height()>100000000) {
        log("无法设置壁纸：图片无效或尺寸过大");return false;
    }
    reader.setAutoTransform(true);
    reader.setScaledSize(size.scaled(QSize(3840,2160),Qt::KeepAspectRatio));
    if (reader.read().isNull()) { log("无法设置壁纸："+reader.errorString());return false; }
    settings_.setValue("wallpaper",path);settings_.sync();emit changed();return true;
}
QColor SystemTools::wallpaperColor(double x, double y, double width, double height) const {
    const auto source=wallpaper();
    if (source!=sampledWallpaper_) {
        sampledWallpaper_=source;
        QImageReader reader(source.toLocalFile());reader.setAutoTransform(true);
        const auto size=reader.size();
        if(size.isValid())reader.setScaledSize(size.scaled(256,256,Qt::KeepAspectRatio));
        wallpaperSample_=source.isEmpty()?QImage{}:reader.read();
    }
    const QColor fallback("#08121f");
    if(wallpaperSample_.isNull() || width<=0 || height<=0)return fallback;
    const double scale=qMax(width/wallpaperSample_.width(),height/wallpaperSample_.height());
    const int px=qBound(0,int((x+(wallpaperSample_.width()*scale-width)/2)/scale),wallpaperSample_.width()-1);
    const int py=qBound(0,int((y+(wallpaperSample_.height()*scale-height)/2)/scale),wallpaperSample_.height()-1);
    double r=0,g=0,b=0;int count=0;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){
        const auto c=wallpaperSample_.pixelColor(qBound(0,px+dx,wallpaperSample_.width()-1),qBound(0,py+dy,wallpaperSample_.height()-1));
        r+=c.redF()*c.alphaF()+fallback.redF()*(1-c.alphaF());
        g+=c.greenF()*c.alphaF()+fallback.greenF()*(1-c.alphaF());
        b+=c.blueF()*c.alphaF()+fallback.blueF()*(1-c.alphaF());++count;
    }
    return QColor::fromRgbF(r/count,g/count,b/count);
}
bool SystemTools::setFileWallpaper(const QString &relative) {
    const auto path=safePath(relative);
    if (path.isEmpty()) { log("壁纸文件路径无效");return false; }
    return setWallpaper(QUrl::fromLocalFile(path));
}
QString SystemTools::fileLocation(const QString &name) const {
    const auto type = name == "downloads" ? QStandardPaths::DownloadLocation : QStandardPaths::DocumentsLocation;
    const auto path = QStandardPaths::writableLocation(type);
    const auto relative = QDir(filesRoot()).relativeFilePath(path);
    return !relative.startsWith("../") && !QDir::isAbsolutePath(relative) && QFileInfo(path).isDir() ? relative : QString{};
}
bool SystemTools::createFile(const QString &relative, const QString &text) {
    const auto path = safePath(relative, true);
    const auto bytes = text.toUtf8();
    QFile file(path);
    if (path.isEmpty() || bytes.size() > 65536 || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        log("创建失败：路径无效或文件已存在：" + relative); return false;
    }
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    if (!ok) QFile::remove(path);
    log((ok ? "已创建文件：" : "创建文件失败：") + relative);
    return ok;
}
void SystemTools::rememberFile(const QString &relative) {
    const auto path = safePath(relative);
    if (path.isEmpty() || !QFileInfo(path).isFile()) return;
    auto recent = settings_.value("recentFiles").toStringList();
    recent.removeAll(relative); recent.prepend(relative);
    while (recent.size() > 50) recent.removeLast();
    settings_.setValue("recentFiles", recent);
}
QVariantList SystemTools::recentFiles() const {
    QVariantList result;
    for (const auto &relative : settings_.value("recentFiles").toStringList()) {
        const auto path = safePath(relative); const QFileInfo file(path);
        if (!path.isEmpty() && file.isFile())
            result.append(QVariantMap{{"name",file.fileName()},{"directory",false},{"bytes",file.size()},{"relative",relative}});
    }
    return result;
}
QString SystemTools::safePath(const QString &relative, bool creating) const {
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.split('/').contains("..")) return {};
    const auto root = QFileInfo(filesRoot()).canonicalFilePath();
    QFileInfo info(filesRoot() + '/' + relative);
    if (root.isEmpty() || info.isSymLink()) return {};
    const auto path = creating && !info.exists()
        ? QFileInfo(info.absolutePath()).canonicalFilePath() + '/' + info.fileName()
        : info.canonicalFilePath();
    return path.startsWith(root + '/') ? path : QString{};
}
bool SystemTools::write(const QString &relative, const QString &text) {
    const auto path = safePath(relative, true); const auto data = text.toUtf8();
    if (path.isEmpty() || data.size() > 65536) { log("保存失败：路径无效或文本超过 64 KiB"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) { log("保存失败：" + file.errorString()); return false; }
    log("已保存：" + relative); return true;
}
bool SystemTools::makeDirectory(const QString &relative) {
    const auto path = safePath(relative, true); const bool ok = !path.isEmpty() && QDir().mkdir(path);
    log((ok ? "已创建目录：" : "创建目录失败：") + relative); return ok;
}
bool SystemTools::rename(const QString &from, const QString &to) {
    const auto source = safePath(from), target = safePath(to, true);
    const bool ok = !source.isEmpty() && !target.isEmpty() && !QFileInfo::exists(target) && QDir().rename(source, target);
    log((ok ? "已重命名：" : "重命名失败：") + from); return ok;
}
bool SystemTools::remove(const QString &relative) {
    const auto path = safePath(relative);
    const bool ok = !path.isEmpty() && (QFileInfo(path).isDir() ? QDir().rmdir(path) : QFile::remove(path));
    log((ok ? "已删除：" : "删除失败（目录必须为空）：") + relative); return ok;
}
QVariantList SystemTools::files(const QString &relative) const {
    const auto path = QFileInfo(filesRoot() + '/' + relative).canonicalFilePath();
    if (path != QFileInfo(filesRoot()).canonicalFilePath() && !path.startsWith(QFileInfo(filesRoot()).canonicalFilePath() + '/')) return {};
    QVariantList out;
    for (const auto &f : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        out.append(QVariantMap{{"name",f.fileName()},{"directory",f.isDir()},{"bytes",f.size()}});
    return out;
}
QString SystemTools::read(const QString &relative) const {
    QFile f(filesRoot() + '/' + relative); const auto path = QFileInfo(f).canonicalFilePath();
    if (!path.startsWith(QFileInfo(filesRoot()).canonicalFilePath() + '/') || !f.open(QIODevice::ReadOnly)) return "无法读取";
    if (f.size() > 65536) return "文件超过 64 KiB，请使用外部编辑器";
    return QString::fromUtf8(f.readAll());
}
bool SystemTools::editable(const QString &relative) const {
    const auto path = safePath(relative); if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return false;
    const auto data = file.readAll();
    return !data.contains('\0') && QString::fromUtf8(data).toUtf8() == data;
}
bool SystemTools::openWithDefault(const QString &relative) {
    const auto path = safePath(relative);
    if (path.isEmpty() || !QFileInfo(path).isFile()) { log("无法打开：文件路径无效"); return false; }
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    log(opened ? "已调用系统默认应用：" + relative : "系统默认应用打开失败：" + relative);
    return opened;
}
QString SystemTools::network() const {
    QStringList out;
    for (auto i : QNetworkInterface::allInterfaces()) for (auto a : i.addressEntries())
        out.append(i.humanReadableName() + ": " + a.ip().toString());
    return out.join('\n');
}
void SystemTools::speak(const QString &s) { if (settings_.value("speech",true).toBool()) speech_.say(s); }
void SystemTools::setSpeechEnabled(bool e) { settings_.setValue("speech",e); emit changed(); }
void SystemTools::setInterfaceScale(double scale) { settings_.setValue("interface_scale",qBound(1.0,scale,1.5)); emit changed(); }
void SystemTools::refreshStatus() {
    auto readText=[](const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()).trimmed():QString{}; };
    bool online=false,wifi=false;
    for (const auto &nic:QNetworkInterface::allInterfaces()) {
        if (!(nic.flags()&QNetworkInterface::IsUp) || !(nic.flags()&QNetworkInterface::IsRunning) || (nic.flags()&QNetworkInterface::IsLoopBack)) continue;
        bool addressed=false;
        for(const auto &address:nic.addressEntries()) if(!address.ip().isNull() && !address.ip().isLoopback() && !address.ip().isLinkLocal()) addressed=true;
        online=online||addressed;
        if(nic.type()==QNetworkInterface::Ieee80211) wifi=wifi||addressed;
    }
    int battery=-1;QString batteryState;
    const QDir power("/sys/class/power_supply");
    for(const auto &name:power.entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
        const QString prefix=power.filePath(name)+"/";
        if(readText(prefix+"type")!="Battery" || readText(prefix+"scope")=="Device") continue;
        bool ok=false;const int value=readText(prefix+"capacity").toInt(&ok);
        if(ok && value>=0 && value<=100) {battery=value;batteryState=readText(prefix+"status");break;}
    }
    qint64 total=0,available=0;
    for(const auto &line:readText("/proc/meminfo").split('\n')) {
        const auto parts=line.simplified().split(' ');
        if(parts.size()<2) continue;
        if(parts[0]=="MemTotal:") total=parts[1].toLongLong();
        if(parts[0]=="MemAvailable:") available=parts[1].toLongLong();
    }
    QStorageInfo storage(filesRoot());
    const auto bluetoothState=bluetooth_.isValid()?bluetooth_.hostMode():QBluetoothLocalDevice::HostPoweredOff;
    deviceStatus_={{"networkOnline",online},{"wifiConnected",wifi},
        {"networkLabel",wifi?"Wi-Fi 已连接":online?"网络接口已连接":"网络未连接"},
        {"bluetoothAvailable",bluetooth_.isValid()},
        {"bluetoothOn",bluetooth_.isValid() && bluetoothState!=QBluetoothLocalDevice::HostPoweredOff},
        {"batteryPresent",battery>=0},{"batteryPercent",battery},{"batteryState",batteryState},
        {"memoryLabel",total>0?QString("%1 / %2 GiB").arg((total-available)/1048576.0,0,'f',1).arg(total/1048576.0,0,'f',1):QString("未检测到")},
        {"storageLabel",storage.isValid()?QString("可用 %1 GiB").arg(storage.bytesAvailable()/1073741824.0,0,'f',1):QString("未检测到")}};
    emit changed();
}
void SystemTools::setBluetoothEnabled(bool enabled) {
    if(!bluetooth_.isValid()) {log("未检测到蓝牙适配器");return;}
    if(enabled) bluetooth_.powerOn();else bluetooth_.setHostMode(QBluetoothLocalDevice::HostPoweredOff);
    log(enabled?"已请求开启蓝牙":"已请求关闭蓝牙");
    QTimer::singleShot(300,this,&SystemTools::refreshStatus);
}
void SystemTools::remember(const QString &url) {
    auto list = bookmarks(); if (!list.contains(url) && list.size() < 100) { list.append(url); settings_.setValue("bookmarks",list); }
    emit changed();
}
QStringList SystemTools::bookmarks() const { return settings_.value("bookmarks").toStringList(); }
void SystemTools::log(const QString &s) {
    logs_.prepend(QDateTime::currentDateTime().toString(Qt::ISODate) + " " + s.left(2048));
    while (logs_.size() > 200) logs_.removeLast();
    const auto name = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/terminal.log";
    QFile output(name);
    if (output.size() > 1024*1024) { QFile::remove(name + ".1"); output.rename(name + ".1"); output.setFileName(name); }
    if (output.open(QIODevice::WriteOnly | QIODevice::Append)) output.write((logs_.first() + '\n').toUtf8());
    emit notification(s);
    emit changed();
}
void SystemTools::hotspot(bool enabled) {
    if (hotspotBusy_) return;
    hotspotBusy_ = true; emit changed();
    auto activate=[this,enabled] {
        auto *p = new QProcess(this);
        connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,p](int code,auto) {
            hotspotBusy_ = false;
            const auto error=QString::fromUtf8(p->readAllStandardError()).trimmed();
            log(code ? "热点操作失败：" + (error.isEmpty()?QString("NetworkManager 未返回错误"):error) : "热点状态已更新");
            refreshStatus();p->deleteLater();
        });
        connect(p,&QProcess::errorOccurred,this,[this,p](auto) { hotspotBusy_ = false; log("热点进程失败：" + p->errorString()); p->deleteLater(); });
        QStringList args{"--wait","30","connection",enabled ? "up" : "down","pi-module-ap"};
        // Optional target deployment grants these two exact commands only.
        // Desktop installs keep using their normal NetworkManager policy.
        if (qEnvironmentVariableIntValue("ORANGE_PI_HOTSPOT_SUDO") == 1) {
            args.prepend("/usr/bin/nmcli"); args.prepend("-n");
            p->start("sudo", args);
        } else p->start("nmcli", args);
    };
    if (!enabled) { activate(); return; }
    auto *check = new QProcess(this);
    connect(check,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,check,activate](int code,auto) {
        if (code == 0) activate();
        else { hotspotBusy_ = false; log("热点尚未配置。请在终端运行 bash scripts/configure-hotspot.sh，输入 WPA2 密码后再开启热点"); }
        check->deleteLater();
    });
    connect(check,&QProcess::errorOccurred,this,[this,check](auto) { hotspotBusy_ = false; log("无法启动 nmcli：请安装 NetworkManager"); check->deleteLater(); });
    check->start("nmcli", {"-g", "connection.id", "connection", "show", "pi-module-ap"});
}
