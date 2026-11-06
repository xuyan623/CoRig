#include "controller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QLibraryInfo>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace corig {
namespace {
QString safeFileStem(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) name.chop(1);
    return name.isEmpty() ? QStringLiteral("app") : name;
}

QString tailText(const QByteArray &bytes, int limit = 1200)
{
    const auto text = QString::fromUtf8(bytes).trimmed();
    return text.size() <= limit ? text : text.right(limit);
}

bool removeTree(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) return true;
    if (info.isDir() && !info.isSymLink()) return QDir(path).removeRecursively();
    return QFile::remove(path);
}
}

QString Controller::softwareExportDirectory() const
{
    return settings_ ? settings_->value(QStringLiteral("publish/export_directory")).toString() : QString();
}

void Controller::setSoftwareExportDirectory(const QString &directory)
{
    if (!settings_) return;
    settings_->setValue(QStringLiteral("publish/export_directory"),directory);
    settings_->sync();
    emit preferencesChanged();
}

QVariant Controller::settingsValue(const QString &key, const QVariant &fallback) const
{
    return settings_ ? settings_->value(key, fallback) : fallback;
}

void Controller::setSettingsValue(const QString &key, const QVariant &value)
{
    if (!settings_) return;
    settings_->setValue(key, value);
    settings_->sync();
}

