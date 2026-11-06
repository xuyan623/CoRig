#include "python_bundle.h"
#include "project.h"
#include "run_mode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

// The generated resource initializer has external linkage; calling it from an
// anonymous namespace would look for a namespaced symbol that does not exist.
static void initializePythonResource() { Q_INIT_RESOURCE(python_worker); }

namespace corig {
namespace {

QVariantMap failure(const QString &code, const QString &phase, const QString &detail)
{
    return QVariantMap{{QStringLiteral("code"),code},{QStringLiteral("phase"),phase},
                       {QStringLiteral("detail"),detail}};
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()
        || !output.commit()) return false;
    return true;
}

QString cleanAbsolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// The probe runs with the payload interpreter and only reports paths; the
// assertions stay in C++ so a payload can be re-verified without the collector.
// Kept in sync with PROBE_SOURCE in python/collect.py.
const char *PROBE_SOURCE = R"PY(
import json, os, sys

payload = os.path.normcase(os.path.abspath(sys.argv[1]))
def inside(path):
    if not path:
        return None
    candidate = os.path.normcase(os.path.abspath(path))
    try:
        return os.path.commonpath([candidate, payload]) == payload
    except ValueError:
        return False

modules = json.loads(sys.argv[2])
info = {
    "executable": sys.executable,
    "prefix": sys.prefix,
    "base_prefix": sys.base_prefix,
    "version": "%d.%d.%d" % sys.version_info[:3],
    "site_packages": None,
    "path": list(sys.path),
    "modules": {},
}
for entry in sys.path:
    if entry and os.path.basename(entry).lower() == "site-packages":
        info["site_packages"] = entry
        break
for name in modules:
    record = {"ok": False}
    try:
        module = __import__(name)
        record["ok"] = True
        record["file"] = getattr(module, "__file__", None)
        if record["file"] is None:
            record["path"] = [str(item) for item in getattr(module, "__path__", []) or []]
        version = None
        try:
            from importlib.metadata import version as distribution_version
            parts = name.split(".")
            for length in range(len(parts), 0, -1):
                try:
                    version = distribution_version(".".join(parts[:length]))
                    break
                except Exception:
                    continue
        except Exception:
            version = None
        record["version"] = version
    except Exception as error:
        record["error"] = "%s: %s" % (type(error).__name__, error)
    info["modules"][name] = record
info["violations"] = sorted({
    key for key in ("executable", "prefix", "base_prefix", "site_packages")
    if inside(info[key]) is False
})
print(json.dumps(info, ensure_ascii=False))
)PY";

QVariantMap parseDocument(const QByteArray &bytes, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes,&parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.errorString();
        return {};
    }
    return fromJsonValue(document.toVariant()).toMap();
}

QString lastLine(const QByteArray &bytes)
{
    const auto lines = QString::fromUtf8(bytes).trimmed().split(QLatin1Char('\n'));
    for (auto i = lines.crbegin(); i != lines.crend(); ++i) if (!i->trimmed().isEmpty()) return i->trimmed();
    return {};
}

QString tail(const QString &text, int limit = 2000)
{
    return text.size() <= limit ? text : text.right(limit);
}

QString locateCollector(const PythonBundleRequest &request, QStringList *searched)
{
    QStringList candidates;
    if (!request.collectorScript.isEmpty()) candidates << request.collectorScript;
    const auto configured = qEnvironmentVariable("CORIG_PYTHON_COLLECT");
    if (!configured.isEmpty()) candidates << configured;
    const auto application = QCoreApplication::applicationDirPath();
    // An installed publishing tool keeps the collector beside itself; a build
    // tree finds it through the two directories above the build folder.
    candidates << application + QStringLiteral("/python/collect.py")
               << application + QStringLiteral("/../python/collect.py")
               << application + QStringLiteral("/../../python/collect.py")
               << application + QStringLiteral("/collect.py");
    for (const auto &candidate : candidates) {
        const auto path = cleanAbsolute(candidate);
        if (QFileInfo(path).isFile()) return path;
        if (searched) *searched << path;
    }
    return {};
}

