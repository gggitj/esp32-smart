#include "PageProvider.h"
void PageProvider::registerPage(const QString &instance, const QString &url) {
    urls_[instance] = url;
    if (!models_.contains(instance)) models_[instance] = new SessionModel(this);
}
void PageProvider::update(const QString &instance, const QVariantMap &data) {
    if (auto *m = models_.value(instance)) m->update(data);
}
void PageProvider::remove(const QString &instance) {
    urls_.remove(instance);
    if (auto *m = models_.take(instance)) m->deleteLater();
}
