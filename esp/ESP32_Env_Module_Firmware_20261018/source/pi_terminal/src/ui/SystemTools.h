#pragma once
#include <QObject>
#include <QVariantList>
#include <QTextToSpeech>
#include <QSettings>
#include <QBluetoothLocalDevice>
#include <QVariantMap>
#include <QUrl>
#include <QImage>
#include <QColor>
class SystemTools : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList logs READ logs NOTIFY changed)
    Q_PROPERTY(QString network READ network NOTIFY changed)
    Q_PROPERTY(QString filesRoot READ filesRoot CONSTANT)
    Q_PROPERTY(bool speechEnabled READ speechEnabled NOTIFY changed)
    Q_PROPERTY(double interfaceScale READ interfaceScale NOTIFY changed)
    Q_PROPERTY(QVariantMap deviceStatus READ deviceStatus NOTIFY changed)
    Q_PROPERTY(bool hotspotBusy READ hotspotBusy NOTIFY changed)
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY changed)
    Q_PROPERTY(double launcherOpacity READ launcherOpacity NOTIFY changed)
    Q_PROPERTY(double moduleOpacity READ moduleOpacity NOTIFY changed)
public:
    Q_INVOKABLE QString saveCameraFrame(const QString &frame, const QUrl &destination);
    Q_INVOKABLE QString cameraPhotoPath() const;
    double launcherOpacity() const { return qBound(0.0,settings_.value("launcherOpacity",0.65).toDouble(),1.0); }
    double moduleOpacity() const { return qBound(0.0,settings_.value("moduleOpacity",0.65).toDouble(),1.0); }
    Q_INVOKABLE void setLauncherOpacity(double value);
    Q_INVOKABLE void setModuleOpacity(double value);
    explicit SystemTools(QObject *parent = nullptr);
    QStringList logs() const { return logs_; }
    QString network() const;
    QString filesRoot() const;
    Q_INVOKABLE QVariantList files(const QString &relative) const;
    Q_INVOKABLE QString fileLocation(const QString &name) const;
    Q_INVOKABLE bool createFile(const QString &relative, const QString &text);
    Q_INVOKABLE QVariantList recentFiles() const;
    Q_INVOKABLE void rememberFile(const QString &relative);
    Q_INVOKABLE QString read(const QString &relative) const;
    Q_INVOKABLE bool editable(const QString &relative) const;
    Q_INVOKABLE bool openWithDefault(const QString &relative);
    Q_INVOKABLE bool write(const QString &relative, const QString &text);
    Q_INVOKABLE bool makeDirectory(const QString &relative);
    Q_INVOKABLE bool rename(const QString &from, const QString &to);
    Q_INVOKABLE bool remove(const QString &relative);
    bool speechEnabled() const { return settings_.value("speech", true).toBool(); }
    double interfaceScale() const { return qBound(1.0, settings_.value("interface_scale", 1.0).toDouble(), 1.5); }
    Q_INVOKABLE void setInterfaceScale(double scale);
    QVariantMap deviceStatus() const { return deviceStatus_; }
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void setBluetoothEnabled(bool enabled);
    Q_INVOKABLE void speak(const QString &text);
    Q_INVOKABLE void remember(const QString &url);
    Q_INVOKABLE QStringList bookmarks() const;
    Q_INVOKABLE void setSpeechEnabled(bool enabled);
    Q_INVOKABLE void hotspot(bool enabled);
    QUrl wallpaper() const;
    Q_INVOKABLE QColor wallpaperColor(double x, double y, double width, double height) const;
    Q_INVOKABLE QUrl monochromeIcon(const QString &name, bool white) const;
    Q_INVOKABLE bool setWallpaper(const QUrl &url);
    Q_INVOKABLE bool setFileWallpaper(const QString &relative);
    bool hotspotBusy() const { return hotspotBusy_; }
    void log(const QString &text);
signals:
    void changed();
    void notification(QString message);
private:
    QTextToSpeech speech_;
    QStringList logs_;
    QSettings settings_;
    QBluetoothLocalDevice bluetooth_;
    QVariantMap deviceStatus_;
    bool hotspotBusy_ = false;
    mutable QUrl sampledWallpaper_;
    mutable QImage wallpaperSample_;
    QString safePath(const QString &relative, bool creating = false) const;
};