bool absolutePath(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        for (auto i = map.cbegin(); i != map.cend(); ++i) if (absolutePath(i.value())) return true;
        return false;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto &item : value.toList()) if (absolutePath(item)) return true;
        return false;
    }
    if (value.metaType().id() != QMetaType::QString) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 1024) return false;
    if (QDir::isAbsolutePath(text)) return true;
    return text.size() > 2 && text.at(1) == QLatin1Char(':') && text.at(0).isLetter();
}

QVariantMap readManifest(const QString &pythonRoot)
{
    QString error;
    return parseDocument(readBytes(pythonRoot + QStringLiteral("/python-runtime.json")),&error);
}

QStringList manifestModules(const QVariantMap &manifest)
{
    QStringList modules;
    const auto declared = manifest.value(QStringLiteral("modules")).toMap();
    for (auto i = declared.cbegin(); i != declared.cend(); ++i) modules << i.key();
    if (modules.isEmpty()) {
        const auto distributions = manifest.value(QStringLiteral("distributions")).toMap();
        for (auto i = distributions.cbegin(); i != distributions.cend(); ++i) modules << i.key();
    }
    modules.sort();
    return modules;
}

} // namespace

QVariantList pythonDeclarations(const QList<PluginPackage> &packages)
{
    QVariantList declarations;
    for (const auto &package : packages) {
        for (const auto &entry : package.definitions()) {
            const auto spec = entry.toMap();
            const auto python = spec.value(QStringLiteral("python")).toMap();
            if (python.isEmpty()) continue;
            declarations.append(QVariantMap{{QStringLiteral("package"),spec.value(QStringLiteral("package_id"))},
                                            {QStringLiteral("id"),spec.value(QStringLiteral("id"))},
                                            {QStringLiteral("python"),python}});
        }
    }
    return declarations;
}

QVariantList pythonDeclarations(const QString &projectPath)
{
    const auto project = loadProject(projectPath);
    QList<PluginPackage> packages;
    for (const auto &entry : project.value(QStringLiteral("plugin_packages")).toList()) {
        const auto bytes = entry.toMap().value(QStringLiteral("data")).toByteArray();
        if (!bytes.isEmpty()) packages.append(PluginPackage::read(bytes));
    }
    return pythonDeclarations(packages);
}

