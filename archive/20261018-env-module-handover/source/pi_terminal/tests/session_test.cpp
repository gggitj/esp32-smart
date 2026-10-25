#include <QtTest>
#include "core/ConnectionManager.h"
#include "core/ModuleManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDir>

class Tests : public QObject {
    Q_OBJECT
private slots:
    void sessions() {
        ConnectionManager manager;
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        const QJsonObject event{{"event","attach"},{"module_id",80},{"module_instance","session-1"}};
        manager.attach(event);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(), 15000);
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance","old-session"}});
        QVERIFY(manager.cards().first().toMap().value("contact").toBool());
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance","session-1"}});
        QVERIFY(!manager.cards().first().toMap().value("contact").toBool());
        QVERIFY(manager.cards().first().toMap().value("ready").toBool());
        manager.action("session-1","start");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().first().toMap().value("message").toString().contains("42"),5000);
        manager.action("session-1","stale_data"); QTest::qWait(100);
        QVERIFY(!manager.cards().first().toMap().value("message").toString().contains("STALE"));
        manager.action("session-1","drop_link");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().first().toMap().value("ready").toBool(),2000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().first().toMap().value("ready").toBool(),12000);
        manager.disconnectModule("session-1");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
        // Cache reuse and a new instance must not receive old session state.
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","session-2"}});
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(),15000);
        manager.action("session-2","request_disconnect"); QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
    }
    void lifecycle() {
        ModuleManager manager;
        const auto folder = qEnvironmentVariable("TEST_PLUGIN");
        for (int n=0;n<100;++n) {
            QVERIFY(manager.load("instance",80,folder,{}));
            manager.connected("instance",2); manager.data("instance",2,"{}");
            manager.unload("instance"); QVERIFY(manager.page("instance").isEmpty());
        }
            QVERIFY(!manager.load("bad-id",79,folder,{}));
    }
    void reconnectAfterDetachedDisconnect() {
        ConnectionManager manager;
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        const QString id = "reinsert-check";
        const QJsonObject attached{{"event","attach"},{"module_id",80},{"module_instance",id}};
        const QJsonObject detached{{"event","detach"},{"module_id",80},{"module_instance",id}};
        auto ready = [&manager] { return !manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(); };
        manager.attach(attached);
        QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
        // Still inserted: repeated bridge announcements must stay suppressed.
        manager.action(id,"request_disconnect");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
        manager.attach(attached);
        QVERIFY(manager.cards().isEmpty());
        manager.attach(detached);
        manager.attach(attached);
        QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
        // User's sequence: remove contact, continue wireless, BOOT exit,
        // then reinsert. No second detach exists to clear suppression.
        manager.attach(detached);
        QVERIFY(ready());
        QVERIFY(!manager.cards().first().toMap().value("contact").toBool());
        manager.action(id,"request_disconnect");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
        manager.attach(attached);
        QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
        manager.disconnectModule(id);
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
    }
    void concurrentLibraryInstances() {
        ModuleManager manager;
        QSignalSpy errors(&manager, &ModuleManager::error);
        const auto folder = qEnvironmentVariable("TEST_PLUGIN");
        for (int cycle=0; cycle<5; ++cycle) {
            for (int n=0; n<3; ++n) QVERIFY(manager.load(QString::number(n),80,folder,{}));
            manager.unload("1");
            manager.connected("0",2);manager.data("0",2,"{}");
            manager.connected("2",2);manager.data("2",2,"{}");
            manager.unload("0");manager.unload("2");
        }
        QCOMPARE(errors.count(),0);
    }
    void trustGate() {
        ConnectionManager manager;
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        QSignalSpy trust(&manager, &ConnectionManager::trustRequired);
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","trust-check"}});
        QTRY_COMPARE_WITH_TIMEOUT(trust.count(), 1, 15000);
        QTRY_COMPARE(manager.cards().first().toMap().value("state").toString(), QString("awaiting_trust"));
        QVERIFY(!manager.cards().first().toMap().value("ready").toBool());
        QVERIFY(manager.cards().first().toMap().value("page").toString().isEmpty());
        manager.disconnectModule("trust-check");
        QTRY_VERIFY(manager.cards().isEmpty());
    }
    void deniedDisconnect() {
        QFile source(qEnvironmentVariable("TEST_CONFIG")); QVERIFY(source.open(QIODevice::ReadOnly));
        auto config = QJsonDocument::fromJson(source.readAll()).object();
        auto profiles = config.value("modules").toObject(); auto profile = profiles.value("80").toObject();
        profile["allow_disconnect"] = false; profiles["80"] = profile; config["modules"] = profiles;
        QTemporaryDir dir; QFile file(dir.filePath("product.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(config).toJson()); file.close();
        ConnectionManager manager; QVERIFY(manager.configure(file.fileName()));
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","deny-check"}});
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(), 15000);
        manager.action("deny-check", "request_disconnect");
        QTRY_VERIFY(manager.cards().first().toMap().value("message").toString().contains("disconnect_denied"));
        QVERIFY(manager.cards().first().toMap().value("ready").toBool());
        manager.action("deny-check", "start");
        QTRY_VERIFY(manager.cards().first().toMap().value("message").toString().contains("42"));
        manager.disconnectModule("deny-check"); QTRY_VERIFY(manager.cards().isEmpty());
    }
    void failedInstallerCanRetry() {
        QFile source(qEnvironmentVariable("TEST_CONFIG")); QVERIFY(source.open(QIODevice::ReadOnly));
        auto config = QJsonDocument::fromJson(source.readAll()).object(); config["python"] = "/nonexistent/opi-python";
        QTemporaryDir dir; QFile file(dir.filePath("product.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(config).toJson()); file.close();
        ConnectionManager manager; QVERIFY(manager.configure(file.fileName()));
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","retry-check"}});
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("state").toString() == "error", 15000);
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        manager.retry("retry-check");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().first().toMap().value("ready").toBool(), 15000);
        manager.disconnectModule("retry-check"); QTRY_VERIFY(manager.cards().isEmpty());
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "session_test.moc"
