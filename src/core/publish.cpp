#include "publish.h"

#include "catalog.h"
#include "container.h"
#include "plugin_package.h"
#include "project.h"
#include "python_bundle.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace corig {
namespace {

QVariantMap err(const QString &code, const QString &phase, const QString &detail)
{
    return {{QStringLiteral("code"),code},{QStringLiteral("phase"),phase},{QStringLiteral("detail"),detail}};
}

PublishResult failed(const QString &code, const QString &phase, const QString &detail)
{
    PublishResult result;
    result.error = err(code,phase,detail);
    return result;
}

QString sha256Bytes(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw Error(QStringLiteral("无法读取：") + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const auto chunk = file.read(1024 * 1024);
        if (chunk.isEmpty()) {
            if (file.error() != QFileDevice::NoError) throw Error(QStringLiteral("无法读取：") + path);
            break;
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw Error(QStringLiteral("无法读取：") + path);
    return file.readAll();
}

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw Error(QStringLiteral("无法写入：") + path);
    if (file.write(bytes) != bytes.size() || !file.commit()) throw Error(QStringLiteral("无法写入：") + path);
}

void removeTree(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    QDir(path).removeRecursively();
}

bool copyFile(const QString &source, const QString &target)
{
    QFile::remove(target);
    return QFile::copy(source,target);
}

bool copyTree(const QString &source, const QString &target)
{
    const QDir root(source);
    QDirIterator entries(source,QDir::Files | QDir::NoSymLinks,QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        const auto path = entries.next();
        const auto destination = QDir(target).filePath(root.relativeFilePath(path));
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !copyFile(path,destination)) return false;
    }
    return true;
}

QString tail(const QString &text, int limit = 2000)
{
    const auto trimmed = text.trimmed();
    return trimmed.size() <= limit ? trimmed : trimmed.right(limit);
}

struct PublishContext {
    PublishRequest request;
    QString output;
    QString staging;
    QString releaseHash;
    QString phase = QStringLiteral("prepare");
    QStringList warnings;
    qint64 payloadBytes = 0;
    QString nativeDir;            // staging/.native: native plugin DLLs used for dependency probing
    QStringList nativeLibraries;  // file names inside nativeDir
    QString containerPart;        // <output>.exe.part-<pid>, removed unless the run succeeds
    QString containerFinal;       // <output>.exe
    qint64 containerBytes = 0;    // final artifact size once assembly finishes
    QString indexHash;            // SHA-256 of the container index
    bool python = false;          // the snapshot contains at least one Python plugin
    QVariantList pythonPlugins;   // declarations handed to the collector
    QVariantMap pythonSummary;    // collector document copied into PublishResult
    QVariantList pythonChecks;    // verifyPythonBundle checks
};

bool findWindeployqt(const QString &prefix, QString &tool, PublishResult &result)
{
    QString candidate;
    if (prefix.trimmed().isEmpty()) {
        candidate = QDir(QLibraryInfo::path(QLibraryInfo::BinariesPath)).filePath(QStringLiteral("windeployqt.exe"));
    } else {
        const QFileInfo given(prefix);
        candidate = given.isDir() ? QDir(prefix).filePath(QStringLiteral("windeployqt.exe")) : given.absoluteFilePath();
    }
    if (!QFileInfo(candidate).isFile()) {
        result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("找不到 windeployqt：") + candidate + QStringLiteral("；请用 --qt-prefix 指定 Qt 安装目录。"));
        return false;
    }
    tool = candidate;
    return true;
}

// Paths that differ only by case, separator or a symlink still name the same
// location on Windows; overlap checks must compare canonical forms.
QString canonicalPath(const QString &path)
{
    QFileInfo info(path);
    if (info.exists()) return QDir::cleanPath(info.canonicalFilePath());
    QString missing;
    while (!info.exists() && !info.fileName().isEmpty()) {
        missing = missing.isEmpty() ? info.fileName()
                                    : info.fileName() + QLatin1Char('/') + missing;
        info = QFileInfo(info.absolutePath());
    }
    if (!info.exists()) return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return QDir::cleanPath(info.canonicalFilePath() + QLatin1Char('/') + missing);
}

bool samePath(const QString &left, const QString &right)
{
    return QString::compare(QDir::cleanPath(left),QDir::cleanPath(right),
                            Qt::CaseInsensitive) == 0;
}

// True when child lives inside directory (both canonical absolute paths).
bool isAncestor(const QString &directory, const QString &child)
{
    const auto prefix = directory.endsWith(QLatin1Char('/')) ? directory
                                                             : directory + QLatin1Char('/');
    return child.size() > prefix.size() && child.startsWith(prefix,Qt::CaseInsensitive);
}

// The application identity becomes a directory name under LOCALAPPDATA on
// first launch; reject values that cannot be a stable, portable identifier.
bool validAppId(const QString &identifier)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{2,63}$"));
    return !identifier.contains(QLatin1Char('/')) && !identifier.contains(QLatin1Char('\\')) &&
           pattern.match(identifier).hasMatch();
}

// A Python plugin needs the bundled interpreter even when its package declares
// no distributions: collectPython always copies the base runtime, so an empty
// declaration list still produces <payload>/python. Returning the declarations
// here also keeps every Python node in the collector's plugin list.
bool pythonPlugins(const QVariantMap &snapshot, QVariantList &declarations)
{
    bool usesPython = false;
    QStringList nodeKinds;
    for (const auto &entry : snapshot.value(QStringLiteral("nodes")).toList()) {
        const auto kind = entry.toMap().value(QStringLiteral("plugin")).toString();
        if (!kind.isEmpty() && !nodeKinds.contains(kind)) nodeKinds.append(kind);
    }
    // The official function.python/protocol.python definitions are native plugins
    // that construct a Python worker internally (official_plugin.cpp), so their
    // catalog implementation never says "python". The design's node list is the
    // authority: a project that places one needs the interpreter in its payload.
    for (const auto &kind : nodeKinds)
        if (kind == QStringLiteral("function.python") || kind == QStringLiteral("protocol.python")) usesPython = true;
    for (const auto &entry : snapshot.value(QStringLiteral("plugin_packages")).toList()) {
        const auto bytes = entry.toMap().value(QStringLiteral("data")).toByteArray();
        if (bytes.isEmpty()) continue;
        const auto package = PluginPackage::read(bytes);
        for (const auto &definition : package.definitions()) {
            const auto spec = definition.toMap();
            const auto id = spec.value(QStringLiteral("id")).toString();
            const bool pythonDefinition = spec.value(QStringLiteral("implementation")).toString() == QStringLiteral("python");
            if (!pythonDefinition) continue;
            if (!nodeKinds.contains(id)) continue;
            usesPython = true;
            declarations.append(QVariantMap{{QStringLiteral("package"),package.id()},
                                            {QStringLiteral("id"),id},
                                            {QStringLiteral("python"),spec.value(QStringLiteral("python"))}});
        }
    }
    return usesPython;
}