QVariantMap Controller::startSoftwareExport(const QVariantMap &arguments)
{
    if (softwareExportProcess_) return QVariantMap{{"error",QStringLiteral("已有导出任务正在进行")}};
    const auto project = arguments.value(QStringLiteral("project")).toString().trimmed().isEmpty()
        ? projectPath_ : arguments.value(QStringLiteral("project")).toString().trimmed();
    const auto directory = arguments.value(QStringLiteral("directory")).toString().trimmed();
    const auto appName = arguments.value(QStringLiteral("name")).toString().trimmed();
    const auto appVersion = arguments.value(QStringLiteral("version")).toString().trimmed();
    const auto appId = arguments.value(QStringLiteral("app_id")).toString().trimmed();
    const bool retain = arguments.value(QStringLiteral("retain_original")).toBool();
    const bool overwrite = arguments.value(QStringLiteral("overwrite")).toBool();
    if (project.isEmpty() || !QFileInfo(project).isFile()) return QVariantMap{{"error",QStringLiteral("请先保存当前工程")}};
    if (directory.isEmpty()) return QVariantMap{{"error",QStringLiteral("请选择导出目录")}};
    if (appName.isEmpty()) return QVariantMap{{"error",QStringLiteral("应用名称不能为空")}};
    if (appVersion.isEmpty()) return QVariantMap{{"error",QStringLiteral("应用版本不能为空")}};
    if (!appId.isEmpty() && !QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9._-]{2,63}$")).match(appId).hasMatch())
        return QVariantMap{{"error",QStringLiteral("应用标识只能使用小写字母、数字、点、下划线和短横线，3–64 个字符")}};
    const auto python = arguments.value(QStringLiteral("python")).toString().trimmed();
    if (projectNeedsPython() && python.isEmpty())
        return QVariantMap{{"error",QStringLiteral("当前工程包含 Python 功能，请先在设置 → Python 选择解释器")}};
    if (!QDir().mkpath(directory)) return QVariantMap{{"error",QStringLiteral("无法创建导出目录：") + directory}};
    const auto appDir = QCoreApplication::applicationDirPath();
    const auto publisher = QDir(appDir).filePath(QStringLiteral("CoRig-publish.exe"));
    if (!QFileInfo(publisher).isFile()) return QVariantMap{{"error",QStringLiteral("安装不完整：找不到 CoRig-publish.exe")}};
    const auto launcher = QDir(appDir).filePath(QStringLiteral("CoRig-launcher.exe"));
    if (!QFileInfo(launcher).isFile()) return QVariantMap{{"error",QStringLiteral("安装不完整：找不到 CoRig-launcher.exe")}};
    QString deployDir = appDir;
    if (!QFileInfo(QDir(deployDir).filePath(QStringLiteral("windeployqt.exe"))).isFile()) {
        const auto fallback = QDir(QLibraryInfo::path(QLibraryInfo::BinariesPath)).filePath(QStringLiteral("windeployqt.exe"));
        if (QFileInfo(fallback).isFile()) deployDir = QFileInfo(fallback).absolutePath();
    }
    if (!QFileInfo(QDir(deployDir).filePath(QStringLiteral("windeployqt.exe"))).isFile())
        return QVariantMap{{"error",QStringLiteral("安装不完整：找不到 windeployqt.exe")}};
    QString qmlDir = QDir(appDir).filePath(QStringLiteral("publish-qml"));
    if (!QFileInfo(qmlDir).isDir()) qmlDir = QDir(appDir).filePath(QStringLiteral("qml"));
    if (!QFileInfo(qmlDir).isDir()) {
        const auto source = QDir(appDir).filePath(QStringLiteral("../../src/qml"));
        if (QFileInfo(source).isDir()) qmlDir = QFileInfo(source).absoluteFilePath();
    }
    if (!QFileInfo(qmlDir).isDir()) return QVariantMap{{"error",QStringLiteral("安装不完整：找不到 qml 目录")}};
    const auto stem = safeFileStem(appName);
    const auto base = QDir(directory).filePath(stem);
    if (QFileInfo::exists(base + QStringLiteral(".exe")) && !overwrite)
        return QVariantMap{{"error",QStringLiteral("目标文件已存在：") + base + QStringLiteral(".exe")},
                           {"exists",true},{"target",base + QStringLiteral(".exe")}};
    const auto logs = QDir(dataDirectory_).filePath(QStringLiteral("cache/logs"));
    QDir().mkpath(logs);
    softwareExportLog_ = QDir(logs).filePath(QStringLiteral("software-export-") +
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".log"));
    softwareExportBase_ = base;
    softwareExportStdout_.clear();
    softwareExportCancelling_ = false;
    const auto jobId = newId("job");
    softwareExportJobId_ = jobId;
    jobs_[jobId] = QVariantMap{{"id",jobId},{"op","publish.start"},{"state","running"},
        {"stage","prepare"},{"progress",5},{"path",base},{"log",softwareExportLog_}};
    softwareExport_ = QVariantMap{
        {QStringLiteral("job"),jobId},
        {QStringLiteral("state"),QStringLiteral("running")},
        {QStringLiteral("stage"),QStringLiteral("prepare")},
        {QStringLiteral("progress"),5},
        {QStringLiteral("path"),QString()},
        {QStringLiteral("error"),QString()},
        {QStringLiteral("log"),softwareExportLog_},
        {QStringLiteral("artifact"),QString()},
        {QStringLiteral("bytes"),0},
        {QStringLiteral("cancelling"),false}
    };
    emit softwareExportChanged();

    QStringList args{
        QStringLiteral("publish"),
        QStringLiteral("--project"),project,
        QStringLiteral("--output"),base,
        QStringLiteral("--name"),appName,
        QStringLiteral("--version"),appVersion,
        QStringLiteral("--single-exe"),
        QStringLiteral("--qt-prefix"),deployDir,
        QStringLiteral("--qml-dir"),qmlDir,
        QStringLiteral("--launcher"),launcher,
        QStringLiteral("--progress")
    };
    if (overwrite) args << QStringLiteral("--overwrite");
    if (retain) args << QStringLiteral("--retain-original");
    if (!appId.isEmpty()) args << QStringLiteral("--app-id") << appId;
    if (!python.isEmpty()) args << QStringLiteral("--python-exe") << python;

    QFile log(softwareExportLog_);
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        log.write(QStringLiteral("CoRig software export\nproject: %1\noutput: %2\n\n")
                      .arg(project,base).toUtf8());
        log.close();
    }
    auto *process = new QProcess(this);
    softwareExportProcess_ = process;
    process->setWorkingDirectory(QFileInfo(project).absolutePath());
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const auto vcDir = QDir(appDir).filePath(QStringLiteral("tools/vc"));
    if (QFileInfo(vcDir).isDir()) env.insert(QStringLiteral("VCToolsRedistDir"),vcDir);
    env.insert(QStringLiteral("PATH"),appDir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
    process->setProcessEnvironment(env);
    process->setProgram(publisher);
    process->setArguments(args);

    connect(process,&QProcess::started,this,[this,process,appDir] {
#ifdef Q_OS_WIN
        const auto job = CreateJobObjectW(nullptr,nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
            const auto handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE,FALSE,
                                            static_cast<DWORD>(process->processId()));
            if (handle) {
                AssignProcessToJobObject(job,handle);
                CloseHandle(handle);
            }
            softwareExportJob_ = job;
        }
#endif
    });
    const auto appendLog = [this](const QByteArray &bytes) {
        QFile file(softwareExportLog_);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) file.write(bytes);
    };
    connect(process,&QProcess::readyReadStandardError,this,[this,process,appendLog] {
        const auto bytes = process->readAllStandardError();
        appendLog(bytes);
        for (const auto &line : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
            const auto trimmed = line.trimmed();
            if (!trimmed.startsWith(QStringLiteral("CORIG_PROGRESS "))) continue;
            const auto parts = trimmed.split(QLatin1Char(' '),Qt::SkipEmptyParts);
            if (parts.size() < 3) continue;
            softwareExport_[QStringLiteral("stage")] = parts[1];
            softwareExport_[QStringLiteral("progress")] = parts[2].toInt();
            if (!softwareExportJobId_.isEmpty() && jobs_.contains(softwareExportJobId_)) {
                jobs_[softwareExportJobId_][QStringLiteral("stage")] = parts[1];
                jobs_[softwareExportJobId_][QStringLiteral("progress")] = parts[2].toInt();
            }
            emit softwareExportChanged();
        }
    });
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,appendLog] {
        const auto bytes = process->readAllStandardOutput();
        softwareExportStdout_.append(bytes);
        appendLog(bytes);
    });
    connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
            [this,process](int code,QProcess::ExitStatus status) {
        appendExportFinished(code,status);
    });
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && softwareExportProcess_ == process)
            finishSoftwareExport(false,QStringLiteral("无法启动发布器"),QString());
    });
    process->start();
    return QVariantMap{{"started",true},{"job",jobId},{"state","running"},{"log",softwareExportLog_}};
}

