// W2 user parameter overlay tests. The overlay must survive a restart, fall back to
// the shipped defaults when the file is corrupt, skip parameters the payload no
// longer ships, and restore the shipped snapshot without touching the project.

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "core/user_params.h"

using namespace corig;

namespace {

QVariantMap sampleProject()
{
    const QVariantMap gain{{"id", "gain"}, {"plugin", "math.gain"}, {"config", QVariantMap{{"gain", 1.0}, {"label", "G"}}}};
    const QVariantMap slider{{"id", "slider"}, {"plugin", "control.slider"}, {"config", QVariantMap{{"value", 0.5}}}};
    const QVariantMap root{{"id", "root"}, {"plugin", "layout.canvas"}, {"children", QVariantList{}}, {"properties", QVariantMap{}}};
    const QVariantMap control{{"id", "control-1"}, {"node", "slider"}};
    QVariantMap project;
    project["format"] = "corig-project";
    project["schema_version"] = 3;
    project["name"] = QStringLiteral("参数测试");
    project["nodes"] = QVariantList{gain, slider};
    project["ui"] = QVariantMap{{"root", "root"}, {"items", QVariantList{root, control}}};
    return project;
}

QVariantMap configOf(const QVariantMap &project, const QString &id)
{
    for (const auto &entry : project.value("nodes").toList())
        if (entry.toMap().value("id").toString() == id) return entry.toMap().value("config").toMap();
    return {};
}

QString paramsFile(const QTemporaryDir &root)
{
    return QDir(root.path()).filePath("params/user-params.json");
}

} // namespace

class UserParamsTest : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void releaseHashChangeKeepsValues();
    void corruptFileFallsBackAndIsRenamed();
    void missingNodeIsSkipped();
    void typeMismatchIsSkipped();
    void resetRestoresShippedValues();
    void resetRestoresBaselineCapturedAfterLoad();
    void removeNodeDropsOneOverride();
};

void UserParamsTest::roundTrip()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    {
        UserParams params(root.path());
        params.setReleaseHash(QStringLiteral("sha256:1"));
        params.setNode(QStringLiteral("gain"), QStringLiteral("gain"), 2.5);
        params.setControl(QStringLiteral("control-1"), 0.75);
        QVERIFY(params.save());
        QVERIFY(QFileInfo::exists(paramsFile(root)));
    }
    UserParams loaded(root.path());
    QVERIFY(loaded.load());
    QCOMPARE(loaded.releaseHash(), QStringLiteral("sha256:1"));
    QCOMPARE(loaded.nodeValue(QStringLiteral("gain"), QStringLiteral("gain")).toDouble(), 2.5);
    QCOMPARE(loaded.controlValue(QStringLiteral("control-1")).toDouble(), 0.75);
    auto project = sampleProject();
    const auto report = loaded.apply(project);
    QCOMPARE(report.value("applied").toInt(), 2);
    QCOMPARE(report.value("skipped").toInt(), 0);
    QCOMPARE(configOf(project, QStringLiteral("gain")).value(QStringLiteral("gain")).toDouble(), 2.5);
    QCOMPARE(configOf(project, QStringLiteral("slider")).value(QStringLiteral("value")).toDouble(), 0.75);
}

void UserParamsTest::releaseHashChangeKeepsValues()
{
    QTemporaryDir root;
    {
        UserParams params(root.path());
        params.setReleaseHash(QStringLiteral("sha256:old"));
        params.setNode(QStringLiteral("gain"), QStringLiteral("gain"), 3.5);
        QVERIFY(params.save());
    }
    UserParams reloaded(root.path());
    reloaded.setReleaseHash(QStringLiteral("sha256:new"));
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.releaseHash(), QStringLiteral("sha256:old"));
    QCOMPARE(reloaded.nodeValue(QStringLiteral("gain"), QStringLiteral("gain")).toDouble(), 3.5);
    auto project = sampleProject();
    QCOMPARE(reloaded.apply(project).value("applied").toInt(), 1);
    QCOMPARE(configOf(project, QStringLiteral("gain")).value(QStringLiteral("gain")).toDouble(), 3.5);
}

