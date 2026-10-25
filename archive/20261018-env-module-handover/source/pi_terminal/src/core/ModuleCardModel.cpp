#include "ModuleCardModel.h"
QVariant ModuleCardModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size() || role != Qt::UserRole + 1) return {};
    return rows_[index.row()];
}
void ModuleCardModel::synchronize(const QVariantList &rows) {
    QSet<QString> present;
    for (const auto &row : rows) present.insert(row.toMap().value("instance").toString());
    for (int i = rows_.size()-1; i >= 0; --i) {
        if (!present.contains(rows_[i].value("instance").toString())) {
            beginRemoveRows({}, i, i); rows_.removeAt(i); endRemoveRows();
        }
    }
    for (const auto &value : rows) {
        const auto row = value.toMap(); int i = 0;
        while (i < rows_.size() && rows_[i].value("instance") != row.value("instance")) ++i;
        if (i == rows_.size()) {
            beginInsertRows({}, i, i); rows_.append(row); endInsertRows();
        } else if (rows_[i] != row) {
            rows_[i] = row; emit dataChanged(index(i), index(i), {Qt::UserRole + 1});
        }
    }
}
