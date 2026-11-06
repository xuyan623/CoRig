#include "run_mode.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace corig {
namespace {

// Installed once by initializeRunOptions; every accessor below is read-only.
RunOptions g_options;

void reject(RunModeError *error, const QString &detail);

qint64 currentProcessId()
{
#ifdef Q_OS_WIN
    return qint64(GetCurrentProcessId());
#else
    return qint64(getpid());
#endif
}

QString currentExecutablePath()
{
#ifdef Q_OS_WIN
    wchar_t buffer[32768];
    const auto length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (!length || length >= 32768) return {};
    const QFileInfo info(QString::fromWCharArray(buffer, int(length)));
#else
    const QFileInfo info(QString::fromLocal8Bit("/proc/self/exe"));
#endif
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

// A published application keeps its data beside the executable. The launcher
// passes the outer work root explicitly; a directly launched payload is
// recognised from its <root>/cache/payloads/<id> layout instead. Both paths are
// resolved before QGuiApplication exists.
QString applicationRoot(const QString &payloadRoot, RunModeError *error)
{
    const auto fromEnvironment = qEnvironmentVariable("CORIG_APP_ROOT").trimmed();
    if (!fromEnvironment.isEmpty()) {
        const auto root = QDir::cleanPath(fromEnvironment);
        // The launcher passes the outer work root; it must own the payload. This
        // keeps the internal override from pointing at another application's data.
        if (!QDir::isAbsolutePath(root) || !payloadRoot.startsWith(root + QStringLiteral("/cache/payloads/"), Qt::CaseInsensitive)) {
            reject(error, "应用工作根与载荷布局不一致");
            return {};
        }
        return root;
    }
    const QDir payload(payloadRoot);
    const auto id = payload.dirName();
    const auto payloads = QFileInfo(payload.absolutePath()).fileName();
    const auto cache = QFileInfo(QFileInfo(payload.absolutePath()).absolutePath()).fileName();
    if (payloads == QStringLiteral("payloads") && cache == QStringLiteral("cache") && !id.isEmpty())
        return QDir::cleanPath(payload.absoluteFilePath(QStringLiteral("../../..")));
    return {};
}

void reject(RunModeError *error, const QString &detail)
{
    if (error) *error = {QStringLiteral("PAYLOAD_INCOMPLETE"), detail};
}

bool samePath(const QString &left, const QString &right)
{
    const auto a = QDir::cleanPath(left), b = QDir::cleanPath(right);
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

// Containment assertion used by computePublishPaths. Paths that do not exist yet
// are compared after cleanPath, which still catches ".." escapes because no root
// is built from user input other than the validated app id.
bool isContained(const QString &base, const QString &candidate)
{
    const auto canonical = [](const QString &path) {
        const auto resolved = QFileInfo(path).canonicalFilePath();
        return QDir::cleanPath(resolved.isEmpty() ? path : resolved);
    };
    auto root = canonical(base), target = canonical(candidate);
#ifdef Q_OS_WIN
    root = root.toCaseFolded(); target = target.toCaseFolded();
#endif
    return target == root || target.startsWith(root + QLatin1Char('/'));
}

// An extracted payload always has the layout <root>/cache/payloads/<id>/runtime.
// Recognising it lets startup refuse an editor fallback when its manifest is gone.
bool looksLikeExtractedPayload(const QString &exeDirectory)
{
    if (QFileInfo(exeDirectory).fileName() != QStringLiteral("runtime")) return false;
    // <root>/cache/payloads/<id>/runtime: the executable's parent is <id>, its
    // grandparent is payloads/ and its great-grandparent is cache/.
    const auto payloadDir = QFileInfo(QDir::cleanPath(exeDirectory + "/..")).absoluteFilePath();
    const auto payloadsDir = QFileInfo(payloadDir).absolutePath();
    const auto cacheDir = QFileInfo(payloadsDir).absolutePath();
    return QFileInfo(payloadDir).fileName().size() > 0
        && QFileInfo(payloadsDir).fileName() == QStringLiteral("payloads")
        && QFileInfo(cacheDir).fileName() == QStringLiteral("cache");
}

// A payload manifest must be a plain file: a directory junction or symbolic link
// could point the runtime at a manifest the user never installed.
bool isPlainFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink()) return false;
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    const auto attributes = GetFileAttributesW(reinterpret_cast<const wchar_t *>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) return false;
#endif
    return true;
}

void configureEditorPaths(RunOptions &options)
{
    const auto directory = QFileInfo(currentExecutablePath()).absolutePath();
    options = RunOptions{};
    options.mode = RunMode::Editor;
    options.payloadRoot = directory;
    options.dataRoot = directory;
    options.cacheRoot = directory + "/cache";
    options.tempRoot = directory + "/cache/temp";
    options.pluginRoot = directory + "/plugins";
    options.pluginCacheRoot = directory + "/cache/plugin-packages";
    options.crashRoot = directory + "/cache/logs";
    options.pythonRoot = directory + "/python";
    options.workerScript = QStringLiteral(":/worker.py");
}

} // namespace

