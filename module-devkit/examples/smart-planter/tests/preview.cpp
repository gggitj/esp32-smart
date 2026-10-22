#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlEngine>
#include <QTimer>
#include <QDateTime>
#include <QImage>
#include <cstdio>
#include <cmath>

// Explicit simulated fixture for repeatable layout checks; never installed
// as module history or published as physical sensor evidence.
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc < 3) return 2;
    QQuickView view;
    bool warnings = false;
    QObject::connect(view.engine(), &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        warnings = true; for (const auto &e : errors) fprintf(stderr, "%s\n", qPrintable(e.toString()));
    });
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (view.status() != QQuickView::Ready) return 3;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVariantList day, week;
    for (int i = 0; i < 96; ++i) {
        const double v = 22 + 3 * std::sin(i / 10.0);
        day.append(QVariantMap{{"t", double(now - (95-i)*900000LL)}, {"v", v}, {"min", v-.7}, {"max", v+.7}});
    }
    for (int i = 0; i < 84; ++i) {
        const double v = 23 + 2 * std::sin(i / 7.0);
        week.append(QVariantMap{{"t", double(now - (83-i)*7200000LL)}, {"v", v}, {"min", v-1.2}, {"max", v+1.2}});
    }
    QVariantMap data{{"temperature_C", 24.6}, {"humidity_RH", 61.2}, {"light_lux", 720},
        {"pressure_hPa", 1015.1}, {"battery_pct", 54}, {"battery_V", 3.85},
        {"power_source", "unknown"}, {"power_protection", "source_unknown_auto_sleep_inhibited"},
        {"received_at", double(now)}, {"history_day", day}, {"history_week", week}};
    view.rootObject()->setProperty("sessionModel", QVariantMap{{"data", data}});
    view.resize(840, 1120); view.show();
    QTimer::singleShot(1000, [&] {
        if (!view.grabWindow().save(QString::fromLocal8Bit(argv[2]) + "-day.png")) warnings = true;
        view.rootObject()->setProperty("weekView", true);
    });
    QTimer::singleShot(1600, [&] {
        if (!view.grabWindow().save(QString::fromLocal8Bit(argv[2]) + "-week.png")) warnings = true;
        view.resize(480, 1400);
    });
    QTimer::singleShot(2300, [&] {
        if (!view.grabWindow().save(QString::fromLocal8Bit(argv[2]) + "-narrow.png")) warnings = true;
        app.exit(warnings ? 1 : 0);
    });
    return app.exec();
}