// prepare: load the design, force-embed every package it uses, and write the
// snapshot into the staging tree. The source .crpj is only ever read.
bool prepare(PublishContext &ctx, PublishResult &result)
{
    if (!validAppId(ctx.request.appId)) {
        result.error = err(QStringLiteral("APP_ID_INVALID"),QStringLiteral("prepare"),
                           QStringLiteral("应用标识不合法：") + ctx.request.appId +
                           QStringLiteral("；需为 3-64 位小写字母或数字开头，仅含 a-z、0-9、点、下划线和连字符。"));
        return false;
    }
    QString code = QStringLiteral("SNAPSHOT_FAILED");
    try {
        if (!QDir().mkpath(ctx.staging + QStringLiteral("/project")))
            throw Error(QStringLiteral("无法创建临时目录：") + ctx.staging);
        code = QStringLiteral("PROJECT_INVALID");
        const auto design = loadProject(ctx.request.projectPath);
        code = QStringLiteral("PLUGIN_INVALID");
        Catalog catalog(bundledPackages());
        for (const auto &entry : design.value(QStringLiteral("plugin_packages")).toList()) {
            const auto item = entry.toMap();
            const auto bytes = item.value(QStringLiteral("data")).toByteArray();
            if (bytes.isEmpty()) continue;
            const auto package = PluginPackage::read(bytes);
            if (package.id() != item.value(QStringLiteral("id")).toString() ||
                package.hash() != item.value(QStringLiteral("sha256")).toString())
                throw Error(QStringLiteral("工程内插件包标识或校验值不一致：") + package.id());
            catalog.addPackage(package,true,item.value(QStringLiteral("relative_path")).toString());
        }
        code = QStringLiteral("PROJECT_INVALID");
        validateProject(design,catalog);
        code = QStringLiteral("MISSING_PLUGIN");
        const auto snapshot = catalog.embed(design,true);
        code = QStringLiteral("PROJECT_INVALID");
        validateProject(snapshot,catalog);
        code = QStringLiteral("SNAPSHOT_FAILED");
        const auto path = ctx.staging + QStringLiteral("/project/runtime.crpj");
        saveProject(path,snapshot);
        ctx.releaseHash = sha256Bytes(readFile(path));
        // The snapshot is authoritative: collection reads the embedded packages,
        // never the developer's library, so the payload matches what was saved.
        ctx.python = pythonPlugins(snapshot,ctx.pythonPlugins);
        if (ctx.python) {
            PythonBundleRequest bundle;
            bundle.developerInterpreter = ctx.request.developerInterpreter;
            bundle.payloadRoot = ctx.staging;
            bundle.projectPath = path;
            bundle.plugins = ctx.pythonPlugins;
            const auto collected = collectPython(bundle);
            if (!collected.ok) {
                result.error = collected.error;
                if (result.error.value(QStringLiteral("code")).toString().isEmpty())
                    result.error = err(QStringLiteral("PY_COLLECT_FAILED"),QStringLiteral("prepare"),
                                       QStringLiteral("Python 载荷收集失败。"));
                return false;
            }
            ctx.pythonSummary = collected.summary;
            result.python = ctx.pythonSummary;
            ctx.warnings << collected.warnings;
        }
        // Retention is a capability flag, not a second copy: the runtime snapshot
        // is the only project blob and project.export_original copies these exact
        // bytes. Storing the developer's on-disk file would both duplicate ~6 MiB
        // and could omit the embedded packages the application needs.
    }
    catch (const std::exception &e) {
        auto detail = QString::fromUtf8(e.what());
        if (code == QStringLiteral("PROJECT_INVALID") &&
            (detail.contains(QStringLiteral("插件不存在")) || detail.contains(QStringLiteral("工程缺少插件"))))
            code = QStringLiteral("MISSING_PLUGIN");
        result.error = err(code,QStringLiteral("prepare"),detail);
        return false;
    }
    return true;
}

// windeployqt 6.5+ ships the vc_redist installer instead of app-local CRT
// files. A payload must start without installing anything, so complete the
// runtime from the toolchain's redistributable directory when it is available.
bool ensureCompilerRuntime(const QString &runtime)
{
    const QStringList wanted{QStringLiteral("vcruntime140.dll"),QStringLiteral("vcruntime140_1.dll"),
                             QStringLiteral("msvcp140.dll"),QStringLiteral("concrt140.dll"),
                             QStringLiteral("msvcp140_1.dll"),QStringLiteral("msvcp140_2.dll")};
    QStringList missing;
    for (const auto &name : wanted) if (!QFileInfo(runtime + QLatin1Char('/') + name).isFile()) missing << name;
    if (missing.isEmpty()) return true;
    QStringList roots;
    // Installed packages carry an app-local copy under tools/vc/x64. The GUI
    // publisher is normally started from Explorer, so no Visual Studio shell
    // variables exist; prefer that packaged copy over environment discovery.
    const auto appDir = QCoreApplication::applicationDirPath();
    roots << QDir::cleanPath(appDir + QStringLiteral("/tools/vc/x64"))
          << QDir::cleanPath(appDir + QStringLiteral("/tools/vc"));
    const auto redist = qEnvironmentVariable("VCToolsRedistDir");
    if (!redist.isEmpty()) roots << QDir::cleanPath(redist + QStringLiteral("/x64")) << QDir::cleanPath(redist);
    const auto vc = qEnvironmentVariable("VCINSTALLDIR");
    if (!vc.isEmpty()) {
        const QDir msvc(QDir::cleanPath(vc + QStringLiteral("/Redist/MSVC")));
        for (const auto &version : msvc.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            roots << msvc.filePath(version + QStringLiteral("/x64"));
    }
    for (const auto &root : roots) {
        if (missing.isEmpty()) break;
        if (!QFileInfo(root).isDir()) continue;
        QDirIterator entries(root,QDir::Files,QDirIterator::Subdirectories);
        while (entries.hasNext() && !missing.isEmpty()) {
            const auto path = entries.next();
            const auto name = QFileInfo(path).fileName().toLower();
            if (!missing.contains(name)) continue;
            if (copyFile(path,runtime + QLatin1Char('/') + name)) missing.removeAll(name);
        }
    }
    // The redistributable installer is not app-local; the DLLs above are.
    if (missing.isEmpty()) QFile::remove(runtime + QStringLiteral("/vc_redist.x64.exe"));
    return missing.isEmpty();
}

// Qt 6.12's QRhi loads D3D shader compilers dynamically, so windeployqt
// cannot discover them from import tables. A clean receiver has neither the
// Windows SDK nor the Qt root on PATH; copy one coherent x64 set into the
// payload and verify it, rather than relying on the publisher's PATH.
bool ensureGraphicsRuntime(const QString &runtime, PublishResult &result)
{
    const auto appDir = QCoreApplication::applicationDirPath();
    const auto sdk = qEnvironmentVariable("WindowsSdkDir");
    const auto programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    QStringList sources{
        QDir(appDir).filePath(QStringLiteral("d3d/x64")),
        QDir(appDir).filePath(QStringLiteral("d3d"))
    };
    if (!sdk.trimmed().isEmpty())
        sources << QDir(sdk).filePath(QStringLiteral("Redist/D3D/x64"));
    if (!programFilesX86.trimmed().isEmpty())
        sources << QDir(programFilesX86).filePath(QStringLiteral("Windows Kits/10/Redist/D3D/x64"));
    sources << QStringLiteral("C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64");

    // dxcompiler.dll and dxil.dll are a matched pair; d3dcompiler_47.dll is
    // normally shipped by the same SDK redist. Select one complete directory
    // and copy all three as a set, overwriting whatever windeployqt staged, so
    // an incomplete app-local folder cannot mix files from different sources.
    const QStringList names{QStringLiteral("d3dcompiler_47.dll"),QStringLiteral("dxcompiler.dll"),
                            QStringLiteral("dxil.dll")};
    QString chosen;
    QStringList searched;
    for (const auto &source : sources) {
        const QDir directory(source);
        if (!directory.exists()) {
            searched << QDir::toNativeSeparators(source) + QStringLiteral("（不存在）");
            continue;
        }
        QStringList missing;
        for (const auto &name : names)
            if (!QFileInfo(directory.filePath(name)).isFile()) missing << name;
        if (missing.isEmpty()) { chosen = directory.absolutePath(); break; }
        searched << QDir::toNativeSeparators(source) + QStringLiteral("（缺少 ") +
                    missing.join(QStringLiteral("、")) + QStringLiteral("）");
    }
    if (chosen.isEmpty()) {
        result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("运行载荷缺少成套 D3D 图形编译器；需要同一目录同时包含 "
                                          "d3dcompiler_47.dll、dxcompiler.dll、dxil.dll。已查找：") +
                           searched.join(QStringLiteral("、")));
        return false;
    }
    for (const auto &name : names) {
        const auto source = QDir(chosen).filePath(name);
        const auto target = runtime + QLatin1Char('/') + name;
        if (!copyFile(source,target)) {
            result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                               QStringLiteral("无法复制 D3D 图形编译器：") + QDir::toNativeSeparators(source));
            return false;
        }
    }
    return true;
}

