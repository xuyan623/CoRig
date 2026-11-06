// The desktop executable has no Python startup dependency. User extensions are
// started lazily by the graph worker only after a local interpreter is selected.
#include "controller.h"
#include "quick/renderers.h"
#include "quick/icon_provider.h"
#include "core/paths.h"
#include "core/run_mode.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <QProcess>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

static QString logPath;
static QMutex logMutex;
static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    QMutexLocker lock(&logMutex);
    const auto text = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " [" + QString::number(type) + "] " + message + '\n';
    if (!logPath.isEmpty()) { QFile file(logPath); const auto mode = QIODevice::WriteOnly | (file.size() > 4 * 1024 * 1024 ? QIODevice::Truncate : QIODevice::Append); if (file.open(mode)) file.write(text.toUtf8()); }
    const auto bytes = text.toLocal8Bit(); std::fwrite(bytes.constData(),1,bytes.size(),stderr);
    if (type == QtFatalMsg) std::abort();
}

namespace {

// Resolved before QGuiApplication exists, so a crash during startup still lands
// in the directory owned by the current mode (the editor keeps <exe>/cache/logs).
QString crashDirectory()
{
    const auto &options = corig::runOptions();
    return options.crashRoot.isEmpty() ? QCoreApplication::applicationDirPath() + "/cache/logs" : options.crashRoot;
}

QString crashPath(const QString &suffix)
{
    QDir().mkpath(crashDirectory());
    return crashDirectory() + "/CoRig-crash-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + suffix;
}

// The crash filter must write evidence itself: the Qt message handler shares a
// mutex that the crashing thread may already hold, so it only touches a file.
void appendCrashRecord(const QString &kind, const QString &detail)
{
    QMutexLocker lock(&logMutex);
    const auto line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " [" + kind + "] " + detail + '\n';
    QFile file(crashPath(".log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) { file.write(line.toUtf8()); file.flush(); }
}

#ifdef Q_OS_WIN
QString addressOwner(quintptr address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address),&module) || !module) return {};
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module,path,MAX_PATH);
    if (!length) return {};
    return QString::fromWCharArray(path,int(length)) + "+0x" + QString::number(address - reinterpret_cast<quintptr>(module),16);
}