PythonBundleResult collectPython(const PythonBundleRequest &request)
{
    static const bool initialized = [] { initializePythonResource(); return true; }();
    Q_UNUSED(initialized);
    PythonBundleResult result;
    const auto phase = QStringLiteral("collect");
    try {
        if (request.developerInterpreter.isEmpty() || !QFileInfo(request.developerInterpreter).isFile()) {
            result.error = failure(QStringLiteral("PY_INTERPRETER"),phase,
                QStringLiteral("请提供用于收集的本地 python.exe：") + request.developerInterpreter);
            return result;
        }
        if (request.payloadRoot.isEmpty()) {
            result.error = failure(QStringLiteral("PY_LAYOUT"),phase,QStringLiteral("发布载荷目录为空"));
            return result;
        }
        const auto payloadRoot = cleanAbsolute(request.payloadRoot);
        const auto pythonRoot = payloadRoot + QStringLiteral("/python");
        // The publishing binary owns the worker script version, so it is written
        // before the collector runs; the collector only verifies its presence.
        QFile workerScript(QStringLiteral(":/worker.py"));
        if (!workerScript.open(QIODevice::ReadOnly)) {
            result.error = failure(QStringLiteral("PY_WORKER_MISSING"),phase,
                                   QStringLiteral("发布程序缺少内置的 worker.py 资源"));
            return result;
        }
        if (!writeBytes(pythonRoot + QStringLiteral("/worker.py"), workerScript.readAll())) {
            result.error = failure(QStringLiteral("PY_LAYOUT"),phase,
                                   QStringLiteral("无法写入载荷工作进程脚本：") + pythonRoot);
            return result;
        }
        auto plugins = request.plugins;
        if (plugins.isEmpty() && !request.projectPath.isEmpty())
            plugins = pythonDeclarations(request.projectPath);
        QStringList searched;
        const auto collector = locateCollector(request,&searched);
        if (collector.isEmpty()) {
            result.error = failure(QStringLiteral("PY_COLLECTOR_MISSING"),phase,
                QStringLiteral("找不到 python/collect.py；已查找：") + searched.join(QStringLiteral("、")));
            return result;
        }
        const QVariantMap body{{QStringLiteral("payload_root"),payloadRoot},
                               {QStringLiteral("worker_source"),pythonRoot + QStringLiteral("/worker.py")},
                               {QStringLiteral("plugins"),plugins}};
        QProcess process;
        auto variables = QProcessEnvironment::systemEnvironment();
        variables.remove(QStringLiteral("PYTHONHOME"));
        variables.remove(QStringLiteral("PYTHONPATH"));
        variables.remove(QStringLiteral("PYTHONSTARTUP"));
        process.setProcessEnvironment(variables);
        process.setWorkingDirectory(payloadRoot);
        process.setProgram(request.developerInterpreter);
        // -E mirrors the developer's real import path: PYTHON* variables stay out,
        // while the per-user site-packages directory (pip install --user) is still
        // visible, so a closure split between the interpreter and %APPDATA% collects
        // instead of failing as unresolved. The payload worker below keeps -I and
        // imports only from the payload; that isolation is what the runtime promises.
        process.setArguments({QStringLiteral("-E"),QStringLiteral("-B"),QStringLiteral("-X"),
                              QStringLiteral("utf8"),collector});
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start();
        if (!process.waitForStarted(10000)) {
            result.error = failure(QStringLiteral("PY_COLLECT_FAILED"),phase,
                                   QStringLiteral("无法启动采集解释器：") + process.errorString());
            return result;
        }
        process.write(QJsonDocument::fromVariant(toJsonValue(body)).toJson(QJsonDocument::Compact) + '\n');
        process.closeWriteChannel();
        const auto timeout = std::clamp(request.timeoutMs,1000,3600000);
        if (!process.waitForFinished(timeout)) {
            process.kill(); process.waitForFinished(3000);
            result.error = failure(QStringLiteral("PY_COLLECT_FAILED"),phase,
                                   QStringLiteral("采集超过 %1 ms 未完成").arg(timeout));
            return result;
        }
        const auto output = lastLine(process.readAllStandardOutput());
        const auto errors = QString::fromUtf8(process.readAllStandardError());
        QString parseError;
        const auto document = parseDocument(output.toUtf8(),&parseError);
        if (document.isEmpty()) {
            result.error = failure(QStringLiteral("PY_COLLECT_FAILED"),phase,
                QStringLiteral("采集输出无法解析（%1）；退出码 %2；%3")
                    .arg(parseError).arg(process.exitCode()).arg(tail(errors)));
            return result;
        }
        if (!document.value(QStringLiteral("ok")).toBool()) {
            result.error = document;
            result.error[QStringLiteral("phase")] = phase;
            if (!result.error.contains(QStringLiteral("detail")))
                result.error[QStringLiteral("detail")] = tail(errors);
            return result;
        }
        result.ok = true;
        result.summary = document;
        result.pythonRoot = pythonRoot;
        result.warnings = document.value(QStringLiteral("warnings")).toStringList();
    } catch (const std::exception &error) {
        result.error = failure(QStringLiteral("PY_COLLECT_FAILED"),phase,QString::fromUtf8(error.what()));
    }
    return result;
}