// A QML tree is not discoverable from the executable's imports. windeployqt
// needs the editor's QML sources to decide which QtQuick modules to ship.
bool resolveQmlDir(PublishContext &ctx, QString &directory, PublishResult &result)
{
    directory.clear();
    if (!ctx.request.qmlDir.trimmed().isEmpty()) {
        const QFileInfo given(ctx.request.qmlDir);
        if (!given.isDir()) {
            result.error = err(QStringLiteral("QML_DIR_MISSING"),QStringLiteral("collect"),
                               QStringLiteral("QML 源目录不存在：") + given.absoluteFilePath());
            return false;
        }
        directory = given.absoluteFilePath();
        return true;
    }
    const auto derived = QDir::cleanPath(QCoreApplication::applicationDirPath() +
                                         QStringLiteral("/../../src/qml"));
    if (QFileInfo(derived).isDir()) {
        directory = derived;
        return true;
    }
    ctx.warnings << QStringLiteral("未能推导 QML 源目录（") + derived +
        QStringLiteral("），载荷可能缺少 QtQuick 模块；请用 --qml-dir 指定 src/qml。");
    return true;
}

// Runs one windeployqt pass. The compiler runtime flag is repeated per pass
// because windeployqt only deploys the redistributable installer; the app-local
// CRT files are completed separately by ensureCompilerRuntime.
bool runDeploy(const QString &tool, const QStringList &arguments, const QString &what,
               PublishResult &result)
{
    QProcess deploy;
    deploy.setProgram(tool);
    deploy.setArguments(arguments);
    deploy.setProcessChannelMode(QProcess::MergedChannels);
    deploy.start();
    if (!deploy.waitForStarted(15000)) {
        result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("无法启动 windeployqt：") + tool);
        return false;
    }
    if (!deploy.waitForFinished(600000)) {
        deploy.kill();
        deploy.waitForFinished(5000);
        result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("windeployqt 超时（600 s）：") + what);
        return false;
    }
    const auto log = QString::fromLocal8Bit(deploy.readAll());
    if (deploy.exitStatus() != QProcess::NormalExit || deploy.exitCode() != 0) {
        result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("windeployqt 失败（") + what + QStringLiteral("，退出码 ") +
                           QString::number(deploy.exitCode()) + QStringLiteral("）：") + tail(log));
        return false;
    }
    return true;
}

// windeployqt follows the imports of the binaries it is given. A native plugin
// provider (corig_official.dll) imports Qt6SerialPort/Qt6Core5Compat that the
// executable never references, so each distinct provider DLL is extracted from
// the snapshot packages and scanned in a second pass.
bool extractNativeLibraries(PublishContext &ctx, PublishResult &result)
{
    const auto snapshotPath = ctx.staging + QStringLiteral("/project/runtime.crpj");
    QVariantMap snapshot;
    try {
        snapshot = loadProject(snapshotPath);
    }
    catch (const std::exception &e) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("无法读取运行时快照：") + QString::fromUtf8(e.what()));
        return false;
    }
    const auto directory = ctx.staging + QStringLiteral("/.native");
    if (!QDir().mkpath(directory)) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("无法创建插件探测目录：") + directory);
        return false;
    }
    QSet<QString> seen;
    QStringList libraries;
    for (const auto &entry : snapshot.value(QStringLiteral("plugin_packages")).toList()) {
        const auto item = entry.toMap();
        const auto bytes = item.value(QStringLiteral("data")).toByteArray();
        if (bytes.isEmpty()) continue;
        try {
            const auto package = PluginPackage::read(bytes);
            const auto native = package.manifest().value(QStringLiteral("native")).toMap();
            const auto library = native.value(QStringLiteral("library")).toString();
            if (library.isEmpty()) continue;
            const auto contents = package.file(library);
            if (contents.isEmpty()) continue;
            const auto digest = sha256Bytes(contents);
            if (seen.contains(digest)) continue;
            seen.insert(digest);
            const auto name = QString::number(seen.size()) + QLatin1Char('-') +
                              QFileInfo(library).fileName();
            writeBytes(directory + QLatin1Char('/') + name,contents);
            libraries << name;
        }
        catch (const std::exception &e) {
            result.error = err(QStringLiteral("PLUGIN_INVALID"),QStringLiteral("collect"),
                               QStringLiteral("无法从快照读取 C++ 插件库：") + QString::fromUtf8(e.what()));
            return false;
        }
    }
    ctx.nativeDir = directory;
    ctx.nativeLibraries = libraries;
    if (libraries.isEmpty())
        ctx.warnings << QStringLiteral("快照中没有 C++ 插件库，跳过插件 Qt 依赖反向收集。");
    return true;
}

