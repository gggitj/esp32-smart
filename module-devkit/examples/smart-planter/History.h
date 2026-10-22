#pragma once
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QMap>
#include <cmath>
#include <iterator>

// No QObject, retained host callbacks or background work crosses unload.
// One-minute real observations; seven-day bounded retention. Never fill gaps.
class History {
    struct Bucket { double sum = 0, low = 0, high = 0; int count = 0; };
    QMap<qint64, Bucket> minutes;
    QString path;
    qint64 persistedMinute = -1;
public:
    static constexpr qint64 minute = 60000;
    static constexpr qint64 week = 7LL * 24 * 60 * minute;
    QString error;
    explicit History(const QString &file) : path(file) {
        QFile f(path);
        if (!f.exists()) return;
        if (!f.open(QIODevice::ReadOnly) || f.size() > 3 * 1024 * 1024) {
            error = QStringLiteral("历史文件不可读或超过上限"); return;
        }
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isArray()) {
            error = QStringLiteral("历史文件格式错误"); return;
        }
        for (auto v : doc.array()) {
            auto o = v.toObject(); const qint64 t = qint64(o["t"].toDouble());
            Bucket b{o["sum"].toDouble(), o["low"].toDouble(), o["high"].toDouble(), o["count"].toInt()};
            if (t > 0 && t % minute == 0 && b.count > 0 && b.count <= 100000 &&
                std::isfinite(b.sum) && b.low >= -50 && b.high <= 100 && b.low <= b.high)
                minutes[t] = b;
        }
        while (minutes.size() > 10081) minutes.erase(minutes.begin());
    }
    void prune(qint64 now) {
        while (!minutes.isEmpty() && minutes.firstKey() < now - week) minutes.erase(minutes.begin());
        // Clock rollback must not render future observations as current data.
        while (!minutes.isEmpty() && minutes.lastKey() > now + minute) minutes.erase(std::prev(minutes.end()));
    }
    void add(qint64 now, double temperature) {
        prune(now);
        if (!std::isfinite(temperature) || temperature < -50 || temperature > 100) return;
        qint64 t = now / minute * minute;
        auto &b = minutes[t];
        if (!b.count) b.low = b.high = temperature;
        b.low = qMin(b.low, temperature); b.high = qMax(b.high, temperature);
        b.sum += temperature; ++b.count;
        if (persistedMinute != t) { save(); persistedMinute = t; }
    }
    bool save() {
        if (path.isEmpty()) { error = QStringLiteral("未提供历史存储目录"); return false; }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonArray a;
        for (auto it = minutes.cbegin(); it != minutes.cend(); ++it)
            a.append(QJsonObject{{"t", double(it.key())}, {"sum", it->sum}, {"low", it->low},
                                 {"high", it->high}, {"count", it->count}});
        QSaveFile f(path); const auto data = QJsonDocument(a).toJson(QJsonDocument::Compact);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
            error = QStringLiteral("历史保存失败：") + f.errorString(); return false;
        }
        error.clear(); return true;
    }
    QJsonArray series(qint64 now, qint64 span, qint64 step) const {
        QMap<qint64, Bucket> groups;
        for (auto it = minutes.cbegin(); it != minutes.cend(); ++it) {
            if (it.key() < now - span || it.key() > now) continue;
            auto &g = groups[it.key() / step * step];
            if (!g.count) { g.low = it->low; g.high = it->high; }
            g.low = qMin(g.low, it->low); g.high = qMax(g.high, it->high);
            g.sum += it->sum; g.count += it->count;
        }
        QJsonArray a;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
            a.append(QJsonObject{{"t", double(it.key())}, {"v", it->sum / it->count},
                                 {"min", it->low}, {"max", it->high}});
        return a;
    }
};