void Controller::appendExportFinished(int code,QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    auto *process = softwareExportProcess_;
    if (!process) return;
    softwareExportStdout_.append(process->readAllStandardOutput());
    const auto out = softwareExportStdout_;
    QFile log(softwareExportLog_);
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) log.write(out);
    const bool cancelled = softwareExportCancelling_;
    QString detail;
    if (!cancelled) {
        QJsonParseError parse{};
        const auto document = QJsonDocument::fromJson(out,&parse);
        if (parse.error == QJsonParseError::NoError && document.isObject()) {
            const auto object = document.object();
            if (object.value(QStringLiteral("ok")).toBool()) {
                const auto artifact = object.value(QStringLiteral("single_exe")).toString();
                const auto path = artifact.isEmpty() ? softwareExportBase_ + QStringLiteral(".exe") : artifact;
                const auto bytes = object.value(QStringLiteral("single_exe_bytes")).toInteger();
                softwareExportProcess_ = nullptr;
                process->deleteLater();
                closeSoftwareExportJob();
                softwareExport_[QStringLiteral("state")] = QStringLiteral("success");
                softwareExport_[QStringLiteral("stage")] = QStringLiteral("done");
                softwareExport_[QStringLiteral("progress")] = 100;
                softwareExport_[QStringLiteral("artifact")] = path;
                softwareExport_[QStringLiteral("path")] = path;
                softwareExport_[QStringLiteral("bytes")] = bytes;
                softwareExport_[QStringLiteral("error")] = QString();
                if (!softwareExportJobId_.isEmpty()) finishJob(softwareExportJobId_,softwareExport_,{});
                emit softwareExportChanged();
                return;
            }
            const auto error = object.value(QStringLiteral("error")).toObject();
            detail = error.value(QStringLiteral("detail")).toString();
            if (detail.isEmpty()) detail = error.value(QStringLiteral("code")).toString();
        } else {
            detail = QString::fromUtf8(out).trimmed();
        }
        if (detail.isEmpty()) detail = QStringLiteral("发布器退出码 %1").arg(code);
    }
    finishSoftwareExport(cancelled,QString(),detail);
}