// CI runs the payload with the offscreen platform; qwindows alone is not enough.
bool deployPlatforms(const QString &tool, const QString &runtime, PublishResult &result)
{
    QStringList roots;
    roots << QDir::cleanPath(QFileInfo(tool).absolutePath() +
                             QStringLiteral("/../plugins/platforms"));
    roots << QDir::cleanPath(QLibraryInfo::path(QLibraryInfo::PluginsPath) +
                             QStringLiteral("/platforms"));
    const QStringList names{QStringLiteral("qwindows.dll"),QStringLiteral("qoffscreen.dll"),
                            QStringLiteral("qminimal.dll")};
    const auto target = runtime + QStringLiteral("/platforms");
    if (!QDir().mkpath(target)) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("无法创建平台插件目录：") + target);
        return false;
    }
    QStringList missing;
    for (const auto &name : names) {
        QString source;
        for (const auto &root : roots) {
            const auto candidate = root + QLatin1Char('/') + name;
            if (QFileInfo(candidate).isFile()) { source = candidate; break; }
        }
        if (source.isEmpty()) { missing << name; continue; }
        if (!copyFile(source,target + QLatin1Char('/') + name)) {
            result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("collect"),
                               QStringLiteral("无法复制平台插件：") + source);
            return false;
        }
    }
    if (!missing.isEmpty()) {
        result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("找不到 Qt 平台插件：") + missing.join(QStringLiteral("、")));
        return false;
    }
    return true;
}

// Minimal PE reader: verify must prove that every Qt module a native plugin
// imports, and every symbol CoRig.exe imports from the SDK, actually ships,
// without depending on dumpbin being on PATH.
struct PeSection { quint32 va; quint32 size; quint32 raw; quint32 rawSize; };

class PeFile {
public:
    explicit PeFile(const QString &path) : bytes_(readFile(path)), path_(path)
    {
        if (bytes_.size() < 64 || u16(0) != 0x5A4D)
            throw Error(QStringLiteral("不是有效的 PE 文件：") + path_);
        const auto pe = static_cast<int>(u32(0x3C));
        if (pe < 0 || pe + 24 > bytes_.size() || u32(pe) != 0x00004550)
            throw Error(QStringLiteral("不是有效的 PE 文件：") + path_);
        const auto coff = pe + 4;
        const auto sectionCount = u16(coff + 2);
        const auto optionalSize = u16(coff + 16);
        const auto optional = coff + 20;
        if (optional + optionalSize > bytes_.size())
            throw Error(QStringLiteral("PE 头越界：") + path_);
        const auto magic = u16(optional);
        is64_ = magic == 0x20B;
        if (!is64_ && magic != 0x10B)
            throw Error(QStringLiteral("不支持的 PE 格式：") + path_);
        const auto directoryOffset = optional + (is64_ ? 112 : 96);
        if (directoryOffset + 16 * 8 > bytes_.size())
            throw Error(QStringLiteral("PE 数据目录越界：") + path_);
        for (int index = 0; index < 16; ++index)
            directories_.append({u32(directoryOffset + index * 8),
                                 u32(directoryOffset + index * 8 + 4)});
        const auto sectionBase = optional + optionalSize;
        for (int index = 0; index < sectionCount; ++index) {
            const auto entry = sectionBase + index * 40;
            if (entry + 40 > bytes_.size()) break;
            sections_.append({u32(entry + 12),u32(entry + 8),u32(entry + 20),u32(entry + 16)});
        }
    }

    quint16 u16(int offset) const
    {
        return static_cast<quint16>(static_cast<quint8>(bytes_[offset])) |
               (static_cast<quint16>(static_cast<quint8>(bytes_[offset + 1])) << 8);
    }
    quint32 u32(int offset) const
    {
        return static_cast<quint32>(static_cast<quint8>(bytes_[offset])) |
               (static_cast<quint32>(static_cast<quint8>(bytes_[offset + 1])) << 8) |
               (static_cast<quint32>(static_cast<quint8>(bytes_[offset + 2])) << 16) |
               (static_cast<quint32>(static_cast<quint8>(bytes_[offset + 3])) << 24);
    }
    quint64 u64(int offset) const
    {
        return static_cast<quint64>(u32(offset)) |
               (static_cast<quint64>(u32(offset + 4)) << 32);
    }
    bool is64() const { return is64_; }
    const QByteArray &bytes() const { return bytes_; }
    quint32 directory(int index) const
    {
        return index >= 0 && index < directories_.size() ? directories_[index].first : 0;
    }
    int mapRva(quint32 rva) const
    {
        for (const auto &section : sections_) {
            const auto span = qMax(section.size,section.rawSize);
            if (rva < section.va || rva >= section.va + span) continue;
            const auto delta = rva - section.va;
            if (delta >= section.rawSize) return -1;
            return static_cast<int>(section.raw + delta);
        }
        return -1;
    }
    QString cstring(quint32 rva) const
    {
        const auto offset = mapRva(rva);
        if (offset < 0 || offset >= bytes_.size()) return {};
        int end = offset;
        while (end < bytes_.size() && bytes_[end] != 0) ++end;
        return QString::fromLatin1(bytes_.mid(offset,end - offset));
    }
private:
    QByteArray bytes_;
    QString path_;
    bool is64_ = false;
    QVector<QPair<quint32,quint32>> directories_;
    QVector<PeSection> sections_;
};

QStringList importedLibraries(const QString &path)
{
    const PeFile file(path);
    const auto importRva = file.directory(1);
    if (importRva == 0) return {};
    auto offset = file.mapRva(importRva);
    if (offset < 0) return {};
    QStringList names;
    while (offset + 20 <= file.bytes().size()) {
        const auto nameRva = file.u32(offset + 12);
        if (nameRva == 0) break;
        names << file.cstring(nameRva);
        offset += 20;
    }
    return names;
}

struct ImportedSymbols {
    QStringList names;
    QVector<int> ordinals;
};

// Symbols a binary imports from one specific DLL; used to prove that the
// executable and the SDK in a payload were produced by the same build.
ImportedSymbols importsFromLibrary(const PeFile &file, const QString &library)
{
    ImportedSymbols result;
    const auto importRva = file.directory(1);
    if (importRva == 0) return result;
    auto offset = file.mapRva(importRva);
    if (offset < 0) return result;
    const int step = file.is64() ? 8 : 4;
    const quint64 ordinalFlag = file.is64() ? (quint64(1) << 63) : 0x80000000ull;
    while (offset + 20 <= file.bytes().size()) {
        const auto nameRva = file.u32(offset + 12);
        if (nameRva == 0) break;
        if (QString::compare(file.cstring(nameRva),library,Qt::CaseInsensitive) == 0) {
            const auto originalThunk = file.u32(offset + 0);
            const auto firstThunk = file.u32(offset + 16);
            const auto thunkRva = originalThunk != 0 ? originalThunk : firstThunk;
            auto thunk = file.mapRva(thunkRva);
            while (thunk >= 0 && thunk + step <= file.bytes().size()) {
                const quint64 value = file.is64() ? file.u64(thunk) : file.u32(thunk);
                if (value == 0) break;
                if (value & ordinalFlag) {
                    result.ordinals.append(static_cast<int>(value & 0xFFFF));
                } else {
                    const auto nameOffset = static_cast<quint32>(value & 0x7FFFFFFF);
                    result.names << file.cstring(nameOffset + 2);
                }
                thunk += step;
            }
        }
        offset += 20;
    }
    return result;
}

