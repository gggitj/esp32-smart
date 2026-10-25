#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QProcess>

// Stop-and-wait file transfer over the authenticated BLE or LAN data session.
class PackageReceiver : public QObject {
    Q_OBJECT
public:
    explicit PackageReceiver(QObject *parent = nullptr);
    void start(const QString &python, const QString &tool, const QString &key, int module);
    bool receive(const QJsonObject &message);
    void cancel();
    QString directory() const { return directory_.path(); }
signals:
    void request(QJsonObject body);
    void progress(int percent);
    void verifying();
    void finished(QString directory);
    void failed(QString message);
private:
    QTemporaryDir directory_;
    QFile file_;
    QTimer timer_;
    QProcess verifier_;
    QString fileName_, transfer_, python_, tool_, key_;
    int module_ = 0, retries_ = 0;
    qint64 expectedSize_ = -1, offset_ = 0;
    bool active_ = false;
    void openFile(const QString &name, qint64 size = -1);
    void ask();
    void fail(const QString &message);
};
