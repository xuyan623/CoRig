#include "paths.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace corig {
namespace {
// Publishing never writes into the payload: every cache and temporary directory was
// resolved inside the portable work root by initializeRunOptions. A cache that
// cannot be created only costs disk caching, so nothing here stops the application.
void configurePublishRuntimePaths(const RunOptions &options)
{
    // Per-process temporary folders are the only thing this application leaves in
    // the work-root cache besides content-addressed payloads. Sweep folders from
    // earlier runs that are old enough that no live instance can own them, so a
    // crash cannot accumulate gigabytes.
    QDir tempRoot(options.cacheRoot + "/temp");
    for (const auto &entry : tempRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo folder(tempRoot.filePath(entry));
        if (folder.lastModified().daysTo(QDateTime::currentDateTime()) > 1) QDir(folder.filePath()).removeRecursively();
    }
    // Unpacked plugin packages are content-addressed and re-creatable. Drop ones
    // no instance has touched for a month so upgrades do not accumulate copies of
    // the same 112 MB official package. A live process holds its files open, so a
    // failed removal is harmless and simply retried on a later start.
    QDir pluginCache(options.pluginCacheRoot);
    for (const auto &entry : pluginCache.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo folder(pluginCache.filePath(entry));
        if (folder.lastModified().daysTo(QDateTime::currentDateTime()) > 30) QDir(folder.filePath()).removeRecursively();
    }
    const auto qmlCache = QDir(options.cacheRoot).filePath("qml");
    const auto shaderCache = QDir(options.cacheRoot).filePath("shaders");
    QDir().mkpath(qmlCache);
    QDir().mkpath(shaderCache);
    QDir().mkpath(options.tempRoot);
    qputenv("QML_DISK_CACHE_PATH", qmlCache.toUtf8());
    qputenv("QT_SHADER_CACHE_PATH", shaderCache.toUtf8());
    qputenv("QSG_RHI_DISABLE_DISK_CACHE", "1");
    qputenv("QSG_RHI_PIPELINE_CACHE_LOAD", (shaderCache + "/pipeline.cache").toUtf8());
    qputenv("QSG_RHI_PIPELINE_CACHE_SAVE", (shaderCache + "/pipeline.cache").toUtf8());
    const auto temporary = QDir::toNativeSeparators(options.tempRoot).toUtf8();
    qputenv("TEMP", temporary); qputenv("TMP", temporary); qputenv("TMPDIR", temporary);
}
} // namespace

void configureRuntimePaths()
{
    const auto &options = runOptions();
    if (options.mode == RunMode::Publish) { configurePublishRuntimePaths(options); return; }
    QString directory;
#ifdef Q_OS_WIN
    // applicationDirPath is unavailable before QCoreApplication exists.
    wchar_t executable[32768];
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) throw Error("无法确定 CoRig 安装目录");
    directory = QFileInfo(QString::fromWCharArray(executable, int(length))).absolutePath();
#else
    directory = QCoreApplication::applicationDirPath();
#endif
    const QDir root(directory);
    for (const auto &name : {"cache/qml", "cache/shaders", "cache/temp", "cache/installer", "cache/logs"}) {
        const auto path = root.filePath(name);
        if (!QDir().mkpath(path)) throw Error("安装目录不可写，请选择可写的安装位置：" + path);
        QTemporaryFile probe(path + "/.write-XXXXXX");
        if (!probe.open()) throw Error("安装目录不可写，请选择可写的安装位置：" + path);
    }
    qputenv("QML_DISK_CACHE_PATH", root.filePath("cache/qml").toUtf8());
    qputenv("QT_SHADER_CACHE_PATH", root.filePath("cache/shaders").toUtf8());
    // Disable Qt's automatic AppData pipeline cache while explicitly keeping
    // the pipeline cache under this installation for every Quick window.
    qputenv("QSG_RHI_DISABLE_DISK_CACHE", "1");
    qputenv("QSG_RHI_PIPELINE_CACHE_LOAD", root.filePath("cache/shaders/pipeline.cache").toUtf8());
    qputenv("QSG_RHI_PIPELINE_CACHE_SAVE", root.filePath("cache/shaders/pipeline.cache").toUtf8());
    const auto temporary = QDir::toNativeSeparators(root.filePath("cache/temp")).toUtf8();
    qputenv("TEMP", temporary); qputenv("TMP", temporary); qputenv("TMPDIR", temporary);
    // IFW stores its cache setting in network.xml. Preserve repositories and
    // proxy settings, replacing only the cache path after install/relocation.
    QFile network(root.filePath("network.xml"));
    if (network.exists()) {
        if (!network.open(QIODevice::ReadOnly)) throw Error("无法读取更新配置：" + network.fileName());
        const auto bytes = network.readAll(); network.close();
        QXmlStreamReader reader(bytes);
        QByteArray output;
        QXmlStreamWriter writer(&output);
        const auto installerCache = root.filePath("cache/installer");
        bool changed = false, found = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == u"LocalCachePath") {
                const auto previous = reader.readElementText();
                writer.writeTextElement("LocalCachePath", installerCache);
                changed = previous != installerCache; found = true;
            } else {
                if (reader.isEndElement() && reader.name() == u"Network" && !found) {
                    writer.writeTextElement("LocalCachePath", installerCache); changed = true;
                }
                writer.writeCurrentToken(reader);
            }
        }
        if (reader.hasError()) throw Error("更新配置 XML 损坏：" + network.fileName());
        if (changed) {
            QSaveFile file(network.fileName());
            if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit())
                throw Error("无法写入更新缓存位置：" + network.fileName());
        }
    }
}

QString defaultDataDirectory()
{
    // Installed and portable builds both keep user data beside the executable.
    // The installer then only replaces program files, and one folder holds
    // everything a user cares about. A published application instead owns its data
    // root beside the executable, because the payload directory is read-only.
    const auto &options = runOptions();
    if (options.mode == RunMode::Publish) return options.dataRoot;
    return QCoreApplication::applicationDirPath();
}

void cleanupRuntimeTemp()
{
    const auto &options = runOptions();
    if (options.mode != RunMode::Publish || options.tempRoot.isEmpty()) return;
    QDir(options.tempRoot).removeRecursively();
}

void ensureDataDirectory(const QString &directory, RunMode mode)
{
    if (mode == RunMode::Publish) {
        // A published payload contains program files only; the installer owns them.
        // Creating project/plugins/cache there or writing an explanatory note would
        // fail on a read-only installation and would mix user data with a payload.
        // A published application keeps its work root beside the executable. A
        // read-only program folder is reported instead of silently falling back
        // to the system drive, which would contradict the portable contract.
        if (!QDir().mkpath(directory)) throw Error("程序所在目录不可写，无法创建工作目录：" + directory + "。请将程序移到可写位置后重试。");
        return;
    }
    QDir root(directory);
    if (!root.mkpath("project") || !root.mkpath("plugins") || !root.mkpath("cache"))
        throw Error("无法创建数据目录：" + directory);
    // A marker file explains the folder without pretending to be a project.
    const auto note = root.filePath("project/说明.txt");
    if (QFileInfo::exists(note)) return;
    QSaveFile file(note);
    if (!file.open(QIODevice::WriteOnly)) throw Error("无法写入工程目录说明：" + note);
    file.write(QStringLiteral(
        "CoRig 工程目录\n\n"
        "此目录保存 .crpj 单文件工程。保存工程时未指定路径，默认写入这里。\n"
        "升级 CoRig 不会删除此目录中的文件；卸载时默认也保留。\n").toUtf8());
    if (!file.commit()) throw Error("无法写入工程目录说明：" + note);
}
} // namespace corig