struct ExportTable {
    QSet<QString> names;
    quint32 base = 0;
    QVector<bool> present;
};

ExportTable exportsOf(const PeFile &file)
{
    ExportTable table;
    const auto exportRva = file.directory(0);
    if (exportRva == 0) return table;
    const auto offset = file.mapRva(exportRva);
    if (offset < 0 || offset + 40 > file.bytes().size()) return table;
    table.base = file.u32(offset + 16);
    const auto functions = file.u32(offset + 20);
    const auto names = file.u32(offset + 24);
    const auto functionsRva = file.u32(offset + 28);
    const auto namesRva = file.u32(offset + 32);
    const auto functionsOffset = file.mapRva(functionsRva);
    if (functionsOffset >= 0) {
        table.present.resize(static_cast<int>(functions));
        for (quint32 index = 0; index < functions; ++index) {
            const auto entry = functionsOffset + static_cast<int>(index) * 4;
            if (entry + 4 > file.bytes().size()) break;
            table.present[static_cast<int>(index)] = file.u32(entry) != 0;
        }
    }
    const auto namesOffset = file.mapRva(namesRva);
    for (quint32 index = 0; index < names && namesOffset >= 0; ++index) {
        const auto entry = namesOffset + static_cast<int>(index) * 4;
        if (entry + 4 > file.bytes().size()) break;
        table.names.insert(file.cstring(file.u32(entry)));
    }
    return table;
}

// collect: copy the executable and the SDK DLL, then let windeployqt resolve
// the Qt tree and the compiler runtime into the same directory.
bool collect(PublishContext &ctx, PublishResult &result)
{
    const QDir exeDir(QCoreApplication::applicationDirPath());
    const auto executable = exeDir.filePath(QStringLiteral("CoRig.exe"));
    const auto sdk = exeDir.filePath(QStringLiteral("CoRigSdk.dll"));
    if (!QFileInfo(executable).isFile()) {
        result.error = err(QStringLiteral("RUNTIME_MISSING"),QStringLiteral("collect"),
                           QStringLiteral("找不到 CoRig.exe：") + executable + QStringLiteral("；CoRig-publish 必须与 CoRig.exe 放在同一目录。"));
        return false;
    }
    if (!QFileInfo(sdk).isFile()) {
        result.error = err(QStringLiteral("RUNTIME_MISSING"),QStringLiteral("collect"),
                           QStringLiteral("找不到 CoRigSdk.dll：") + sdk);
        return false;
    }
    const auto runtime = ctx.staging + QStringLiteral("/runtime");
    if (!QDir().mkpath(runtime) || !copyFile(executable,runtime + QStringLiteral("/CoRig.exe")) ||
        !copyFile(sdk,runtime + QStringLiteral("/CoRigSdk.dll"))) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("无法复制运行文件到载荷：") + runtime);
        return false;
    }
    QString tool;
    if (!findWindeployqt(ctx.request.qtPrefix,tool,result)) return false;
    QString qmlDir;
    if (!resolveQmlDir(ctx,qmlDir,result)) return false;
    const QStringList base{QStringLiteral("--release"),QStringLiteral("--no-translations"),
                           QStringLiteral("--compiler-runtime")};
    QStringList deployArgs = base;
    if (!qmlDir.isEmpty())
        deployArgs << QStringLiteral("--qmldir") << QDir::toNativeSeparators(qmlDir);
    deployArgs << QStringLiteral("--dir") << QDir::toNativeSeparators(runtime)
               << QDir::toNativeSeparators(runtime + QStringLiteral("/CoRig.exe"));
    if (!runDeploy(tool,deployArgs,QStringLiteral("Qt 依赖"),result)) return false;
    if (!extractNativeLibraries(ctx,result)) return false;
    for (const auto &library : ctx.nativeLibraries) {
        QStringList args = base;
        args << QStringLiteral("--dir") << QDir::toNativeSeparators(runtime)
             << QDir::toNativeSeparators(ctx.nativeDir + QLatin1Char('/') + library);
        if (!runDeploy(tool,args,QStringLiteral("插件依赖（") + library + QStringLiteral("）"),result))
            return false;
    }
    if (!deployPlatforms(tool,runtime,result)) return false;
    if (!ensureCompilerRuntime(runtime)) {
        result.error = err(QStringLiteral("DEPLOY_FAILED"),QStringLiteral("collect"),
                           QStringLiteral("未能收集 VC++ x64 运行库；请安装 Visual Studio 生成工具或 VC++ 2015-2022 运行库。"));
        return false;
    }
    if (!ensureGraphicsRuntime(runtime,result)) return false;
    const QDir licenses(exeDir.filePath(QStringLiteral("licenses")));
    if (licenses.exists()) {
        if (!copyTree(licenses.absolutePath(),ctx.staging + QStringLiteral("/licenses"))) {
            result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("collect"),
                               QStringLiteral("无法复制 licenses/ 到载荷"));
            return false;
        }
    } else {
        ctx.warnings << QStringLiteral("未找到 licenses/，发布载荷不含许可证文本。");
    }
    return true;
}

