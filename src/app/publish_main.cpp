// Headless publisher: snapshots a saved project into a self-contained payload.
// It runs beside CoRig.exe so the runtime tree, the SDK DLL and the official
// plugin library are the ones this build actually ships.
#include "core/publish.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

namespace {

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject errorReport(const QVariantMap &error)
{
    return QJsonObject{{QStringLiteral("code"),error.value(QStringLiteral("code")).toString()},
                       {QStringLiteral("phase"),error.value(QStringLiteral("phase")).toString()},
                       {QStringLiteral("detail"),error.value(QStringLiteral("detail")).toString()}};
}

// Names without an ASCII slug (Chinese project names, for example) would all
// collapse to the same id. Fall back to a content-derived identifier so two
// different projects never share an application directory.
QString defaultAppId(const QString &name, const QString &projectPath)
{
    QString slug;
    for (const auto character : name) if (character.isLetterOrNumber()) slug.append(character.toLower());
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{2,63}$"));
    const auto candidate = QStringLiteral("org.corig.") + slug;
    if (!slug.isEmpty() && pattern.match(candidate).hasMatch()) return candidate;
    QByteArray contents;
    QFile project(projectPath);
    if (project.open(QIODevice::ReadOnly)) contents = project.readAll();
    const auto digest = QCryptographicHash::hash(contents,QCryptographicHash::Sha256).toHex().left(12);
    return QStringLiteral("corig.app.") + QString::fromLatin1(digest);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    QCoreApplication::setApplicationName(QStringLiteral("CoRig-publish"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.4.4"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("将工程打包为自包含运行载荷（阶段一：目录型载荷）。"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"),QStringLiteral("要执行的命令：publish。"),QStringLiteral("<command>"));
    const QCommandLineOption project({QStringLiteral("p"),QStringLiteral("project")},QStringLiteral("工程文件（.crpj）。"),QStringLiteral("path"));
    const QCommandLineOption output({QStringLiteral("o"),QStringLiteral("output")},QStringLiteral("输出目录；已存在时需 --overwrite。"),QStringLiteral("path"));
    const QCommandLineOption appId(QStringLiteral("app-id"),QStringLiteral("应用标识，缺省由名称推导。"),QStringLiteral("id"));
    const QCommandLineOption name(QStringLiteral("name"),QStringLiteral("应用名称，缺省取工程文件名。"),QStringLiteral("name"));
    const QCommandLineOption version(QStringLiteral("version"),QStringLiteral("应用版本，缺省 1.0.0。"),QStringLiteral("version"));
    const QCommandLineOption retainOriginal(QStringLiteral("retain-original"),QStringLiteral("同时保留原始工程文件。"));
    const QCommandLineOption overwrite(QStringLiteral("overwrite"),QStringLiteral("覆盖已存在的输出目录。"));
    const QCommandLineOption qtPrefix(QStringLiteral("qt-prefix"),QStringLiteral("Qt 安装目录或 windeployqt.exe；缺省用构建期 Qt。"),QStringLiteral("path"));
    const QCommandLineOption qmlDir(QStringLiteral("qml-dir"),QStringLiteral("QML 源目录；缺省从构建树推导 <exeDir>/../../src/qml。"),QStringLiteral("path"));
    const QCommandLineOption singleExe(QStringLiteral("single-exe"),QStringLiteral("同时把目录载荷封装为单文件 <output>.exe。"));
    const QCommandLineOption launcher(QStringLiteral("launcher"),QStringLiteral("CoRig-launcher.exe；缺省在发布器同目录查找。"),QStringLiteral("path"));
    const QCommandLineOption pythonExe(QStringLiteral("python-exe"),QStringLiteral("收集 Python 载荷用的本地 python.exe；工程含 Python 插件时必需。"),QStringLiteral("path"));
    const QCommandLineOption progress(QStringLiteral("progress"),QStringLiteral("向 stderr 输出 CORIG_PROGRESS 阶段行，供图形界面显示进度。"));
    parser.addOptions({project,output,appId,name,version,retainOriginal,overwrite,qtPrefix,qmlDir,singleExe,launcher,pythonExe,progress});
    parser.process(app);

    const auto arguments = parser.positionalArguments();
    if (arguments.isEmpty() || arguments.first() != QStringLiteral("publish")) {
        std::fprintf(stderr,"用法：CoRig-publish publish --project <工程> --output <目录> [--app-id <id>] [--name <名称>] [--version <版本>] [--retain-original] [--overwrite] [--qt-prefix <Qt>] [--qml-dir <QML 源目录>] [--single-exe] [--launcher <CoRig-launcher.exe>] [--python-exe <python.exe>]\n");
        return 1;
    }

    corig::PublishRequest request;
    request.projectPath = parser.value(project);
    request.outputPath = parser.value(output);
    request.appId = parser.value(appId);
    request.appName = parser.value(name);
    request.appVersion = parser.value(version);
    request.retainOriginal = parser.isSet(retainOriginal);
    request.overwrite = parser.isSet(overwrite);
    request.qtPrefix = parser.value(qtPrefix);
    request.qmlDir = parser.value(qmlDir);
    request.singleExe = parser.isSet(singleExe);
    request.launcherPath = parser.value(launcher);
    request.developerInterpreter = parser.value(pythonExe);
    if (parser.isSet(progress)) {
        request.progress = [](const QString &phase,int percent) {
            std::fprintf(stderr,"CORIG_PROGRESS %s %d\n",qUtf8Printable(phase),percent);
            std::fflush(stderr);
        };
    }
    if (request.appName.isEmpty()) request.appName = QFileInfo(request.projectPath).completeBaseName();
    if (request.appVersion.isEmpty()) request.appVersion = QStringLiteral("1.0.0");
    if (request.appId.isEmpty()) request.appId = defaultAppId(request.appName,request.projectPath);

    const auto result = corig::publishApplication(request);
    QJsonObject report;
    report.insert(QStringLiteral("ok"),result.ok);
    if (result.ok) {
        report.insert(QStringLiteral("output"),result.output);
        report.insert(QStringLiteral("release_hash"),result.releaseHash);
        report.insert(QStringLiteral("payload_bytes"),result.payloadBytes);
        if (!result.artifactPath.isEmpty()) {
            report.insert(QStringLiteral("single_exe"),result.artifactPath);
            report.insert(QStringLiteral("single_exe_bytes"),result.artifactBytes);
            report.insert(QStringLiteral("index_sha256"),result.indexHash);
        }
        QJsonArray warnings;
        for (const auto &warning : result.warnings) warnings.append(warning);
        report.insert(QStringLiteral("warnings"),warnings);
        if (!result.python.isEmpty() || !result.pythonChecks.isEmpty()) {
            QJsonObject python;
            python.insert(QStringLiteral("summary"),QJsonObject::fromVariantMap(result.python));
            QJsonArray checks;
            for (const auto &item : result.pythonChecks)
                checks.append(QJsonObject::fromVariantMap(item.toMap()));
            python.insert(QStringLiteral("checks"),checks);
            report.insert(QStringLiteral("python"),python);
        }
        // The runtime files are copied from the publisher's own build directory;
        // record identity so a payload mixing two builds is visible in the report.
        QJsonArray runtimeFiles;
        for (const auto &runtimeName : {QStringLiteral("CoRig.exe"),QStringLiteral("CoRigSdk.dll")}) {
            const auto relative = QStringLiteral("runtime/") + runtimeName;
            const QFileInfo file(QDir(result.output).filePath(relative));
            runtimeFiles.append(QJsonObject{{QStringLiteral("path"),relative},
                                            {QStringLiteral("sha256"),sha256File(file.absoluteFilePath())},
                                            {QStringLiteral("mtime"),file.lastModified().toString(Qt::ISODate)}});
        }
        report.insert(QStringLiteral("runtime_files"),runtimeFiles);
    } else {
        report.insert(QStringLiteral("error"),errorReport(result.error));
        if (!result.output.isEmpty()) report.insert(QStringLiteral("output"),result.output);
    }
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(),1,static_cast<size_t>(json.size()),stdout);
    std::fputc('\n',stdout);
    return result.ok ? 0 : 1;
}
