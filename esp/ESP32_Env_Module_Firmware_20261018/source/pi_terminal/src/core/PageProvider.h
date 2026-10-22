#pragma once
#include <QObject>
#include <QVariantMap>
#include <QHash>

// Host-owned session models stay valid until the QML Loader is released.
class SessionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
public:
    explicit SessionModel(QObject *parent = nullptr) : QObject(parent) {}
    QVariantMap data() const { return data_; }
    void update(const QVariantMap &data) { data_ = data; emit changed(); }
signals:
    void changed();
private:
    QVariantMap data_;
};
class PageProvider : public QObject {
    Q_OBJECT
public:
    explicit PageProvider(QObject *parent = nullptr) : QObject(parent) {}
    void registerPage(const QString &instance, const QString &url);
    void update(const QString &instance, const QVariantMap &data);
    void remove(const QString &instance);
    Q_INVOKABLE QString page(const QString &instance) const { return urls_.value(instance); }
    Q_INVOKABLE QObject *model(const QString &instance) const { return models_.value(instance); }
private:
    QHash<QString, QString> urls_;
    QHash<QString, SessionModel *> models_;
};