// verify: a payload without the compiler runtime cannot start on a clean
// machine, so this is a hard failure reported under the collect phase. QML
// modules, the offscreen platform and every Qt import of the native plugins are
// checked here because a missing one only shows up as a load failure at run time.
bool verify(PublishContext &ctx, PublishResult &result)
{
    const auto runtime = ctx.staging + QStringLiteral("/runtime");
    QStringList required{QStringLiteral("CoRig.exe"),QStringLiteral("CoRigSdk.dll"),
        QStringLiteral("Qt6Core.dll"),QStringLiteral("Qt6Gui.dll"),QStringLiteral("Qt6Qml.dll"),
        QStringLiteral("Qt6Quick.dll"),QStringLiteral("Qt6Network.dll"),QStringLiteral("Qt6Svg.dll"),
        QStringLiteral("Qt6QuickControls2.dll"),QStringLiteral("VCRUNTIME140.dll"),
        QStringLiteral("MSVCP140.dll"),QStringLiteral("VCRUNTIME140_1.dll"),
        QStringLiteral("d3dcompiler_47.dll"),QStringLiteral("dxcompiler.dll"),QStringLiteral("dxil.dll")};
    // SerialPort and Core5Compat are imported by the official native provider,
    // not by CoRig.exe. A Python-only payload has no provider DLL and must not be
    // rejected for modules nothing in it imports.
    if (!ctx.nativeLibraries.isEmpty())
        required << QStringLiteral("Qt6SerialPort.dll") << QStringLiteral("Qt6Core5Compat.dll");
    QStringList missing;
    for (const auto &name : required)
        if (!QFileInfo(runtime + QLatin1Char('/') + name).isFile()) missing << name;
    const QStringList platforms{QStringLiteral("qwindows.dll"),QStringLiteral("qoffscreen.dll"),
                                QStringLiteral("qminimal.dll")};
    for (const auto &name : platforms)
        if (!QFileInfo(runtime + QStringLiteral("/platforms/") + name).isFile())
            missing << QStringLiteral("platforms/") + name;
    const QDir qml(runtime + QStringLiteral("/qml"));
    int qmlFiles = 0;
    if (qml.exists()) {
        QDirIterator entries(qml.absolutePath(),QDir::Files,QDirIterator::Subdirectories);
        while (entries.hasNext()) { entries.next(); ++qmlFiles; }
        const auto modules = qml.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (qmlFiles == 0)
            missing << QStringLiteral("qml/（目录为空）");
        else if (std::none_of(modules.cbegin(),modules.cend(),[](const QString &name) {
                     return name.startsWith(QStringLiteral("QtQuick"),Qt::CaseInsensitive);
                 }))
            missing << QStringLiteral("qml/QtQuick");
    } else {
        missing << QStringLiteral("qml/");
    }
    QStringList pluginMissing;
    for (const auto &library : ctx.nativeLibraries) {
        QStringList imports;
        try {
            imports = importedLibraries(ctx.nativeDir + QLatin1Char('/') + library);
        }
        catch (const std::exception &e) {
            result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("verify"),
                               QStringLiteral("无法解析 C++ 插件依赖：") + library + QStringLiteral("：") +
                               QString::fromUtf8(e.what()));
            return false;
        }
        for (const auto &import : imports) {
            if (!import.startsWith(QStringLiteral("Qt6"),Qt::CaseInsensitive)) continue;
            if (!QFileInfo(runtime + QLatin1Char('/') + import).isFile())
                pluginMissing << library + QStringLiteral(" -> ") + import;
        }
    }
    for (const auto &item : pluginMissing) missing << item;
    // A payload that mixes CoRig.exe and CoRigSdk.dll from different build trees
    // links but dies at start-up with STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139).
    // Every symbol the executable imports from the SDK must exist in its exports.
    QStringList sdkMissing;
    try {
        const PeFile executable(runtime + QStringLiteral("/CoRig.exe"));
        const PeFile sdk(runtime + QStringLiteral("/CoRigSdk.dll"));
        const auto imported = importsFromLibrary(executable,QStringLiteral("CoRigSdk.dll"));
        const auto exported = exportsOf(sdk);
        for (const auto &name : imported.names)
            if (!exported.names.contains(name))
                sdkMissing << QStringLiteral("CoRigSdk.dll!") + name;
        for (const auto ordinal : imported.ordinals) {
            const auto index = ordinal - static_cast<int>(exported.base);
            if (index < 0 || index >= exported.present.size() || !exported.present[index])
                sdkMissing << QStringLiteral("CoRigSdk.dll!#") + QString::number(ordinal);
        }
    }
    catch (const std::exception &e) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("verify"),
                           QStringLiteral("无法校验 CoRig.exe 与 CoRigSdk.dll 的导入导出一致性：") +
                           QString::fromUtf8(e.what()));
        return false;
    }
    if (missing.isEmpty() && sdkMissing.isEmpty()) {
        if (ctx.python) {
            const auto python = verifyPythonBundle(ctx.staging);
            ctx.pythonChecks = python.value(QStringLiteral("checks")).toList();
            result.pythonChecks = ctx.pythonChecks;
            if (!python.value(QStringLiteral("ok")).toBool()) {
                QStringList failed;
                for (const auto &item : ctx.pythonChecks) {
                    const auto check = item.toMap();
                    if (check.value(QStringLiteral("ok")).toBool()) continue;
                    const auto detail = check.value(QStringLiteral("detail")).toString();
                    failed << check.value(QStringLiteral("name")).toString() +
                                  (detail.isEmpty() ? QString() : QStringLiteral("（") + detail + QStringLiteral("）"));
                }
                auto error = python.value(QStringLiteral("error")).toMap();
                if (error.isEmpty())
                    error = err(QStringLiteral("PY_PROBE_FAILED"),QStringLiteral("verify"),
                                QStringLiteral("Python 载荷校验未通过：") + failed.join(QStringLiteral("、")));
                else if (error.value(QStringLiteral("phase")).toString().isEmpty())
                    error[QStringLiteral("phase")] = QStringLiteral("verify");
                result.error = error;
                return false;
            }
        }
        if (sha256File(ctx.staging + QStringLiteral("/project/runtime.crpj")) != ctx.releaseHash) {
            result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("verify"),
                               QStringLiteral("快照哈希与准备阶段不一致"));
            return false;
        }
        // The extracted provider DLLs are only a probe input, never payload.
        removeTree(ctx.nativeDir);
        return true;
    }
    const bool compilerRuntime = missing.contains(QStringLiteral("VCRUNTIME140.dll")) ||
        missing.contains(QStringLiteral("MSVCP140.dll")) || missing.contains(QStringLiteral("VCRUNTIME140_1.dll"));
    QStringList reasons;
    if (!missing.isEmpty()) reasons << QStringLiteral("运行载荷缺少：") + missing.join(QStringLiteral("、"));
    if (!sdkMissing.isEmpty())
        reasons << QStringLiteral("CoRig.exe 引用的 CoRigSdk.dll 导出缺失：") + sdkMissing.join(QStringLiteral("、"));
    auto detail = reasons.join(QStringLiteral("；"));
    if (compilerRuntime)
        detail += QStringLiteral("；windeployqt --compiler-runtime 未能收集 VC++ 运行库。请在 Visual Studio 开发者命令提示符中发布，或安装 VC++ 2015-2022 运行库后重试。");
    result.error = err(QStringLiteral("PUBLISH_FAILED"),compilerRuntime ? QStringLiteral("collect") : QStringLiteral("verify"),detail);
    return false;
}

bool assemble(PublishContext &ctx, PublishResult &result)
{
    try {
        const QJsonObject app{{QStringLiteral("id"),ctx.request.appId},{QStringLiteral("name"),ctx.request.appName},
                              {QStringLiteral("version"),ctx.request.appVersion}};
        const QJsonObject release{{QStringLiteral("snapshot_sha256"),ctx.releaseHash},
                                  {QStringLiteral("created_at"),QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                                  {QStringLiteral("corig_version"),QCoreApplication::applicationVersion()}};
        const QJsonObject runtime{{QStringLiteral("executable"),QStringLiteral("runtime/CoRig.exe")}};
        const QJsonObject manifest{{QStringLiteral("format"),QStringLiteral("corig-app")},
                                   {QStringLiteral("format_version"),1},
                                   {QStringLiteral("mode"),QStringLiteral("publish")},
                                   {QStringLiteral("app"),app},
                                   {QStringLiteral("release"),release},
                                   {QStringLiteral("retain_original"),ctx.request.retainOriginal},
                                   {QStringLiteral("runtime"),runtime}};
        writeBytes(ctx.staging + QStringLiteral("/manifest.json"),QJsonDocument(manifest).toJson(QJsonDocument::Indented));

        QStringList paths;
        const QDir root(ctx.staging);
        QDirIterator entries(ctx.staging,QDir::Files | QDir::NoSymLinks,QDirIterator::Subdirectories);
        while (entries.hasNext()) {
            const auto path = entries.next();
            const auto relative = root.relativeFilePath(path);
            if (relative == QStringLiteral("payload-index.json")) continue;
            paths << relative;
        }
        paths.sort();
        QJsonArray files;
        qint64 bytes = 0;
        for (const auto &relative : paths) {
            const auto path = ctx.staging + QLatin1Char('/') + relative;
            const auto size = QFileInfo(path).size();
            files.append(QJsonObject{{QStringLiteral("path"),relative},{QStringLiteral("size"),size},
                                     {QStringLiteral("sha256"),sha256File(path)}});
            bytes += size;
        }
        const QJsonObject index{{QStringLiteral("format"),QStringLiteral("corig-payload-index")},
                                {QStringLiteral("format_version"),1},
                                {QStringLiteral("files"),files}};
        const auto indexBytes = QJsonDocument(index).toJson(QJsonDocument::Indented);
        writeBytes(ctx.staging + QStringLiteral("/payload-index.json"),indexBytes);
        ctx.payloadBytes = bytes + indexBytes.size();
    }
    catch (const std::exception &e) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("assemble"),QString::fromUtf8(e.what()));
        return false;
    }
    return true;
}


