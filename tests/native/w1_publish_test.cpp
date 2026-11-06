// W1 publication contract tests: manifest detection, portable roots, and the
// read-only plugin store. These cover the pure decision layer that the end-to-end
// payload test cannot exercise directly.

#include <QtTest>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include "core/run_mode.h"
#include "app/plugin_store.h"

using namespace corig;

namespace {
QByteArray manifest(int formatVersion = 1, const QByteArray &appId = "org.example.lab")
{
    return QByteArray("{\"format\":\"corig-app\",\"format_version\":") + QByteArray::number(formatVersion)
        + ",\"mode\":\"publish\",\"app\":{\"id\":\"" + appId + "\",\"name\":\"Lab\",\"version\":\"1.0.0\"}"
        + ",\"release\":{\"snapshot_sha256\":\"abc\"},\"retain_original\":false,"
        + "\"runtime\":{\"executable\":\"runtime/CoRig.exe\"}}";
}
QString payloadExe(const QTemporaryDir &root)
{
    return QDir(root.path()).filePath("cache/payloads/p1/runtime/CoRig.exe");
}
}

class W1PublishTest : public QObject {
    Q_OBJECT
private slots:
    void validManifestDetected()
    {
        QTemporaryDir root; RunOptions options; RunModeError error;
        QVERIFY(detectRunMode(payloadExe(root), manifest(), &options, &error));
        QCOMPARE(options.mode, RunMode::Publish);
        QCOMPARE(options.appId, QStringLiteral("org.example.lab"));
        QCOMPARE(options.appVersion, QStringLiteral("1.0.0"));
        QCOMPARE(options.releaseHash, QStringLiteral("abc"));
        QVERIFY(options.retainOriginal == false);
    }
    void wrongFormatVersionRejected()
    {
        QTemporaryDir root; RunOptions options; RunModeError error;
        QVERIFY(!detectRunMode(payloadExe(root), manifest(2), &options, &error));
        QCOMPARE(error.code, QStringLiteral("PAYLOAD_INCOMPLETE"));
    }
    void badAppIdRejected()
    {
        QTemporaryDir root; RunOptions options; RunModeError error;
        QVERIFY(!detectRunMode(payloadExe(root), manifest(1, "Bad/Id"), &options, &error));
        QCOMPARE(error.code, QStringLiteral("PAYLOAD_INCOMPLETE"));
    }
    void exeOutsideRuntimeRejected()
    {
        QTemporaryDir root; RunOptions options; RunModeError error;
        const auto exe = QDir(root.path()).filePath("CoRig.exe");
        QVERIFY(!detectRunMode(exe, manifest(), &options, &error));
        QCOMPARE(error.code, QStringLiteral("PAYLOAD_INCOMPLETE"));
    }
    void publishPathsStayInsideRoot()
    {
        QTemporaryDir root; RunOptions options; RunModeError error;
        QVERIFY(detectRunMode(payloadExe(root), manifest(), &options, &error));
        QVERIFY(computePublishPaths(root.path(), &options, &error));
        const auto prefix = QDir::cleanPath(root.path()) + QLatin1Char('/');
        QVERIFY(options.dataRoot.startsWith(prefix));
        QVERIFY(options.cacheRoot.startsWith(prefix));
        QVERIFY(options.tempRoot.startsWith(prefix));
        QVERIFY(options.crashRoot.startsWith(prefix));
        QVERIFY(options.pluginCacheRoot.startsWith(prefix));
    }
    void emptyRootRejected()
    {
        RunOptions options; options.mode = RunMode::Publish; options.appId = QStringLiteral("org.example.lab");
        RunModeError error;
        QVERIFY(!computePublishPaths(QString(), &options, &error));
        QCOMPARE(error.code, QStringLiteral("PAYLOAD_INCOMPLETE"));
    }
    void pluginStoreReadOnlyRefusesWrites()
    {
        QTemporaryDir root; QSettings settings(QDir(root.path()).filePath("settings.ini"), QSettings::IniFormat);
        const auto directory = QDir(root.path()).filePath("plugins");
        PluginStore store(directory, settings, {}, true);
        QVERIFY(!QDir(directory).exists());
        QVERIFY_EXCEPTION_THROWN(store.remove(QStringLiteral("x"), {}), Error);
        QVERIFY_EXCEPTION_THROWN(store.setEnabled({QStringLiteral("x")}, true, {}), Error);
    }
};

QTEST_MAIN(W1PublishTest)
#include "w1_publish_test.moc"