PythonProbeResult probePythonBundle(const QString &payloadRoot, const QStringList &modules)
{
    PythonProbeResult result;
    const auto root = cleanAbsolute(payloadRoot);
    const auto pythonRoot = root + QStringLiteral("/python");
    const auto executable = pythonRoot + QStringLiteral("/python.exe");
    if (!QFileInfo(executable).isFile()) {
        result.error = failure(QStringLiteral("PY_PROBE_FAILED"),QStringLiteral("verify"),
                               QStringLiteral("载荷缺少解释器：") + executable);
        return result;
    }
    auto wanted = modules;
    if (wanted.isEmpty()) wanted = manifestModules(readManifest(pythonRoot));
    QJsonArray encoded;
    for (const auto &name : wanted) encoded.append(name);
    const auto moduleJson = QString::fromUtf8(QJsonDocument(encoded).toJson(QJsonDocument::Compact));
    QProcess process;
    auto variables = QProcessEnvironment::systemEnvironment();
    variables.remove(QStringLiteral("PYTHONHOME"));
    variables.remove(QStringLiteral("PYTHONPATH"));
    variables.remove(QStringLiteral("PYTHONSTARTUP"));
    process.setProcessEnvironment(variables);
    process.setWorkingDirectory(pythonRoot);
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-I"),QStringLiteral("-B"),QStringLiteral("-X"),
                          QStringLiteral("utf8"),QStringLiteral("-c"),
                          QString::fromLatin1(PROBE_SOURCE),pythonRoot,moduleJson});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(10000) || !process.waitForFinished(300000)) {
        process.kill(); process.waitForFinished(3000);
        result.error = failure(QStringLiteral("PY_PROBE_FAILED"),QStringLiteral("verify"),
            QStringLiteral("载荷解释器无法运行自检：") + process.errorString() + QStringLiteral("；")
                + tail(QString::fromUtf8(process.readAllStandardError())));
        return result;
    }
    QString parseError;
    result.info = parseDocument(lastLine(process.readAllStandardOutput()).toUtf8(),&parseError);
    if (result.info.isEmpty()) {
        result.error = failure(QStringLiteral("PY_PROBE_FAILED"),QStringLiteral("verify"),
            QStringLiteral("自检输出无法解析（%1）：%2").arg(parseError)
                .arg(tail(QString::fromUtf8(process.readAllStandardError()))));
        return result;
    }
    QStringList violations;
    const auto requireInside = [&](const QString &key) {
        const auto value = result.info.value(key).toString();
        if (value.isEmpty()) violations << QStringLiteral("%1 未报告").arg(key);
        else if (!pythonPathInside(value,root)) violations << QStringLiteral("%1 在载荷之外：%2").arg(key,value);
    };
    requireInside(QStringLiteral("executable"));
    requireInside(QStringLiteral("prefix"));
    requireInside(QStringLiteral("base_prefix"));
    requireInside(QStringLiteral("site_packages"));
    const auto reported = QDir::cleanPath(result.info.value(QStringLiteral("executable")).toString());
    if (!reported.isEmpty() && reported.compare(executable,Qt::CaseInsensitive) != 0)
        violations << QStringLiteral("实际解释器不是载荷内的 python.exe：") + reported;
    for (const auto &item : result.info.value(QStringLiteral("violations")).toStringList())
        violations << QStringLiteral("%1 在载荷之外").arg(item);
    const auto found = result.info.value(QStringLiteral("modules")).toMap();
    for (auto i = found.cbegin(); i != found.cend(); ++i) {
        const auto record = i.value().toMap();
        if (!record.value(QStringLiteral("ok")).toBool()) {
            violations << QStringLiteral("模块 %1 无法导入：%2").arg(i.key(),record.value(QStringLiteral("error")).toString());
            continue;
        }
        const auto file = record.value(QStringLiteral("file")).toString();
        if (!file.isEmpty()) {
            if (!pythonPathInside(file,root)) violations << QStringLiteral("模块 %1 来自载荷之外：%2").arg(i.key(),file);
            continue;
        }
        for (const auto &entry : record.value(QStringLiteral("path")).toStringList())
            if (!pythonPathInside(entry,root)) violations << QStringLiteral("模块 %1 的搜索路径在载荷之外：%2").arg(i.key(),entry);
    }
    violations.removeDuplicates();
    result.ok = violations.isEmpty();
    if (!result.ok) {
        result.error = failure(QStringLiteral("PY_PROBE_FAILED"),QStringLiteral("verify"),
                               QStringLiteral("载荷自检未通过：") + violations.join(QStringLiteral("；")));
        result.error[QStringLiteral("violations")] = violations;
    }
    return result;
}

