#include <QtTest>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include "core/PageProvider.h"

class PluginScaleTests : public QObject {
    Q_OBJECT
private slots:
    void responsivePage_data() {
        QTest::addColumn<double>("scale");
        for (double scale : {0.65, 0.8125, 0.975})
            QTest::newRow(qPrintable(QString::number(scale))) << scale;
    }
    void responsivePage() {
        QFETCH(double, scale);
        QQmlEngine engine;
        SessionModel model;
        model.update({{"heartbeat", 12345}, {"source", "s3_pogo_device"}, {"linkit", "wired-compatible"}});
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(PLUGIN_QML_PATH)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"sessionModel", QVariant::fromValue<QObject *>(&model)}, {"hostUiScale", scale}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);
        for (double width : {850.0, 500.0}) {
            page->setWidth(width);
            QTest::qWait(30);
            page->setHeight(page->implicitHeight());
            QTest::qWait(30);
            QVERIFY(page->implicitHeight() > 0);
            const int columns = page->property("columns").toInt();
            QCOMPARE(columns, width >= 600 * scale ? 2 : 1);
            for (int n = 0; n < 4; ++n) {
                const auto name = QString("telemetryMetric%1").arg(n);
                QQuickItem *card = nullptr;
                // Repeater delegates have visual rather than QObject parents.
                std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
                    if (item->objectName() == name) card = item;
                    for (auto *child : item->childItems()) visit(child);
                };
                visit(page);
                QVERIFY(card);
                const auto topLeft = card->mapToItem(page, QPointF(0, 0));
                QVERIFY(topLeft.x() >= -0.5);
                QVERIFY(topLeft.x() + card->width() <= page->width() + 0.5);
                QVERIFY(topLeft.y() + card->height() <= page->implicitHeight() + 0.5);
                QVERIFY(card->height() > 0);
            }
            QVERIFY(page->setProperty("diagnostics", true));
            QTest::qWait(20);
            QVERIFY(page->implicitHeight() > 0);
            QVERIFY(page->setProperty("diagnostics", false));
            model.update({{"heartbeat", 12346}, {"source", "s3_pogo_device"}});
            QTest::qWait(20);
        }
    }
};
QTEST_MAIN(PluginScaleTests)
#include "plugin_scale_test.moc"
