#pragma once
#include <QQuickFramebufferObject>
#include <QPointer>
class BlurItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(float strength READ strength WRITE setStrength NOTIFY strengthChanged)
    Q_PROPERTY(float mixAmount READ mixAmount WRITE setMixAmount NOTIFY strengthChanged)
    Q_PROPERTY(int levels READ levels WRITE setLevels NOTIFY strengthChanged)
    Q_PROPERTY(float cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY strengthChanged)
public:
    Renderer *createRenderer() const override;
    QQuickItem *source() const { return source_; }
    void setSource(QQuickItem *item) {
        if (source_ == item) return;
        if (source_) disconnect(source_, nullptr, this, nullptr);
        source_ = item;
        if (item) connect(item, &QObject::destroyed, this, [this] { source_ = nullptr; emit sourceChanged(); update(); });
        emit sourceChanged(); update();
    }
    float strength() const { return strength_; }
    void setStrength(float value) { strength_ = qBound(0.1f, value, 4.0f); emit strengthChanged(); update(); }
    float mixAmount() const { return mix_; }
    void setMixAmount(float value) { mix_ = qBound(0.0f,value,1.0f); emit strengthChanged(); update(); }
    int levels() const { return levels_; }
    float cornerRadius() const { return radius_; }
    void setCornerRadius(float value) { radius_=qMax(0.0f,value);emit strengthChanged();update(); }
    void setLevels(int value) { levels_ = qBound(1,value,4); emit strengthChanged(); update(); }
signals:
    void sourceChanged();
    void strengthChanged();
private:
    QPointer<QQuickItem> source_;
    float strength_ = 1.0f;
    float mix_ = 1.0f;
    int levels_ = 4;
    float radius_ = 0;
};