bool detectRunMode(const QString &exePath, const QByteArray &manifestBytes, RunOptions *options, RunModeError *error)
{
    if (!options) return false;
    if (error) *error = {};
    if (exePath.trimmed().isEmpty()) { reject(error, "无法确定运行时程序路径"); return false; }
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(manifestBytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        reject(error, "manifest.json 不是有效的 JSON 对象");
        return false;
    }
    const auto manifest = document.object();
    if (manifest.value("format").toString() != QStringLiteral("corig-app")) {
        reject(error, "manifest.json 的 format 不是 corig-app");
        return false;
    }
    if (manifest.value("mode").toString() != QStringLiteral("publish")) {
        reject(error, "manifest.json 的 mode 不是 publish");
        return false;
    }
    if (manifest.value("format_version").toInt() != 1) {
        reject(error, "manifest.json 的 format_version 不受支持");
        return false;
    }
    // The manifest groups identity and release data; flat keys stay readable so
    // payloads produced by an earlier build keep working.
    const auto app = manifest.value("app").toObject();
    const auto release = manifest.value("release").toObject();
    const auto appId = app.value("id").toString(manifest.value("appId").toString());
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{2,63}$"));
    if (!pattern.match(appId).hasMatch() || appId.contains(QLatin1Char('/')) || appId.contains(QLatin1Char('\\'))) {
        reject(error, "manifest.json 的 appId 不合法");
        return false;
    }
    const auto runtime = manifest.value("runtime").toObject();
    if (runtime.value("executable").toString() != QStringLiteral("runtime/CoRig.exe")) {
        reject(error, "manifest.json 的 runtime.executable 不是 runtime/CoRig.exe");
        return false;
    }
    const auto payloadRoot = QDir::cleanPath(QFileInfo(exePath).absolutePath() + "/..");
    if (!samePath(exePath, payloadRoot + "/runtime/CoRig.exe")) {
        reject(error, "当前程序不在载荷的 runtime 目录中");
        return false;
    }
    RunOptions detected;
    detected.mode = RunMode::Publish;
    detected.appId = appId;
    detected.appName = app.value("name").toString(manifest.value("appName").toString(appId));
    detected.appVersion = app.value("version").toString(manifest.value("appVersion").toString());
    detected.releaseHash = release.value("snapshot_sha256").toString(manifest.value("releaseHash").toString());
    detected.retainOriginal = manifest.value("retain_original").toBool(manifest.value("retainOriginal").toBool(false));
    detected.payloadRoot = payloadRoot;
    detected.workerScript = QStringLiteral(":/worker.py");
    *options = detected;
    return true;
}