// container: wraps the staged directory into one executable. The launcher PE is
// copied verbatim, every staged file follows at its recorded offset, and a
// binary index plus a 64-byte trailer close the file. Compression is store-only:
// the payload is dominated by Qt DLLs and QML data, and a decompressor would
// have to live in the Qt-free launcher for little gain.
bool assembleContainer(PublishContext &ctx, PublishResult &result)
{
    if (!ctx.request.singleExe) return true;
    try {
        auto launcher = ctx.request.launcherPath.trimmed();
        if (launcher.isEmpty())
            launcher = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("CoRig-launcher.exe"));
        const QFileInfo launcherInfo(launcher);
        if (!launcherInfo.isFile()) {
            result.error = err(QStringLiteral("LAUNCHER_MISSING"),QStringLiteral("container"),
                               QStringLiteral("找不到启动器：") + launcher +
                               QStringLiteral("；请用 --launcher 指定 CoRig-launcher.exe。"));
            return false;
        }
        if (launcherInfo.size() <= 0 || launcherInfo.size() > 256ll * 1024 * 1024)
            throw Error(QStringLiteral("启动器大小超出范围：") + launcherInfo.absoluteFilePath());
        try {
            const PeFile probe(launcherInfo.absoluteFilePath());
            (void)probe;
        }
        catch (const std::exception &e) {
            result.error = err(QStringLiteral("LAUNCHER_INVALID"),QStringLiteral("container"),
                               QStringLiteral("启动器不是有效的 PE 文件：") + QString::fromUtf8(e.what()));
            return false;
        }
        const auto launcherBytes = readFile(launcherInfo.absoluteFilePath());

        QStringList paths;
        const QDir root(ctx.staging);
        QDirIterator entries(ctx.staging,QDir::Files | QDir::NoSymLinks,QDirIterator::Subdirectories);
        while (entries.hasNext()) paths << root.relativeFilePath(entries.next());
        paths.sort();

        std::vector<container::Entry> indexEntries;
        std::uint64_t offset = static_cast<std::uint64_t>(launcherBytes.size());
        for (const auto &relative : paths) {
            const QFileInfo file(ctx.staging + QLatin1Char('/') + relative);
            if (!file.isFile()) continue;
            container::Entry entry;
            const auto pathBytes = relative.toUtf8();
            entry.path.assign(pathBytes.constData(),static_cast<std::size_t>(pathBytes.size()));
            container::Error pathError = container::Error::None;
            if (!container::validPath(entry.path,&pathError))
                throw Error(QStringLiteral("无法写入容器索引的路径：") + relative +
                            QStringLiteral("（") + QLatin1String(container::detail(pathError).c_str()) + QStringLiteral("）"));
            const auto digest = QByteArray::fromHex(sha256File(file.absoluteFilePath()).toLatin1());
            if (digest.size() != 32) throw Error(QStringLiteral("无法计算文件哈希：") + file.absoluteFilePath());
            std::memcpy(entry.sha256.data(),digest.constData(),32);
            entry.size = static_cast<std::uint64_t>(file.size());
            entry.offset = offset;
            offset += entry.size;
            if (offset > container::kMaxPayloadBytes)
                throw Error(QStringLiteral("载荷超过 8 GiB 上限。"));
            indexEntries.push_back(entry);
            if (indexEntries.size() > container::kMaxEntries)
                throw Error(QStringLiteral("文件数超过容器上限。"));
        }

        container::Error indexError = container::Error::None;
        const auto index = container::encodeIndex(indexEntries,&indexError);
        if (index.empty() || indexError != container::Error::None)
            throw Error(QStringLiteral("无法编码容器索引：") +
                        QLatin1String(container::detail(indexError).c_str()));
        if (index.size() > container::kMaxIndexBytes)
            throw Error(QStringLiteral("容器索引超过 64 MiB 上限。"));
        const auto indexBytes = QByteArray(reinterpret_cast<const char *>(index.data()),
                                           static_cast<int>(index.size()));
        const auto indexHash = QCryptographicHash::hash(indexBytes,QCryptographicHash::Sha256);

        container::Trailer trailer;
        trailer.version = container::kVersion;
        trailer.flags = 0;
        trailer.launcherSize = static_cast<std::uint32_t>(launcherBytes.size());
        trailer.indexOffset = offset;
        trailer.indexSize = index.size();
        std::memcpy(trailer.indexSha256.data(),indexHash.constData(),32);
        const auto trailerBytes = container::encodeTrailer(trailer);

        const auto finalPath = ctx.output + QStringLiteral(".exe");
        if (QFileInfo::exists(finalPath) && !ctx.request.overwrite) {
            result.error = err(QStringLiteral("OUTPUT_EXISTS"),QStringLiteral("container"),
                               QStringLiteral("输出已存在：") + finalPath + QStringLiteral("；使用 --overwrite 覆盖。"));
            return false;
        }
        const auto pid = QString::number(QCoreApplication::applicationPid());
        const auto part = finalPath + QStringLiteral(".part-") + pid;
        // Record the part path before writing so every failure path removes it.
        ctx.containerPart = part;
        ctx.containerFinal = finalPath;
        QFile::remove(part);
        QFile output(part);
        if (!output.open(QIODevice::WriteOnly)) {
            result.error = err(QStringLiteral("COMMIT_FAILED"),QStringLiteral("container"),
                               QStringLiteral("无法写入：") + part);
            return false;
        }
        qint64 written = 0;
        const auto append = [&](const QByteArray &bytes) {
            if (output.write(bytes) != bytes.size()) throw Error(QStringLiteral("写入失败：") + part);
            written += bytes.size();
        };
        append(launcherBytes);
        for (const auto &entry : indexEntries) {
            const auto relative = QString::fromUtf8(entry.path.data(),static_cast<int>(entry.path.size()));
            QFile input(ctx.staging + QLatin1Char('/') + relative);
            if (!input.open(QIODevice::ReadOnly)) throw Error(QStringLiteral("无法读取：") + relative);
            while (!input.atEnd()) {
                const auto chunk = input.read(4 * 1024 * 1024);
                if (chunk.isEmpty() && input.error() != QFileDevice::NoError)
                    throw Error(QStringLiteral("读取失败：") + relative);
                if (!chunk.isEmpty()) append(chunk);
            }
        }
        append(indexBytes);
        append(QByteArray(reinterpret_cast<const char *>(trailerBytes.data()),
                          static_cast<int>(trailerBytes.size())));
        output.flush();
        output.close();
        if (output.error() != QFileDevice::NoError) {
            QFile::remove(part);
            result.error = err(QStringLiteral("COMMIT_FAILED"),QStringLiteral("container"),
                               QStringLiteral("写入失败：") + part);
            return false;
        }
        if (QFileInfo(part).size() != written) {
            QFile::remove(part);
            result.error = err(QStringLiteral("COMMIT_FAILED"),QStringLiteral("container"),
                               QStringLiteral("输出大小不一致：") + part);
            return false;
        }
        ctx.containerBytes = written;
        ctx.indexHash = QString::fromLatin1(indexHash.toHex());
        result.indexHash = ctx.indexHash;
    }
    catch (const std::exception &e) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),QStringLiteral("container"),QString::fromUtf8(e.what()));
        return false;
    }
    return true;
}