void UserParamsTest::corruptFileFallsBackAndIsRenamed()
{
    QTemporaryDir root;
    const auto directory = QDir(root.path());
    QVERIFY(directory.mkpath(QStringLiteral("params")));
    {
        QFile broken(paramsFile(root));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ not json");
    }
    UserParams params(root.path());
    QVERIFY(!params.load());
    QVERIFY(!QFileInfo::exists(paramsFile(root)));
    QVERIFY(QFileInfo::exists(paramsFile(root) + QStringLiteral(".bad")));
    QVERIFY(params.values().value("nodes").toMap().isEmpty());
    auto project = sampleProject();
    QCOMPARE(params.apply(project).value("applied").toInt(), 0);
}

void UserParamsTest::missingNodeIsSkipped()
{
    QTemporaryDir root;
    UserParams params(root.path());
    params.setNode(QStringLiteral("removed"), QStringLiteral("gain"), 3.0);
    params.setNode(QStringLiteral("gain"), QStringLiteral("gain"), 2.0);
    auto project = sampleProject();
    const auto report = params.apply(project);
    QCOMPARE(report.value("applied").toInt(), 1);
    QCOMPARE(report.value("skipped").toInt(), 1);
    QCOMPARE(configOf(project, QStringLiteral("gain")).value(QStringLiteral("gain")).toDouble(), 2.0);
}

void UserParamsTest::typeMismatchIsSkipped()
{
    QTemporaryDir root;
    UserParams params(root.path());
    params.setNode(QStringLiteral("gain"), QStringLiteral("label"), 5.0);
    params.setNode(QStringLiteral("gain"), QStringLiteral("unknown"), 1.0);
    auto project = sampleProject();
    const auto report = params.apply(project);
    QCOMPARE(report.value("applied").toInt(), 0);
    QCOMPARE(report.value("skipped").toInt(), 2);
    QCOMPARE(configOf(project, QStringLiteral("gain")).value(QStringLiteral("label")).toString(), QStringLiteral("G"));
}

void UserParamsTest::resetRestoresShippedValues()
{
    QTemporaryDir root;
    UserParams params(root.path());
    params.setNode(QStringLiteral("gain"), QStringLiteral("gain"), 4.0);
    auto project = sampleProject();
    QCOMPARE(params.apply(project).value("applied").toInt(), 1);
    QCOMPARE(configOf(project, QStringLiteral("gain")).value(QStringLiteral("gain")).toDouble(), 4.0);
    params.reset(project);
    QCOMPARE(configOf(project, QStringLiteral("gain")).value(QStringLiteral("gain")).toDouble(), 1.0);
    QVERIFY(params.values().value("nodes").toMap().isEmpty());
    QVERIFY(params.save());
    UserParams reloaded(root.path());
    QVERIFY(reloaded.load());
    QVERIFY(reloaded.values().value("nodes").toMap().isEmpty());
}

void UserParamsTest::resetRestoresBaselineCapturedAfterLoad()
{
    QTemporaryDir root;
    UserParams params(root.path());
    auto project = sampleProject();
    // Nothing is applied at startup, so no snapshot exists yet; a parameter first
    // changed during the session must still be restorable.
    QCOMPARE(params.apply(project).value("applied").toInt(), 0);
    params.captureBaseline(QStringLiteral("gain"), QStringLiteral("gain"), 1.0);
    params.setNode(QStringLiteral("gain"), QStringLiteral("gain"), 4.0);
    QCOMPARE(params.apply(project).value("applied").toInt(), 1);
    params.reset(project);
    QCOMPARE(configOf(project, QStringLiteral("gain")).value(QStringLiteral("gain")).toDouble(), 1.0);
    QVERIFY(params.values().value("nodes").toMap().isEmpty());
}

void UserParamsTest::removeNodeDropsOneOverride()
{
    QTemporaryDir root;
    UserParams params(root.path());
    params.setNode(QStringLiteral("gain"), QStringLiteral("gain"), 2.0);
    params.setNode(QStringLiteral("gain"), QStringLiteral("label"), QStringLiteral("X"));
    params.removeNode(QStringLiteral("gain"), QStringLiteral("gain"));
    QVERIFY(!params.nodeValue(QStringLiteral("gain"), QStringLiteral("gain")).isValid());
    QCOMPARE(params.nodeValue(QStringLiteral("gain"), QStringLiteral("label")).toString(), QStringLiteral("X"));
    params.removeNode(QStringLiteral("gain"), QStringLiteral("label"));
    QVERIFY(params.values().value("nodes").toMap().isEmpty());
}

QTEST_MAIN(UserParamsTest)
#include "user_params_test.moc"