void Controller::finishSoftwareExport(bool cancelled,const QString &error,const QString &detail)
{
    if (softwareExportProcess_) {
        softwareExportProcess_->deleteLater();
        softwareExportProcess_ = nullptr;
    }
    closeSoftwareExportJob();
    const auto directory = QFileInfo(softwareExportBase_).absolutePath();
    const auto stem = QFileInfo(softwareExportBase_).fileName();
    if (!softwareExportBase_.isEmpty()) {
        QDir dir(directory);
        for (const auto &entry : dir.entryList({stem + QStringLiteral(".part-*"),
                                               stem + QStringLiteral(".exe.part-*")},
                                              QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
            removeTree(dir.filePath(entry));
    }
    softwareExport_[QStringLiteral("state")] = cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
    softwareExport_[QStringLiteral("error")] = cancelled ? QString() : (detail.isEmpty() ? error : detail);
    softwareExport_[QStringLiteral("stage")] = QStringLiteral("done");
    softwareExport_[QStringLiteral("cancelling")] = false;
    if (!softwareExportJobId_.isEmpty()) {
        if (cancelled) {
            jobs_[softwareExportJobId_][QStringLiteral("state")] = QStringLiteral("cancelled");
            jobs_[softwareExportJobId_][QStringLiteral("error")] = QStringLiteral("发布已取消");
            jobs_[softwareExportJobId_][QStringLiteral("result")] = softwareExport_;
            emit jobFinished(softwareExportJobId_);
        } else {
            finishJob(softwareExportJobId_,softwareExport_,softwareExport_.value(QStringLiteral("error")).toString());
        }
    }
    emit softwareExportChanged();
}

void Controller::closeSoftwareExportJob()
{
#ifdef Q_OS_WIN
    if (softwareExportJob_) {
        CloseHandle(static_cast<HANDLE>(softwareExportJob_));
        softwareExportJob_ = nullptr;
    }
#endif
}

void Controller::cancelSoftwareExport()
{
    if (!softwareExportProcess_) return;
    softwareExportCancelling_ = true;
    softwareExport_[QStringLiteral("state")] = QStringLiteral("cancelling");
    softwareExport_[QStringLiteral("cancelling")] = true;
    emit softwareExportChanged();
#ifdef Q_OS_WIN
    if (softwareExportJob_) TerminateJobObject(static_cast<HANDLE>(softwareExportJob_),1);
#endif
    softwareExportProcess_->kill();
}

QVariantMap Controller::publishLog() const
{
    if (softwareExportLog_.isEmpty() || !QFileInfo(softwareExportLog_).isFile())
        throw Error("发布日志尚不存在");
    QFile file(softwareExportLog_);
    if (!file.open(QIODevice::ReadOnly)) throw Error("无法读取发布日志");
    const auto bytes = file.readAll();
    constexpr qsizetype limit = 2 * 1024 * 1024;
    const auto text = QString::fromUtf8(bytes.left(limit));
    return QVariantMap{{"path",softwareExportLog_},{"text",text},{"bytes",bytes.size()},
                       {"truncated",bytes.size() > limit}};
}

void Controller::openSoftwareExportLog()
{
    if (!softwareExportLog_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(softwareExportLog_));
}

bool Controller::projectNeedsPython() const
{
    for (const auto &entry : project_.value(QStringLiteral("nodes")).toList()) {
        const auto plugin = entry.toMap().value(QStringLiteral("plugin")).toString();
        if (plugin == QStringLiteral("function.python") || plugin == QStringLiteral("protocol.python")) return true;
        const auto definition = catalog_.definition(plugin);
        if (definition.value(QStringLiteral("implementation")).toString() == QStringLiteral("python")) return true;
    }
    return false;
}

} // namespace corig
