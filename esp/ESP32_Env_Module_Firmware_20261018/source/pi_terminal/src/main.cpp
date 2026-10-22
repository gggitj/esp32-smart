#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QCommandLineParser>
#include <QTimer>
#include <QQuickStyle>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QSysInfo>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QIcon>
#include <algorithm>
#include <memory>
#include "core/ConnectionManager.h"
#include "ui/BlurItem.h"
#include "ui/SystemTools.h"
#include "ui/DesktopShell.h"
#ifdef TERMINAL_GUI_AUDIT
#include "GuiAudit.h"
#endif

int main(int argc, char **argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickStyle::setStyle("Material");
    QtWebEngineQuick::initialize();
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/orange_pi_terminal.svg")));
    app.setOrganizationName("ModularTerminal"); app.setApplicationName("OrangePiTerminal");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOptions({{{"c","config"},"Product configuration JSON","path"},
                       {"demo","Attach simulator module 80"}, {"quit-after","Quit after milliseconds","ms"},
                       {"screenshot","Save a window screenshot before quitting","path"},
                       {"metrics","Write frame-delivery and process-memory metrics on exit","path"},
                        {"window-size","Initial logical window size, e.g. 800x480","size"},
                        {"open-module","Open this ready module session in the foreground","instance"},
                        {"invoke-action","Invoke an available action after opening the module","action"},
                       {"ui-audit","Run responsive layout and injected touch tests","directory"},
                       {"ui-test","Exercise dynamic page/actions/disconnect automatically"}});
    parser.process(app);
    ConnectionManager connection;
    SystemTools tools;
    DesktopShell desktopShell;
    QObject::connect(&connection, &ConnectionManager::announcement, &tools, &SystemTools::speak);
    QObject::connect(&connection, &ConnectionManager::announcement, &tools, &SystemTools::log);
    QObject::connect(&connection, &ConnectionManager::bleOnlyPackageReady, &tools,
                     [&tools](const QString &instance) {
        qInfo().noquote() << "BLE-only package loaded; disabling module hotspot:" << instance;
        tools.hotspot(false);
    });
    if (parser.isSet("config") && !connection.configure(parser.value("config"))) { qCritical().noquote() << connection.status(); return 2; }
    const QString device = qEnvironmentVariable("ORANGE_PI_CONTROL_DEVICE");
    if (!device.isEmpty()) connection.start(device);
    qmlRegisterType<BlurItem>("Terminal.Native",1,0,"BlurItem");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("connectionManager", &connection);
    engine.rootContext()->setContextProperty("systemTools", &tools);
    engine.rootContext()->setContextProperty("desktopShell", &desktopShell);
    engine.rootContext()->setContextProperty("desktopDebug", parser.isSet("demo"));
    engine.load(QUrl("qrc:/qt/qml/OrangePiTerminal/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    desktopShell.setWindow(window);
    if (window && parser.isSet("window-size")) {
        const auto parts = parser.value("window-size").split('x');
        if (parts.size()!=2 || parts[0].toInt()<320 || parts[1].toInt()<320) { qCritical("Invalid window size (minimum 320x320 logical pixels)"); return 2; }
        window->resize(parts[0].toInt(),parts[1].toInt());
    }
    if (window && !parser.isSet("window-size") && !parser.isSet("ui-audit") && !parser.isSet("ui-test"))
        window->showFullScreen();
    struct Metrics { QElapsedTimer clock; QList<double> gaps; qint64 last = 0; };
    auto metrics = std::make_shared<Metrics>(); metrics->clock.start();
    if (window) {
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [metrics] {
            const auto ns = metrics->clock.nsecsElapsed();
            if (metrics->last && metrics->gaps.size() < 100000) metrics->gaps.append((ns-metrics->last)/1e6);
            metrics->last = ns;
        });
        QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, [] {
            if (auto *context = QOpenGLContext::currentContext()) {
                auto *gl = context->functions();
                qInfo("GL_RENDERER: %s; GL_VERSION: %s", gl->glGetString(GL_RENDERER), gl->glGetString(GL_VERSION));
            }
        }, Qt::DirectConnection);
    }
    bool uiTestPassed = false;
    if (parser.isSet("ui-audit")) {
#ifdef TERMINAL_GUI_AUDIT
        QTimer::singleShot(700,&app,[&] { app.exit(runGuiAudit(window,parser.value("ui-audit"))); });
#else
        qCritical("Rebuild with BUILD_TESTING=ON for --ui-audit"); return 2;
#endif
    }
    if (parser.isSet("demo") && device.isEmpty()) QTimer::singleShot(200, &connection, [&connection] {
        connection.attach({{"event","attach"},{"module_id",80},{"module_instance","desktop-demo"}});
    });
    if (parser.isSet("open-module")) {
        const QString instance = parser.value("open-module");
        auto *openTimer = new QTimer(&app);
        openTimer->setInterval(250);
        QObject::connect(openTimer, &QTimer::timeout, &app, [&, openTimer, instance] {
            for (const auto &row : connection.cards()) {
                const auto card = row.toMap();
                if (card.value("instance").toString() != instance || !card.value("ready").toBool()) continue;
                if (QMetaObject::invokeMethod(engine.rootObjects().first(), "openModule", Q_ARG(QVariant, QVariant(instance)))) {
                    qInfo().noquote() << "Foreground module opened:" << instance;
                    openTimer->stop();
                    if (parser.isSet("invoke-action")) {
                        const QString action = parser.value("invoke-action");
                        QTimer::singleShot(2500, &app, [&connection, instance, action] {
                            qInfo().noquote() << "Invoking module action:" << instance << action;
                            connection.action(instance, action);
                        });
                    }
                }
                return;
            }
        });
        openTimer->start();
    }
    if (parser.isSet("ui-test")) {
        auto *timer = new QTimer(&app); timer->setInterval(100);
        auto stage = std::make_shared<int>(0);
        auto lastReportedStage = std::make_shared<int>(-1);
        QObject::connect(timer, &QTimer::timeout, &app, [&, timer, stage, lastReportedStage] {
            const auto cards = connection.cards();
            if (*lastReportedStage != *stage) {
                *lastReportedStage = *stage;
                const auto card = cards.isEmpty() ? QVariantMap{} : cards.first().toMap();
                qInfo().noquote() << "UI_TEST_STAGE" << *stage
                    << "state=" << card.value("state").toString()
                    << "ready=" << card.value("ready").toBool()
                    << "message=" << card.value("message").toString()
                    << "status=" << connection.status()
                    << "phase=" << window->property("phase").toString()
                    << "permissionPending=" << window->property("permissionPending").toBool();
            }
            if (*stage == 0 && !cards.isEmpty() && cards.first().toMap().value("state").toString() == "awaiting_trust")
                QMetaObject::invokeMethod(engine.rootObjects().first(), "approveTrustOnce");
            if (*stage == 0 && !cards.isEmpty() && cards.first().toMap().value("ready").toBool() && !window->property("permissionPending").toBool()) {
                *stage=1;
#ifdef TERMINAL_GUI_AUDIT
                if(!touchLauncherEntry(window,"desktop-demo")){qCritical("Home module icon missing");app.exit(3);}
#else
                QMetaObject::invokeMethod(engine.rootObjects().first(), "openModule", Q_ARG(QVariant, QVariant("desktop-demo")));
#endif
            } else if(*stage==1 && window->property("phase").toString()=="open") {
                if(window->property("section").toInt()!=5){qCritical("Module feature opened inside connection management");app.exit(3);return;}
                connection.action("desktop-demo","start");*stage=2;
            } else if (*stage == 2 && !cards.isEmpty() && cards.first().toMap().value("message").toString().contains("42")) {
                *stage = 3;
                QTimer::singleShot(350, &app, [&] {
                    if (parser.isSet("screenshot") && window) window->grabWindow().save(parser.value("screenshot"));
                    QMetaObject::invokeMethod(window,"closeApp");
                });
            } else if(*stage==3 && window->property("phase").toString()=="home") {
                if(cards.isEmpty() || !cards.first().toMap().value("ready").toBool()){qCritical("Closing the app unexpectedly disconnected the module");app.exit(3);return;}
                *stage=30;
                // Wait for the launcher's asynchronous SVG image providers to
                // settle after the feature-page close animation before taking
                // the home screenshot or reopening the module.
                 QTimer::singleShot(800, &app, [&, stage] {
                     if (*stage != 30) return;
                     if(parser.isSet("screenshot")){const QFileInfo image(parser.value("screenshot"));window->grabWindow().save(image.dir().filePath(image.completeBaseName()+"-home.png"));}
                     *stage=4;
#ifdef TERMINAL_GUI_AUDIT
                     if(!touchLauncherEntry(window,"desktop-demo")){qCritical("Home module icon missing after app close");app.exit(3);}
#else
                     QMetaObject::invokeMethod(window,"openModule",Q_ARG(QVariant,QVariant("desktop-demo")));
#endif
                 });
            } else if(*stage==4 && window->property("phase").toString()=="open") {
                connection.disconnectModule("desktop-demo");*stage=5;
            } else if (*stage == 5 && cards.isEmpty()) {
                if(window->property("phase").toString()!="home" || !window->property("activeSession").toString().isEmpty()){qCritical("Plugin unloaded before close animation completed");app.exit(3);return;}
                QMetaObject::invokeMethod(engine.rootObjects().first(), "beginBrowserTest"); *stage = 6;
            } else if (*stage == 6 && engine.rootObjects().first()->property("browserTestPassed").toBool()) {
                qInfo("UI_TEST_PASS: home module icon tapped, feature opened directly, zoom close/reopen preserved session, animated disconnect released page before unload, browser verified");
                timer->stop(); *stage = 7; uiTestPassed = true;
            }
        }); timer->start();
    }
    if (parser.isSet("quit-after")) QTimer::singleShot(parser.value("quit-after").toInt(), &app, [&] {
        if (parser.isSet("screenshot") && !parser.isSet("ui-test")) {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (window) window->grabWindow().save(parser.value("screenshot"));
        }
        app.quit();
    });
    const int result = app.exec(); // engine (and QML pages) dies before connection/plugins
    if (parser.isSet("metrics")) {
        auto gaps = metrics->gaps; std::sort(gaps.begin(), gaps.end());
        QFile status("/proc/self/status"); QString rss;
        if (status.open(QIODevice::ReadOnly)) for (const auto &line : status.readAll().split('\n')) if (line.startsWith("VmRSS:")) rss = QString::fromLatin1(line).simplified();
        const double seconds = metrics->clock.elapsed()/1000.0;
        QJsonObject report{{"qt_version", qVersion()}, {"architecture", QSysInfo::currentCpuArchitecture()},
            {"elapsed_seconds",seconds}, {"frame_intervals",gaps.size()},
            {"average_frame_events_per_second", seconds > 0 ? (gaps.size()+1)/seconds : 0},
            {"p95_frame_interval_ms",gaps.isEmpty() ? 0 : gaps[int((gaps.size()-1)*0.95)]}, {"memory",rss}};
        QFile output(parser.value("metrics"));
        if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(report).toJson()) < 0) { qCritical("Cannot write metrics"); return 4; }
    }
    return parser.isSet("ui-test") && !uiTestPassed ? 3 : result;
}