QVariantMap verifyPythonBundle(const QString &payloadRoot)
{
    const auto root = cleanAbsolute(payloadRoot);
    const auto pythonRoot = root + QStringLiteral("/python");
    QVariantList checks;
    bool ok = true;
    const auto check = [&](const QString &name, bool passed, const QString &detail = QString()) {
        checks.append(QVariantMap{{QStringLiteral("name"),name},{QStringLiteral("ok"),passed},
                                  {QStringLiteral("detail"),detail}});
        if (!passed) ok = false;
    };
    check(QStringLiteral("python-executable"),QFileInfo(pythonRoot + QStringLiteral("/python.exe")).isFile(),
          pythonRoot + QStringLiteral("/python.exe"));
    check(QStringLiteral("python-worker"),QFileInfo(pythonRoot + QStringLiteral("/worker.py")).isFile(),
          pythonRoot + QStringLiteral("/worker.py"));
    check(QStringLiteral("python-site-packages"),QFileInfo(pythonRoot + QStringLiteral("/Lib/site-packages")).isDir(),
          pythonRoot + QStringLiteral("/Lib/site-packages"));
    const auto manifest = readManifest(pythonRoot);
    const auto valid = !manifest.isEmpty()
        && manifest.value(QStringLiteral("format")) == QStringLiteral("corig-python-runtime")
        && manifest.value(QStringLiteral("format_version")).toInt() == 1
        && manifest.value(QStringLiteral("executable")) == QStringLiteral("python.exe")
        && manifest.value(QStringLiteral("worker")) == QStringLiteral("worker.py")
        && manifest.value(QStringLiteral("site_packages")) == QStringLiteral("Lib/site-packages");
    check(QStringLiteral("python-manifest"),valid,
          manifest.isEmpty() ? QStringLiteral("缺少或损坏 python-runtime.json") : QString());
    check(QStringLiteral("python-manifest-relative"),!absolutePath(manifest),
          QStringLiteral("python-runtime.json 包含绝对路径"));
    const auto probe = probePythonBundle(root);
    check(QStringLiteral("python-probe"),probe.ok,probe.error.value(QStringLiteral("detail")).toString());
    QVariantMap outcome{{QStringLiteral("ok"),ok},{QStringLiteral("checks"),checks},
                        {QStringLiteral("python"),manifest.value(QStringLiteral("python"))},
                        {QStringLiteral("distributions"),manifest.value(QStringLiteral("distributions"))}};
    if (!probe.ok) outcome[QStringLiteral("error")] = probe.error;
    return outcome;
}

PythonEnvironment pythonEnvironmentForRun()
{
    const auto &options = runOptions();
    if (options.mode != RunMode::Publish) return {};
    PythonEnvironment environment;
    environment.payloadRoot = options.payloadRoot;
    environment.interpreter = options.pythonRoot + QStringLiteral("/python.exe");
    const auto worker = options.pythonRoot + QStringLiteral("/worker.py");
    // verifyPythonBundle rejects a payload without the script; the fallback
    // keeps a hand-assembled payload usable with the compiled worker instead.
    if (QFileInfo(worker).isFile()) environment.workerScript = worker;
    environment.workingDirectory = options.tempRoot;
    environment.isolated = true;
    return environment;
}

} // namespace corig