// dbghelp is loaded on demand so a machine without it still reports the fault.
QString writeMiniDump(EXCEPTION_POINTERS *information)
{
    using WriteDump = BOOL (WINAPI *)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,const PMINIDUMP_EXCEPTION_INFORMATION,const PMINIDUMP_USER_STREAM_INFORMATION,const PMINIDUMP_CALLBACK_INFORMATION);
    const auto library = LoadLibraryW(L"dbghelp.dll"); if (!library) return {};
    const auto write = reinterpret_cast<WriteDump>(GetProcAddress(library,"MiniDumpWriteDump"));
    if (!write) return {};
    const auto path = crashPath(".dmp");
    const auto native = QDir::toNativeSeparators(path);
    const auto file = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(),information,FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules);
    const auto written = write(GetCurrentProcess(),GetCurrentProcessId(),file,type,&exception,nullptr,nullptr);
    CloseHandle(file);
    return written ? path : QString();
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS *information)
{
    const auto record = information ? information->ExceptionRecord : nullptr;
    const auto address = record ? reinterpret_cast<quintptr>(record->ExceptionAddress) : 0;
    const auto code = record ? record->ExceptionCode : 0;
    auto detail = QString("未处理异常 0x%1，地址 0x%2").arg(code,8,16,QChar('0')).arg(address,0,16);
    const auto owner = addressOwner(address); if (!owner.isEmpty()) detail += " (" + owner + ")";
    const auto dump = writeMiniDump(information); if (!dump.isEmpty()) detail += "\n转储文件：" + dump;
    appendCrashRecord("crash",detail);
    const auto location = corig::runOptions().mode == corig::RunMode::Publish
        ? QStringLiteral("应用诊断目录") : QStringLiteral("程序目录的 cache/logs");
    const auto message = detail + "\n\n诊断信息已写入" + location + "。";
    MessageBoxW(nullptr,reinterpret_cast<LPCWSTR>(message.utf16()),L"CoRig 遇到严重错误",MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// An escaping exception used to abort through the runtime with no message at
// all; record it before the standard abort so the log names the failure.
void installCrashHandlers()
{
    std::set_terminate([] {
        QString detail = "std::terminate";
        if (const auto current = std::current_exception()) {
            try { std::rethrow_exception(current); }
            catch (const std::exception &error) { detail += "：" + QString::fromUtf8(error.what()); }
            catch (...) { detail += "：未知异常"; }
        }
        appendCrashRecord("terminate",detail);
        std::abort();
    });
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(crashFilter);
#endif
}

} // namespace

int main(int argc, char **argv)
{
    // The mode is decided from the payload manifest, never from argv, and it must be
    // known before any path-sensitive work starts. A payload that claims to be a
    // CoRig application but fails validation stops startup instead of silently
    // degrading into an editor session.
    corig::RunModeError modeError;
    if (!corig::initializeRunOptions(&modeError)) {
        const auto message = QStringLiteral("应用载荷不完整（%1）：%2").arg(modeError.code, modeError.message);
#ifdef Q_OS_WIN
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"CoRig 无法启动", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
#endif
        return 1;
    }
    try { corig::configureRuntimePaths(); }
    catch (const std::exception &error) {
#ifdef Q_OS_WIN
        const auto message = QString::fromUtf8(error.what());
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"CoRig 无法启动", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "%s\n", error.what());
#endif
        return 1;
    }
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication application(argc,argv); QCoreApplication::setOrganizationName("CoRig"); QCoreApplication::setApplicationName("CoRig"); QCoreApplication::setApplicationVersion("0.4.4");
    application.setApplicationDisplayName(QStringLiteral("联调 · CoRig"));
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.setApplicationDescription("联调 · CoRig 工程调试工作台"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir","工程、设置和本地连接信息的目录","directory",corig::defaultDataDirectory()});
    parser.addOption({"plugin-selections","重启后恢复本次工程的插件版本选择","base64-json"});
    parser.addOption({"update-installer","打开此安装目录的更新工具"});
    parser.addOption({"configure-installer","配置此安装目录的更新缓存后退出"});
    parser.addPositionalArgument("project","启动后打开的新版工程文件（不会自动运行）"); parser.process(application);
    // Maintenance flags belong to the editor installation; a published payload
    // has no installer and must ignore them.
    const auto publishing = corig::runOptions().mode == corig::RunMode::Publish;
    if (!publishing && parser.isSet("configure-installer")) return 0;
    if (!publishing && parser.isSet("update-installer")) {
        const auto root = QCoreApplication::applicationDirPath();
        const auto tool = root + "/CoRigMaintenanceTool.exe";
        return QFileInfo::exists(tool) && QProcess::startDetached(tool,
            {"--cache-path", root + "/cache/installer", "--start-updater"}) ? 0 : 1;
    }
    const auto &options = corig::runOptions();
    // A published application always uses its own data root: --data-dir stays an
    // editor and debugging option, so a shortcut cannot redirect user data.
    const auto directory = options.mode == corig::RunMode::Publish
        ? options.dataRoot : QDir(parser.value("data-dir")).absolutePath();
    try {
        // Data lives with the program; create the three user-facing folders once so
        // an empty project folder is explained instead of silently missing.
        corig::ensureDataDirectory(directory, options.mode);
        // A published application writes no file log: the payload is read-only and
        // diagnostics belong to its data root. Stderr output is kept in both modes.
        if (options.mode == corig::RunMode::Editor) logPath = QCoreApplication::applicationDirPath() + "/cache/logs/native.log";
        qInstallMessageHandler(messageHandler);
        installCrashHandlers();
        // Startup breadcrumbs name the stage that was reached last, so a process
        // that dies before the window appears still tells where it stopped.
        qInfo("启动：数据目录 %s",qUtf8Printable(directory));
        QQuickStyle::setStyle("Basic");
        application.setFont(QFont("Microsoft YaHei UI",10));
        application.setWindowIcon(QIcon(":/icon.svg"));
        qmlRegisterType<corig::WireLayer>("CoRig.Native",1,0,"WireLayer");
        qmlRegisterType<corig::NodeSurface>("CoRig.Native",1,0,"NodeSurface");
        qmlRegisterType<corig::NativePlot>("CoRig.Native",1,0,"NativePlot");
        qmlRegisterType<corig::NativeAttitude>("CoRig.Native",1,0,"NativeAttitude");
        qmlRegisterType<corig::NativeSixDof>("CoRig.Native",1,0,"NativeSixDof");
        const auto projects = parser.positionalArguments();
        const auto choices = QJsonDocument::fromJson(QByteArray::fromBase64(parser.value("plugin-selections").toLatin1())).toVariant().toMap();
        QVariantMap initialCatalog;
        if (!projects.isEmpty() && !choices.isEmpty()) { initialCatalog = corig::loadProject(projects.first()); initialCatalog["plugin_selections"] = choices; }
        qInfo("启动：工程 %s，插件选择 %lld 项",projects.isEmpty() ? "(无)" : qUtf8Printable(projects.first()),static_cast<qlonglong>(choices.size()));
        corig::Controller controller(directory,nullptr,initialCatalog); QQmlApplicationEngine qml;
        if (publishing) {
            try { controller.loadPublishedProject(corig::runOptions().payloadRoot + "/project/runtime.crpj"); }
            catch (const std::exception &error) {
                const auto message = QStringLiteral("应用载荷不完整：%1").arg(QString::fromUtf8(error.what()));
                qCritical("%s",qUtf8Printable(message));
                MessageBoxW(nullptr,reinterpret_cast<LPCWSTR>(message.utf16()),L"CoRig 无法启动",MB_OK | MB_ICONERROR);
                return 2;
            }
        }
        qml.addImageProvider("icons",new corig::IconProvider);
        QObject::connect(&application,&QCoreApplication::aboutToQuit,&controller,&corig::Controller::shutdown);
        controller.attachUi(&qml);
        qInfo("启动：界面已加载");
        if (!publishing && !projects.isEmpty()) {
            if (projects.first().endsWith(".crpl",Qt::CaseInsensitive)) controller.invoke("plugin.import",{{"path",projects.first()}});
            else controller.invoke("project.open",{{"path",projects.first()},{"plugin_selections",choices}});
        }
        const auto result = application.exec(); qInfo("启动：事件循环结束，退出码 %d",result); controller.shutdown(); corig::cleanupRuntimeTemp(); return result;
    } catch (const std::exception &error) {
        qCritical("%s",error.what());
#ifdef Q_OS_WIN
        const auto message = QString::fromUtf8(error.what());
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"CoRig 无法启动", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