// commit: rename the staging directory over the output. An existing output is
// moved aside first and restored if the final rename fails.
bool commit(PublishContext &ctx, PublishResult &result)
{
    const auto pid = QString::number(QCoreApplication::applicationPid());
    const auto previous = ctx.output + QStringLiteral(".old-") + pid;
    removeTree(previous);
    const bool existed = QFileInfo::exists(ctx.output);
    if (existed && !QDir().rename(ctx.output,previous)) {
        result.error = err(QStringLiteral("COMMIT_FAILED"),QStringLiteral("commit"),
                           QStringLiteral("无法移动已有输出：") + ctx.output);
        return false;
    }
    if (!QDir().rename(ctx.staging,ctx.output)) {
        if (existed) QDir().rename(previous,ctx.output);
        result.error = err(QStringLiteral("COMMIT_FAILED"),QStringLiteral("commit"),
                           QStringLiteral("无法写入输出：") + ctx.output);
        return false;
    }
    if (existed) removeTree(previous);
    return true;
}

} // namespace

PublishResult publishApplication(const PublishRequest &request)
{
    PublishResult result;
    PublishContext ctx;
    ctx.request = request;
    if (request.projectPath.trimmed().isEmpty()) return failed(QStringLiteral("INVALID_REQUEST"),QStringLiteral("prepare"),QStringLiteral("缺少工程路径。"));
    if (request.outputPath.trimmed().isEmpty()) return failed(QStringLiteral("INVALID_REQUEST"),QStringLiteral("prepare"),QStringLiteral("缺少输出路径。"));
    if (!QFileInfo(request.projectPath).isFile())
        return failed(QStringLiteral("PROJECT_NOT_FOUND"),QStringLiteral("prepare"),QStringLiteral("工程文件不存在：") + request.projectPath);
    const auto projectCanonical = canonicalPath(request.projectPath);
    const auto outputCanonical = canonicalPath(request.outputPath);
    ctx.output = QFileInfo(request.outputPath).absoluteFilePath();
    result.output = ctx.output;
    // --overwrite renames the output aside and deletes it after commit; an output
    // that equals, contains or sits inside the source project would destroy the
    // developer's .crpj, so reject the request before touching the disk.
    if (samePath(projectCanonical,outputCanonical) ||
        isAncestor(outputCanonical,projectCanonical) ||
        isAncestor(projectCanonical,outputCanonical))
        return failed(QStringLiteral("OUTPUT_OVERLAPS_PROJECT"),QStringLiteral("prepare"),
                      QStringLiteral("输出目录与源工程重叠：") + ctx.output +
                      QStringLiteral(" 与 ") + projectCanonical +
                      QStringLiteral("；请选择工程之外的位置。"));
    if (QFileInfo::exists(ctx.output) && !request.overwrite)
        return failed(QStringLiteral("OUTPUT_EXISTS"),QStringLiteral("prepare"),
                      QStringLiteral("输出已存在：") + ctx.output + QStringLiteral("；使用 --overwrite 覆盖。"));
    const auto parent = QFileInfo(ctx.output).absoluteDir();
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath()))
        return failed(QStringLiteral("INVALID_REQUEST"),QStringLiteral("prepare"),
                      QStringLiteral("无法创建输出目录：") + parent.absolutePath());
    ctx.staging = ctx.output + QStringLiteral(".part-") + QString::number(QCoreApplication::applicationPid());
    removeTree(ctx.staging);
    const auto discard = [&]() {
        removeTree(ctx.staging);
        if (!ctx.containerPart.isEmpty()) QFile::remove(ctx.containerPart);
    };
    const auto phase = [&](const QString &name,int percent) {
        ctx.phase = name;
        if (ctx.request.progress) ctx.request.progress(name,percent);
    };
    try {
        phase(QStringLiteral("prepare"),5);
        if (!prepare(ctx,result)) { discard(); return result; }
        phase(QStringLiteral("collect"),25);
        if (!collect(ctx,result)) { discard(); return result; }
        phase(QStringLiteral("verify"),70);
        if (!verify(ctx,result)) { discard(); return result; }
        phase(QStringLiteral("assemble"),85);
        if (!assemble(ctx,result)) { discard(); return result; }
        phase(QStringLiteral("container"),92);
        if (!assembleContainer(ctx,result)) { discard(); return result; }
        phase(QStringLiteral("commit"),98);
        if (!commit(ctx,result)) { discard(); return result; }
        if (ctx.request.progress) ctx.request.progress(QStringLiteral("done"),100);
    }
    catch (const std::exception &e) {
        result.error = err(QStringLiteral("PUBLISH_FAILED"),ctx.phase,QString::fromUtf8(e.what()));
        discard();
        return result;
    }
    if (!ctx.containerPart.isEmpty()) {
        const auto part = ctx.containerPart;
        const auto finalPath = ctx.containerFinal;
#ifdef Q_OS_WIN
        const auto moved = MoveFileExW(reinterpret_cast<const wchar_t *>(part.utf16()),
                                       reinterpret_cast<const wchar_t *>(finalPath.utf16()),
                                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        QFile::remove(finalPath);
        const auto moved = QFile::rename(part,finalPath);
#endif
        if (!moved) {
            QFile::remove(part);
            result.error = err(QStringLiteral("COMMIT_FAILED"),QStringLiteral("commit"),
                               QStringLiteral("无法写入单文件产物：") + finalPath);
            return result;
        }
        result.artifactPath = finalPath;
        result.artifactBytes = QFileInfo(finalPath).size();
        result.indexHash = ctx.indexHash;
    }
    result.ok = true;
    result.releaseHash = ctx.releaseHash;
    result.payloadBytes = ctx.payloadBytes;
    result.python = ctx.pythonSummary;
    result.pythonChecks = ctx.pythonChecks;
    result.warnings = ctx.warnings;
    return result;
}

} // namespace corig
