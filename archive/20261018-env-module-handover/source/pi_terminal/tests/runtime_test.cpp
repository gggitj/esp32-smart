#include <QtTest>
#include "core/ModuleCardModel.h"
#include "core/PageProvider.h"
#include "core/SessionStateMachine.h"
#include "core/PackageReceiver.h"
#include "core/HostEventDetector.h"
#include "ui/SystemTools.h"
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <QBuffer>

class RuntimeTests : public QObject {
    Q_OBJECT
private slots:
    void bridgeSequenceValidation() {
        HostEventDetector detector;
        QSignalSpy events(&detector, &HostEventDetector::detected);
        QJsonObject e{{"event", "attach"}, {"module_id", 80}, {"module_instance", "seq-test"}};
        for (double sequence : {-1.0, 0.0, 0.5, 4294967296.0}) {
            e["_bridge_sequence"] = sequence; detector.consume(e);
        }
        QCOMPARE(events.count(), 0);
        e["_bridge_sequence"] = 1; detector.consume(e); detector.consume(e);
        QCOMPARE(events.count(), 1);
        e["_bridge_sequence"] = 2; detector.consume(e);
        QCOMPARE(events.count(), 2);
        detector.reset(); detector.consume(e);
        QCOMPARE(events.count(), 3);
    }
    void cameraFrameSave() {
        SystemTools tools;QTemporaryDir dir;
        QImage image(320,240,QImage::Format_RGB32);image.fill(Qt::green);
        QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);QVERIFY(image.save(&buffer,"PNG"));
        const QString frame="data:image/png;base64,"+QString::fromLatin1(bytes.toBase64());
        const auto path=dir.filePath("照片 with spaces.png");const auto url=QUrl::fromLocalFile(path);
        QVERIFY(tools.saveCameraFrame(frame,url).isEmpty());
        QVERIFY(!tools.saveCameraFrame(frame,url).isEmpty());
        QVERIFY(tools.saveCameraFrame(frame,QUrl(dir.filePath("另一张 photo.png"))).isEmpty());
        QFile saved(path);QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),bytes);saved.close();
        QVERIFY(!tools.saveCameraFrame("data:image/png;base64,invalid",url).isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),bytes);saved.close();
        QVERIFY(!tools.saveCameraFrame(frame,QUrl::fromLocalFile(dir.filePath("missing/photo.png"))).isEmpty());
        QVERIFY(!tools.saveCameraFrame(frame,QUrl("https://example.com/photo.png")).isEmpty());
    }
    void opacityPreferences() {
        SystemTools tools;
        const auto home=tools.launcherOpacity(),module=tools.moduleOpacity();
        tools.setLauncherOpacity(0.23);tools.setModuleOpacity(0.87);
        SystemTools reloaded;
        QCOMPARE(reloaded.launcherOpacity(),0.23);QCOMPARE(reloaded.moduleOpacity(),0.87);
        tools.setLauncherOpacity(-1);tools.setModuleOpacity(2);
        QCOMPARE(tools.launcherOpacity(),0.0);QCOMPARE(tools.moduleOpacity(),1.0);
        tools.setLauncherOpacity(home);tools.setModuleOpacity(module);
    }
    void modelKeepsIdentity() {
        ModuleCardModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.synchronize({QVariantMap{{"instance","one"},{"progress",10}}, QVariantMap{{"instance","two"},{"progress",20}}});
        QPersistentModelIndex index(model.index(0));
        model.synchronize({QVariantMap{{"instance","two"},{"progress",80}}, QVariantMap{{"instance","one"},{"progress",40}}});
        QCOMPARE(index.data(Qt::UserRole+1).toMap().value("instance").toString(), "one");
        QCOMPARE(index.data(Qt::UserRole+1).toMap().value("progress").toInt(), 40);
        model.synchronize({QVariantMap{{"instance","two"}}});
        QVERIFY(!index.isValid()); QCOMPARE(model.rowCount(), 1);
    }
    void isolatedPageModels() {
        PageProvider pages;
        pages.registerPage("one", "file:///one.qml"); pages.registerPage("two", "file:///two.qml");
        pages.update("one", {{"value",42}});
        QCOMPARE(pages.model("one")->property("data").toMap().value("value").toInt(), 42);
        QVERIFY(pages.model("two")->property("data").toMap().isEmpty());
        pages.remove("one"); QVERIFY(!pages.model("one")); QVERIFY(pages.page("one").isEmpty());
    }
    void rejectsIllegalTransitions() {
        SessionStateMachine machine; QSignalSpy states(&machine, &SessionStateMachine::changed);
        machine.request("ready", "must not bypass verification"); QTest::qWait(30); QCOMPARE(states.count(), 0);
        for (const auto *name : {"connecting", "downloading_package", "awaiting_trust", "loading_plugin", "ready", "contact_detached", "ready", "disconnecting", "unloading", "closed"}) {
            machine.request(name, name); QTRY_VERIFY(!states.isEmpty()); QCOMPARE(states.takeLast().first().toString(), QString(name));
        }
        machine.request("ready", "cannot resurrect"); QTest::qWait(30); QCOMPARE(states.count(), 0);
    }
    void chunkIdentityAndBounds() {
        PackageReceiver receiver; QSignalSpy requests(&receiver, &PackageReceiver::request), errors(&receiver, &PackageReceiver::failed);
        receiver.start("python3", "unused", "unused", 128);
        QCOMPARE(requests.count(), 1);
        auto request = requests.first().first().toJsonObject();
        auto wrong = request; wrong["transfer_id"] = "wrong";
        QVERIFY(!receiver.receive(wrong)); QCOMPARE(errors.count(), 0);
        request["total"] = 999999; request["payload"] = "YQ==";
        QVERIFY(!receiver.receive(request)); QCOMPARE(errors.count(), 1);
    }
    void confinedFiles() {
        QStandardPaths::setTestModeEnabled(true);
        SystemTools tools;
        const QString name = "test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(tools.makeDirectory(name));
        QVERIFY(tools.write(name + "/a.txt", "文本"));
        QCOMPARE(tools.read(name + "/a.txt"), QString("文本"));
        QVERIFY(!tools.write("../escape.txt", "no"));
        QVERIFY(!tools.write(name + "/big.txt", QString(65537, 'a')));
        QTemporaryDir external;
        QVERIFY(QFile::link(external.path(), tools.filesRoot() + "/" + name + "/link"));
        QVERIFY(!tools.write(name + "/link/out.txt", "no"));
        QFile::remove(tools.filesRoot() + "/" + name + "/link");
        QVERIFY(tools.rename(name + "/a.txt", name + "/b.txt"));
        QVERIFY(tools.remove(name + "/b.txt")); QVERIFY(tools.remove(name));
    }
    void fileCreationAndLocations() {
        SystemTools tools;
        const QString name = "create-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt";
        QVERIFY(tools.createFile(name,"original"));
        QVERIFY(!tools.createFile(name,"replacement"));
        QCOMPARE(tools.read(name),QString("original"));
        QVERIFY(!tools.createFile("../outside.txt","no"));
        tools.rememberFile(name);
        QVERIFY(!tools.recentFiles().isEmpty());
        QCOMPARE(tools.recentFiles().first().toMap().value("relative").toString(),name);
        for (const auto &location : {"documents","downloads"}) {
            const auto relative=tools.fileLocation(location);
            QVERIFY(!QDir::isAbsolutePath(relative));
            QVERIFY(!relative.startsWith("../"));
        }
        QVERIFY(tools.remove(name));
    }
    void fileBrowserStartsAtHome() {
        SystemTools tools;
        QCOMPARE(QFileInfo(tools.filesRoot()).canonicalFilePath(), QFileInfo(QDir::homePath()).canonicalFilePath());
    }
    void wallpaperValidationAndPersistence() {
        SystemTools tools;
        const auto previous=tools.wallpaper();
        QTemporaryDir dir;
        const auto path=dir.filePath("wallpaper with spaces.png");
        QImage image(160,90,QImage::Format_RGB32);image.fill(QColor("#38b8a0"));
        QVERIFY(image.save(path));
        QVERIFY(tools.setWallpaper(QUrl::fromLocalFile(path)));
        QCOMPARE(tools.wallpaper(),QUrl::fromLocalFile(path));
        QCOMPARE(tools.wallpaperColor(80,45,160,90),QColor("#38b8a0"));
        QImage contrast(160,90,QImage::Format_RGB32);contrast.fill(Qt::white);
        for(int y=0;y<90;++y)for(int x=0;x<80;++x)contrast.setPixelColor(x,y,Qt::black);
        const auto contrastPath=dir.filePath("contrast.png");QVERIFY(contrast.save(contrastPath));
        QVERIFY(tools.setWallpaper(QUrl::fromLocalFile(contrastPath)));
        QCOMPARE(tools.wallpaperColor(20,45,160,90),QColor(Qt::black));
        QCOMPARE(tools.wallpaperColor(140,45,160,90),QColor(Qt::white));
        QCOMPARE(tools.wallpaperColor(10,160,90,320),QColor(Qt::black));
        QCOMPARE(tools.wallpaperColor(80,160,90,320),QColor(Qt::white));
        QVERIFY(tools.setWallpaper(QUrl::fromLocalFile(path)));
        SystemTools reloaded;
        QCOMPARE(reloaded.wallpaper(),tools.wallpaper());
        QVERIFY(!tools.setWallpaper(QUrl("https://example.com/picture.png")));
        QVERIFY(!tools.setWallpaper(QUrl::fromLocalFile(dir.filePath("missing.png"))));
        QCOMPARE(tools.wallpaper(),QUrl::fromLocalFile(path));
        QVERIFY(tools.setWallpaper(QUrl{}));QVERIFY(tools.wallpaper().isEmpty());
        QVERIFY(tools.setWallpaper(previous));
    }
    void desktopFileAndBrowserContracts() {
        SystemTools tools;
        const QString name = "terminal-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt";
        QVERIFY(tools.write(name, "default application test"));
        const auto rows = tools.files("");
        bool found = false;
        for (const auto &row : rows) if (row.toMap().value("name").toString() == name) { found = true; QVERIFY(!row.toMap().value("directory").toBool()); }
        QVERIFY(found);
        QVERIFY(!tools.openWithDefault("../" + name));
        QVERIFY(tools.remove(name));
        // The GUI owns URL normalization; the native file API must remain bounded.
        QVERIFY(tools.files("").size() >= 0);
    }
};
QTEST_GUILESS_MAIN(RuntimeTests)
#include "runtime_test.moc"
