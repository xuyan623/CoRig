#pragma once

// The application has one checked command interface for QML and automation.
// Candidate designs become visible only after the runtime accepts them; view
// refreshes use immutable observations and never lock the graph worker.
#include "core/engine.h"
#include "core/python_host.h"
#include "core/native_plugin.h"
#include "core/run_mode.h"
#include "core/user_params.h"
#include "model_commands.h"
#include "plugin_store.h"
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QPointer>
#include <QSharedPointer>
#include <QQuickItemGrabResult>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QElapsedTimer>
#include <functional>

class QQmlEngine;
class QProcess;
class QQuickItem;
class QQuickWindow;
namespace corig {
class Workspaces;
class ApiServer;
class PropertySession;

class Controller final : public QObject {
    Q_OBJECT
    // Property accessors form the Qt meta-object interface consumed by QML.
    Q_PROPERTY(QVariantMap project READ project NOTIFY projectChanged)
    Q_PROPERTY(QVariantList graphNodes READ graphNodes NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap runtime READ runtime NOTIFY runtimeChanged)
    Q_PROPERTY(QVariantList registry READ registry NOTIFY catalogChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY preferencesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool executionPending READ executionPending NOTIFY runtimeChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY preferencesChanged)
    Q_PROPERTY(QString pythonPath READ pythonPath NOTIFY preferencesChanged)
    Q_PROPERTY(QString selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedNodes READ selectedNodes NOTIFY selectionChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString noticeLevel READ noticeLevel NOTIFY noticeChanged)
    Q_PROPERTY(QObject *workspaces READ workspaces CONSTANT)
    Q_PROPERTY(QVariantMap libraryPreferences READ libraryPreferences NOTIFY preferencesChanged)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    // Published applications expose their identity to QML so the editor surfaces
    // can stay hidden without a second window implementation.
    Q_PROPERTY(QString appMode READ appMode CONSTANT)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool retainOriginal READ retainOriginal CONSTANT)
    Q_PROPERTY(QString libraryTarget READ libraryTarget NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap pluginState READ pluginState NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantMap pendingPluginOpen READ pendingPluginOpen NOTIFY pluginOpenRequested)
    Q_PROPERTY(QVariantMap softwareExport READ softwareExport NOTIFY softwareExportChanged)
    Q_PROPERTY(QString softwareExportDirectory READ softwareExportDirectory NOTIFY preferencesChanged)
public:
    explicit Controller(QString dataDirectory, QObject *parent = nullptr, const QVariantMap &initialCatalog = {});
    ~Controller() override;
    void attachUi(QQmlEngine *engine);
    // Supplied by ApiServer once it owns the discovery record; app.connection
    // returns the same copyable text the GUI shows.
    void setConnectionProvider(std::function<QVariantMap()> provider) { connectionProvider_ = std::move(provider); }
    void shutdown();
    QVariant command(const QString &operation, const QVariantMap &arguments = {});
    QVariantMap schema() const;
    // Loads the snapshot a published payload carries. The manifest guarantees the
    // packages are embedded, so a readiness failure is a broken payload, not a
    // prompt for the user to pick plugin versions.
    QVariantMap loadPublishedProject(const QString &path);
    bool confirmExit(const QString &window, bool discard);
    QString dataDirectory() const { return dataDirectory_; }
    QString appMode() const { return publishMode_ ? QStringLiteral("publish") : QStringLiteral("editor"); }
    QString appName() const { return appName_; }
    QString appVersion() const { return appVersion_; }
    bool retainOriginal() const { return retainOriginal_; }
    bool publishMode() const { return publishMode_; }
    QVariantMap project() const { return project_; }
    QVariantList graphNodes() const { return graphNodes_; }
    QVariantMap runtime() const { return runtime_; }
    QVariantList registry() const { return catalog_.describe(); }
    QString projectPath() const { return projectPath_; }
    bool dirty() const { return dirty_; }
    bool readOnly() const { return readOnly_; }
    bool busy() const { return busy_; }
    bool executionPending() const { return runtimePending_ > 0; }
    QString theme() const { return theme_; }
    QString pythonPath() const { return python_->interpreter(); }
    QString selected() const { return selected_; }
    QString selectedKind() const { return selectedKind_; }
    QStringList selectedNodes() const { return selectedNodes_; }
    QString notice() const { return notice_; }
    QString noticeLevel() const { return noticeLevel_; }
    QObject *workspaces() const;
    QVariantMap libraryPreferences() const;
    QString libraryTarget() const { return libraryTarget_; }
    QVariantMap pluginState() const { return plugins_->state(project_); }
    QVariantMap pendingPluginOpen() const { return pendingPluginOpen_; }
    QVariantMap softwareExport() const { return softwareExport_; }
    QString softwareExportDirectory() const;
    Q_INVOKABLE void setSoftwareExportDirectory(const QString &directory);
    Q_INVOKABLE QVariant settingsValue(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE void setSettingsValue(const QString &key, const QVariant &value);

    void appendExportFinished(int code, QProcess::ExitStatus status);
    void finishSoftwareExport(bool cancelled, const QString &error, const QString &detail);
    void closeSoftwareExportJob();
    Q_INVOKABLE QVariantMap startSoftwareExport(const QVariantMap &arguments);
    Q_INVOKABLE void cancelSoftwareExport();
    Q_INVOKABLE void openSoftwareExportLog();
    Q_INVOKABLE bool projectNeedsPython() const;
    Q_INVOKABLE QVariant invoke(const QString &operation, const QVariantMap &arguments = {});
    Q_INVOKABLE QVariantMap job(const QString &id) const;
    Q_INVOKABLE QVariantMap nodeDesign(const QString &id) const;
    Q_INVOKABLE QVariantMap nodeState(const QString &id) const;
    Q_INVOKABLE QVariantMap definition(const QString &id) const;
    Q_INVOKABLE QVariantMap uiItem(const QString &id) const;
    Q_INVOKABLE QVariantMap inspector() const;
    Q_INVOKABLE QVariantMap propertyInfo(const QString &scope, const QString &target) const;
    Q_INVOKABLE QObject *propertyBridge(const QString &scope, const QString &target);
    Q_INVOKABLE QVariantMap containerLayout(const QString &plugin, const QVariantMap &properties, const QVariantList &children, double width, double height, int grid) const;
    Q_INVOKABLE QVariantList catalogEntries(const QString &query, const QStringList &category = {}, const QString &scope = "all") const;
    Q_INVOKABLE QVariantMap previewGeometry(const QString &plugin, const QVariantMap &config = {}) const;
    Q_INVOKABLE QVariantMap connectionOptions(const QString &node, const QString &port) const;
    Q_INVOKABLE void openLibrary(const QString &page);
    Q_INVOKABLE QString hitWire(double x, double y, double scale) const;
    Q_INVOKABLE QVariantMap hitPort(double x, double y, double scale) const;
    Q_INVOKABLE QVariantMap wireEditState(const QString &wire, double x = 0., double y = 0.) const;
    Q_INVOKABLE void dragPlugin(const QString &plugin);
    Q_INVOKABLE void dragUiItem(const QString &item);
    Q_INVOKABLE void dragObject(const QString &page, const QString &item);
    Q_INVOKABLE void observeView(const QString &node, const QString &view, const QVariantMap &arguments = {});
    Q_INVOKABLE QVariantMap viewState(const QString &node, const QString &view) const;
    Q_INVOKABLE void registerView(QObject *item, const QString &node, const QString &view);
    Q_INVOKABLE QString userViewUrl(const QString &node, const QString &view);
    Q_INVOKABLE QString pluginViewUrl(const QString &plugin, const QString &view);
    Q_INVOKABLE QString pluginResourceUrl(const QString &plugin, const QString &file) const;
    Q_INVOKABLE QObject *userViewBridge(const QString &node, const QString &view, const QString &item = {});
signals:
    void projectChanged();
    void runtimeChanged();
    void catalogChanged();
    void selectionChanged();
    void preferencesChanged();
    void noticeChanged();
    void busyChanged();
    void viewChanged(const QString &key);
    void jobFinished(const QString &id);
    void exitConfirmation(const QString &window);
    void graphFocusRequested(const QString &node);
    void pluginsChanged();
    void pluginOpenRequested();
    void softwareExportChanged();
    void pluginSettingsRequested();
    void settingsRequested(const QString &page);
    void pluginExportRequested(const QString &scope, const QString &plugin);
    void configurationConfirmationRequested(const QString &operation, const QVariantMap &arguments, const QVariantMap &plan);
    void draftResolutionRequested(const QString &operation, const QVariantMap &arguments, const QVariantList &drafts);
private:
    friend class Workspaces;
    friend class UserViewBridge;
    QVariantMap appState() const;
    QVariantMap launchEngine(const QString &operation, const QVariantMap &arguments, std::function<void(const QVariant &,const QString &)> completed = {});
    QVariantMap launchTask(const QString &operation, std::function<QVariant()> task, std::function<void(const QVariant &,const QString &)> completed = {});
    void finishJob(const QString &id, const QVariant &result, const QString &error);
    QVariantMap commit(EditResult edit, const QString &label, bool remember = true, bool runtimeOnly = false);
    void adopt(const QVariantMap &project, bool dirty, const QString &selection = {});
    void refreshGraph();
    void tell(const QString &message, const QString &level = "info");
    QVariant controls(const QString &operation, const QVariantMap &args);
    QVariant editorView(const QString &operation, const QVariantMap &args);
    QVariant configureWindow(const QVariantMap &args);
    QVariantMap capture(const QVariantMap &arguments);
    // Capture crosses event-loop turns while opening a page and while Qt
    // renders an exposed window. The request owns no raw UI pointers between
    // those turns: every step re-resolves the target and checks QPointer
    // liveness before touching it.
    struct CaptureState {
        QVariantMap arguments;
        QString target;
        QString path;
        QSize requested;
        QVariantMap requestObservation;
        QPointer<QQuickWindow> window;
        QPointer<QQuickItem> item;
        QSharedPointer<QQuickItemGrabResult> grab;
        QTimer *deadline = nullptr;
        QMetaObject::Connection dependency;
        QMetaObject::Connection ready;
        QMetaObject::Connection destroyed;
        bool opened = false;
        bool pageTurn = false;
        bool done = false;
    };
    void startCapture(const QString &id);
    void captureStep(const QString &id);
    void finishCapture(const QString &id, const QVariant &result, const QString &error);
    QVariantMap publishLog() const;
    QVariant viewCall(const QVariantMap &arguments);
    QVariantList viewActions(const QString &node, const QString &view) const;
    QVariantMap importPlugin(const QString &path) const;
    QVariantMap pluginService(const PluginPackage &package, const QString &node, const QString &operation, const QVariantMap &arguments);
    void requireEditable(bool layout = false) const;
    // Publication is a capability boundary, not a hidden menu: every command
    // passes through the registry policy before it can reach a handler.
    void ensureModeAllowed(const QString &operation) const;
    QVariantList dirtyProperties(const QString &scope = {}, const QString &target = {}) const;
    QVariantMap draftGuard(const QString &operation, const QVariantMap &arguments);
    QString dataDirectory_, projectPath_, selected_, selectedKind_ = "node", theme_ = "dark", notice_ = "就绪", noticeLevel_ = "info";
    QStringList selectedNodes_;
    QString libraryTarget_ = "graph";
    QVariantMap project_, baselineProject_, runtime_;
    QVariantList graphNodes_;
    Catalog catalog_{QList<PluginPackage>{}};
    std::unique_ptr<QSettings> settings_;
    std::unique_ptr<PluginStore> plugins_;
    QVariantMap pendingPluginOpen_;
    std::unique_ptr<PythonHost> python_;
    std::unique_ptr<UserParams> params_;
    QMap<QString,std::shared_ptr<PluginService>> services_;
    std::mutex servicesMutex_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Workspaces> workspaces_;
    std::unique_ptr<ApiServer> api_;
    QPointer<QQmlEngine> qmlEngine_;
    std::function<QVariantMap()> connectionProvider_;
    QTimer poll_;
    QThreadPool tasks_;
    QHash<quint64,std::function<void(const QVariant &,const QString &)>> callbacks_;
    QHash<QString,QVariantMap> jobs_, views_, viewArguments_;
    QHash<QString,QSharedPointer<CaptureState>> captures_;
    QVariantMap softwareExport_;
    QProcess *softwareExportProcess_ = nullptr;
    QString softwareExportLog_;
    QString softwareExportBase_;
    QString softwareExportJobId_;
    QByteArray softwareExportStdout_;
    bool softwareExportCancelling_ = false;
#ifdef Q_OS_WIN
    void *softwareExportJob_ = nullptr;
#endif
    QSet<QString> viewPending_;
    QHash<QString,QList<QPointer<QObject>>> viewItems_;
    QHash<QString,QPointer<QObject>> viewBridges_;
    QHash<QString,QPointer<PropertySession>> propertySessions_;
    mutable QHash<QString,QString> resourceUrls_;
    QHash<QString,std::shared_ptr<QTemporaryDir>> pluginDirectories_;
    QVector<QVariantMap> undo_, redo_;
    QElapsedTimer viewClock_;
    QHash<QString,qint64> viewRequestedAt_;
    QVariantMap observedViews_;
    bool dirty_ = false, readOnly_ = false, busy_ = false, exitDiscarded_ = false;
    bool publishMode_ = false, retainOriginal_ = false;
    QString appName_, appVersion_;
    int runtimePending_ = 0;
    std::atomic_bool closing_{false};
};

} // namespace corig
