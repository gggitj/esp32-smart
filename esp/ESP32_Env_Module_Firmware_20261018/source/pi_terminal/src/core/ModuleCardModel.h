#pragma once
#include <QAbstractListModel>
#include <QVariantMap>

// Rows contain values only; no plugin-owned QObject survives unloading.
class ModuleCardModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ModuleCardModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : rows_.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "cardData"}}; }
    void synchronize(const QVariantList &rows);
private:
    QList<QVariantMap> rows_;
};