bool computePublishPaths(const QString &appRoot, RunOptions *options, RunModeError *error)
{
    if (!options || options->mode != RunMode::Publish || options->appId.isEmpty()) {
        reject(error, "尚未识别发布载荷");
        return false;
    }
    if (appRoot.trimmed().isEmpty() || !QDir::isAbsolutePath(appRoot)) {
        reject(error, "无法确定应用工作目录（应位于程序旁的 .data 目录）");
        return false;
    }
    const auto root = QDir::cleanPath(appRoot);
    RunOptions filled = *options;
    filled.dataRoot = root + "/data";
    filled.cacheRoot = root + "/cache";
    filled.tempRoot = filled.cacheRoot + "/temp/" + QString::number(currentProcessId());
    filled.pluginRoot = QDir::cleanPath(options->payloadRoot + "/plugins");
    filled.pluginCacheRoot = filled.cacheRoot + "/plugin-packages";
    filled.crashRoot = filled.dataRoot + "/diagnostics";
    filled.pythonRoot = QDir::cleanPath(options->payloadRoot + "/python");
    // Every writable root must stay inside the portable work root; a malformed
    // payload or a junction cannot redirect writes elsewhere.
    if (!isContained(root, filled.dataRoot)
        || !isContained(root, filled.cacheRoot)
        || !isContained(root, filled.pluginCacheRoot)
        || !isContained(root, filled.tempRoot)
        || !isContained(filled.dataRoot, filled.crashRoot)) {
        reject(error, "应用路径越出工作目录");
        return false;
    }
    *options = filled;
    return true;
}

bool initializeRunOptions(RunModeError *error)
{
    if (error) *error = {};
    RunOptions options;
    const auto exePath = currentExecutablePath();
    if (exePath.isEmpty()) {
        // configureRuntimePaths reports the same condition with its own message;
        // leaving editor defaults here keeps that failure path unchanged.
        g_options = options;
        return true;
    }
    const auto exeDirectory = QFileInfo(exePath).absolutePath();
    const auto manifestPath = QDir::cleanPath(exeDirectory + "/../manifest.json");
    const QFileInfo manifest(manifestPath);
    if (!manifest.exists()) {
        // An extracted payload without a manifest is broken, not an editor: falling
        // back to editor mode here would write user data into the payload cache and
        // expose editing inside a published application.
        if (looksLikeExtractedPayload(exeDirectory)) {
            reject(error, "载荷缺少 manifest.json");
            return false;
        }
        configureEditorPaths(options);
        g_options = options;
        return true;
    }
    if (!isPlainFile(manifestPath)) {
        reject(error, "manifest.json 必须是常规文件");
        return false;
    }
    if (manifest.size() > 1024 * 1024) {
        reject(error, "manifest.json 超过 1 MiB");
        return false;
    }
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        reject(error, "无法读取 manifest.json：" + manifestPath);
        return false;
    }
    const auto bytes = file.readAll();
    file.close();
    // A manifest in an unrelated parent directory (for example a build tree that
    // also holds another product's manifest) must not lock the editor out. The
    // payload is recognised by our format marker or by its project snapshot; a
    // damaged manifest that still sits next to a payload stays a hard error so a
    // published application can never fall back to the editor.
    QJsonParseError manifestParse{};
    const auto manifestDocument = QJsonDocument::fromJson(bytes,&manifestParse);
    const auto payloadProject = QFileInfo(QDir::cleanPath(exeDirectory + "/../project/runtime.crpj")).exists();
    const auto ours = payloadProject || (manifestParse.error == QJsonParseError::NoError && manifestDocument.isObject()
        && manifestDocument.object().value("format").toString() == QStringLiteral("corig-app"));
    // A directory that has the extracted-payload layout is never an editor, even
    // when both the manifest marker and the snapshot are damaged: degrading to an
    // editor would expose editing and write user data into the payload cache.
    if (!ours && looksLikeExtractedPayload(exeDirectory)) {
        reject(error, "载荷布局中的 manifest.json 不属于 CoRig");
        return false;
    }
    if (!ours) { configureEditorPaths(options); g_options = options; return true; }
    RunOptions detected;
    if (!detectRunMode(exePath, bytes, &detected, error)) return false;
    if (!computePublishPaths(applicationRoot(detected.payloadRoot, error), &detected, error)) return false;
    g_options = detected;
    return true;
}

const RunOptions &runOptions() { return g_options; }

RuntimePaths runtimePaths()
{
    const auto &options = runOptions();
    return {options.payloadRoot, options.dataRoot, options.cacheRoot, options.tempRoot,
            options.pluginRoot, options.pluginCacheRoot, options.crashRoot, options.pythonRoot};
}

} // namespace corig
