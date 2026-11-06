#include "controller.h"
#include "command_registry.h"
#include "workspaces.h"
#include "quick/graph_geometry.h"
#include "updates.h"
#include "core/resource_policy.h"
#include "property_session.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QImage>
#include <QDateTime>
#include <QSaveFile>
#include <QUrl>
#include <QRegularExpression>
#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QProcess>
#include <algorithm>

namespace corig {

// Discovery lists the real command surface. Node/view-specific schemas stay
// with their declarations, so adding a plugin cannot widen unrelated commands.
QVariantMap Controller::schema() const
{
    const QMap<QString,QStringList> fields{
        {"app.state",{}},{"app.quit",{"discard"}},{"app.restart",{"discard","path","plugin_selections"}},{"preferences.set",{"theme","python","read_only"}},
        {"resources.get",{}},{"resources.set",{"limits","reset"}},{"job.cancel",{"id"}},{"job.list",{}},
        {"update.state",{}},{"update.check",{}},{"update.install",{}},
        {"registry",{}},{"schema",{}},{"project.get",{}},{"project.validate",{}},{"project.new",{"name","discard"}},
        {"project.open",{"path","discard","plugin_selections"}},{"project.save",{"path","include_plugins"}},
        {"project.export",{"path","include_plugins"}},{"project.options",{"include_plugins"}},{"project.rename",{"name"}},{"project.info",{"name","description","author","version"}},
        {"history.undo",{}},{"history.redo",{}},{"batch",{"commands"}},
        {"graph.node.add",{"id","plugin","title","config","position","orientation","placed"}},
        {"graph.node.remove",{"id","ids"}},{"graph.node.move",{"id","position"}},{"graph.node.rotate",{"id","degrees"}},
        {"graph.node.patch",{"id","changes"}},{"graph.wire.add",{"id","source","target","routing","color","width","points"}},
        {"graph.wire.remove",{"id"}},{"graph.wire.patch",{"id","changes"}},{"graph.select",{"id","kind","ids"}},{"graph.fit",{}},{"graph.overlaps",{}},
        {"graph.wire.handles",{"id","x","y"}},{"graph.activity",{}},
        {"graph.connection.options",{"node","port"}},
        {"node.configure",{"node","changes","persist","confirmed","revision"}},{"node.configure.plan",{"node","changes"}},{"node.state",{"node","detailed"}},{"node.action",{"node","action","args"}},
        {"params.set",{"node","param","value"}},{"params.get",{"node","param"}},{"params.reset",{"scope"}},{"app.connection",{}},{"project.export_original",{"path","overwrite"}},
        {"property.get",{"scope","target"}},{"property.stage",{"scope","target","changes"}},{"property.commit",{"scope","target","changes","confirmed","revision"}},{"property.discard",{"scope","target"}},{"property.drafts",{}},
        {"property.text",{"scope","target","field","text"}},
        {"runtime.state",{}},{"runtime.start",{"debug"}},{"runtime.debug",{"enabled"}},{"runtime.pause",{}},{"runtime.stop",{}},{"runtime.reset",{}},{"runtime.step",{"mode","source"}},
        {"runtime.emit",{"node","outputs"}},{"log.query",{"after","limit","node","level","text","batch"}},{"log.clear",{}},{"log.configure",{"detail"}},
        {"log.export",{"path","node","level","text","batch"}},{"io.ports",{}},{"clipboard.read",{}},
        {"ui.update",{}},{"ui.container.add",{"id","parent","plugin","properties"}},{"ui.view.add",{"id","parent","node","view","properties"}},
        {"ui.item.move",{"id","parent","index"}},{"ui.item.remove",{"id"}},{"ui.item.patch",{"id","properties"}},
        {"ui.item.schema",{"id"}},{"ui.view.bind",{"id","node","view"}},
        {"ui.view.appearance",{"node","view","changes"}},
        {"editor.state",{"page"}},{"editor.configure",{"page","zoom","pan_x","pan_y","left_width","right_width","fit","animation"}},
        {"editor.add",{"id","page","plugin"}},{"editor.place",{"page","id","ids","position","parent","index"}},
        {"editor.withdraw",{"page","id","ids"}},{"editor.remove",{"page","id","ids","confirmed","revision"}},
        {"editor.replace",{"page","id","ids","plugin","confirmed","revision"}},{"editor.reorder",{"page","id","index"}},
        {"editor.copy",{"page","ids"}},{"editor.paste",{"page","payload"}},{"editor.plan",{"page","ids","operation","plugin"}},
        {"sync.plan",{"direction"}},{"sync.apply",{"direction","keys","revision"}},
        {"library.search",{"query","category","scope"}},{"library.state",{}},{"library.favorite",{"plugin","enabled"}},
        {"library.configure",{"view","size"}},{"library.preview",{"plugin","scope"}},{"plugin.import",{"path","manifest"}},
        {"plugin.state",{}},{"plugin.tree",{"scope"}},{"plugin.settings",{}},{"plugin.directory",{}},{"plugin.enable",{"plugin","plugins","enabled"}},
        {"plugin.export",{"plugin","path","project","plugins","directory","overwrite","scope"}},{"plugin.export.dialog",{"scope","plugin"}},
        {"plugin.remove",{"plugin","package"}},{"python.probe",{"dependencies"}},{"job.get",{"id"}},
        {"workspace.open",{"page","window","activate"}},{"workspace.close",{"page","discard"}},{"workspace.detach",{"page","x","y"}},
        {"workspace.list",{}},{"workspace.configure",{"page","changes"}},{"workspace.remove",{"page"}},
        {"container.layout",{"id","width","height","grid"}},
        {"workspace.move",{"page","window","index"}},{"window.close",{"window","discard"}},{"window.pin",{"window","enabled"}},
        {"window.configure",{"window","x","y","width","height","state","focus"}},
        {"view.read",{"node","view","args"}},{"view.actions",{"node","view"}},{"view.call",{"node","view","action","args"}},
        {"controls.list",{"page"}},{"controls.get",{"id","page"}},{"controls.set",{"id","page","property","value"}},
        {"controls.click",{"id","page"}},{"controls.focus",{"id","page"}},{"controls.commit",{"id","page"}},
        {"controls.key",{"id","page","key","text","modifiers"}},
        {"controls.pointer",{"id","page","type","x","y","button","buttons","delta_x","delta_y","modifiers"}},
        {"settings.open",{"page"}},
        {"publish.start",{"project","directory","name","version","app_id","retain_original","overwrite","python"}},
        {"publish.state",{}},{"publish.cancel",{"job"}},{"publish.log",{}},
        {"capture",{"target","path","width","height","job","window"}}
    };
    QVariantMap commands;
    for (auto i = fields.cbegin(); i != fields.cend(); ++i) commands.insert(i.key(),QVariantMap{{"arguments",i.value()}});
    // The registry owns the machine-readable signature and its digest; the legacy
    // argument-name map stays so existing clients keep working.
    const auto &registry = CommandRegistry::instance();
    return {{"application","CoRig"},{"api_version",3},{"version","0.4.4"},{"commands",commands},{"signatures",registry.signatures()},
        {"schema_digest",registry.digest()},{"app_mode",publishMode_ ? "publish" : "editor"},
        {"async","A job is complete only after its operation succeeds; poll job.get"},
        {"values",QVariantMap{{"bytes","{type:bytes,base64:...,length:N}"},{"samples","{time:seconds,values:{name:number}}"}}}};
}

QVariant Controller::command(const QString &op, const QVariantMap &args)
{
    const auto commands = schema().value("commands").toMap();
    if (!commands.contains(op)) throw Error("未知操作：" + op);
    const auto fields = commands.value(op).toMap().value("arguments").toStringList();
    for (auto i = args.cbegin(); i != args.cend(); ++i) if (!fields.contains(i.key())) throw Error("操作 " + op + " 不接受参数：" + i.key());
    // Publication is enforced at the command boundary, before any handler runs.
    // batch is validated as a whole first: one forbidden child rejects the batch
    // without executing earlier children.
    if (publishMode_) {
        ensureModeAllowed(op);
        if (op == "batch") for (const auto &entry : args.value("commands").toList()) {
            const auto child = entry.toMap(); const auto childOp = child.value("op").toString();
            if (!commands.contains(childOp)) throw Error("未知操作：" + childOp);
            ensureModeAllowed(childOp);
            const auto childArgs = child.value("args").toMap();
            const auto childFields = commands.value(childOp).toMap().value("arguments").toStringList();
            for (auto i = childArgs.cbegin(); i != childArgs.cend(); ++i)
                if (!childFields.contains(i.key())) throw Error("操作 " + childOp + " 不接受参数：" + i.key());
        }
    }
    if (op == "app.connection") {
        if (!connectionProvider_) throw Error("AI 接入信息尚未就绪");
        return connectionProvider_();
    }
    const auto guard = draftGuard(op,args); if (!guard.isEmpty()) return guard;
    if (op == "property.drafts") return dirtyProperties();
    if (op.startsWith("property.")) {
        const auto scope = args.value("scope").toString(), target = args.value("target").toString();
        const auto details = propertyInfo(scope,target); if (details.contains("error")) throw Error(details.value("error").toString());
        auto session = qobject_cast<PropertySession *>(propertyBridge(scope,target));
        if (op == "property.get") { auto result = details; result["draft"] = session->draft(); result["text_draft"] = session->textDraft(); result["dirty"] = session->dirty(); result["pending"] = session->pending(); result["error"] = session->error(); return result; }
        if (op == "property.discard") { if (session->pending()) throw Error("请等待当前属性提交完成"); session->discard(); return QVariantMap{{"discarded",true}}; }
        if (op == "property.text") {
            const auto result = session->stageText(args.value("field").toString(),args.value("text").toString()).toMap();
            if (result.contains("error")) throw Error(result.value("error").toString());
            return result;
        }
        const auto staged = session->stage(args.value("changes").toMap()).toMap();
        if (staged.contains("error")) throw Error(staged.value("error").toString());
        return op == "property.stage" ? QVariant(staged) : session->apply(args.value("confirmed").toBool(),args.value("revision").toString());
    }
    if (op == "node.configure.plan") return nodeConfigurationPlan(project_,catalog_,args.value("node").toString(),args.value("changes").toMap());
    if (op == "schema") return schema();
    if (op == "app.state") return appState();
    if (op == "registry") return registry();
    if (op == "job.get") return job(args.value("id").toString());
    if (op == "job.list") { QVariantList result; for (const auto &entry : jobs_) result.append(entry); return result; }
    if (op == "job.cancel") {
        const auto id = args.value("id").toString(), task = jobs_.value(id).value("python_task").toString();
        if (task.isEmpty()) throw Error("此任务没有可取消的后台操作："+id);
        return launchTask(op,[this,task] { return python_->cancelTask(task); });
    }
    if (op == "resources.get") return resourceLimits();
    if (op == "resources.set") {
        if (engine_->observation().value("active").toBool() || busy_) throw Error("请先结束运行会话，再调整资源额度");
        const auto previous = resourceLimits();
        try {
            const auto limits = configureResourceLimits(args.value("limits").toMap(),args.value("reset").toBool());
            validateProject(project_,catalog_); python_->close();
            settings_->setValue("resources/limits",limits); emit preferencesChanged(); return limits;
        } catch (...) { configureResourceLimits(previous,true); throw; }
    }
    if (op == "runtime.state") return engine_->observation();
    if (op == "project.get") return project_;
    // Discovery rescans first: packages dragged into the library folder appear
    // without restarting, and deleted files disappear.
    if (op == "plugin.state") { plugins_->rescan(); return pluginState(); }
    if (op == "plugin.tree") { plugins_->rescan(); return plugins_->tree(project_,args.value("scope","project").toString()); }
    if (op == "plugin.export.dialog") { emit pluginExportRequested(args.value("scope","project").toString(),args.value("plugin").toString()); return QVariantMap{{"opened",true}}; }
    if (op == "plugin.directory") { if (!QDesktopServices::openUrl(QUrl::fromLocalFile(pluginState().value("directory").toString()))) throw Error("无法打开插件文件夹"); return QVariantMap{{"opened",true}}; }
    if (op == "plugin.settings") { plugins_->rescan(); emit pluginSettingsRequested(); return QVariantMap{{"opened",true}}; }
    if (op == "settings.open") {
        auto page = args.value("page").toString();
        if (page.isEmpty()) page = "general";
        const QStringList allowed = publishMode_
            ? QStringList{"general","tasks"}
            : QStringList{"general","project","plugins","python","resources","tasks"};
        if (!allowed.contains(page)) throw Error("当前模式没有设置页：" + page);
        emit settingsRequested(page);
        return QVariantMap{{"opened",true},{"page",page}};
    }
    if (op == "update.state") return QVariantMap{{"supported",!maintenanceTool().isEmpty()},{"version","0.4.4"}};
    if (op == "update.check") return launchTask(op,[this] { return checkUpdates([this] { return closing_.load(); }); });
    if (op == "update.install") {
        if (dirty_) throw Error("请先保存当前工程，再开始更新");
        if (busy_) throw Error("请等待当前工程操作完成");
        const auto tool = maintenanceTool(); if (tool.isEmpty()) throw Error("请使用安装版 CoRig 进行安装式更新");
        if (!QProcess::startDetached(tool,{"--cache-path",QCoreApplication::applicationDirPath() + "/cache/installer","--start-updater"})) throw Error("无法启动安装式更新");
        exitDiscarded_ = true; QTimer::singleShot(50,qApp,&QCoreApplication::quit);
        return QVariantMap{{"updating",true}};
    }
    if (op == "graph.connection.options") {
        const auto node = args.value("node").toString(), port = args.value("port").toString();
        if (nodeDesign(node).isEmpty()) throw Error("节点不存在：" + node);
        const auto geometry = previewGeometry(nodeDesign(node).value("plugin").toString(),nodeDesign(node).value("config").toMap());
        bool found = false;
        for (const auto &entry : geometry.value("ports").toList()) found |= entry.toMap().value("id") == port;
        if (!found) throw Error("端口不存在：" + port);
        return connectionOptions(node,port);
    }
    if (op == "ui.item.schema") {
        const auto item = uiItem(args.value("id").toString());
        if (item.isEmpty()) throw Error("界面对象不存在");
        return uiPropertySchema(item,catalog_,project_);
    }
    if (op == "container.layout") {
        const auto item = uiItem(args.value("id").toString()); QVariantList children;
        if (item.isEmpty()) throw Error("界面容器不存在");
        for (const auto &id : item.value("children").toList()) children.append(uiItem(id.toString()));
        const auto result = containerLayout(item.value("plugin").toString(),item.value("properties").toMap(),children,
            args.value("width").toDouble(),args.value("height").toDouble(),args.value("grid",16).toInt());
        if (result.contains("error")) throw Error(result.value("error").toString());
        return result;
    }
    if (op == "workspace.configure" || op == "workspace.remove") {
        requireEditable(true);
        return commit(editProject(project_,catalog_,op,args),"工作区属性已更新");
    }
    if (op == "workspace.list") {
        if (!workspaces_) throw Error("工作区尚未建立");
        return workspaces_->available();
    }
    if (op == "editor.state" || op == "editor.configure") return editorView(op,args);
    if (op == "window.configure") return configureWindow(args);
    if (op == "sync.plan") return synchronizationPlan(project_,catalog_,args.value("direction").toString());
    if (op == "editor.plan") return editorImpact(project_,catalog_,args);
    if (op == "editor.copy") {
        const auto payload = copyEditorObjects(project_,args.value("page","graph").toString(),args.value("ids").toStringList());
        QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact)));
        return QVariantMap{{"copied",args.value("ids")}};
    }
    if (op == "clipboard.read") return QVariantMap{{"text",QGuiApplication::clipboard()->text()}};
    if (op == "io.ports") { const auto package = catalog_.servicePackage("io"); return launchTask(op,[this,package] { return pluginService(package,{},"list_ports",{}); }); }
    if (op == "project.validate") { validateProject(project_,catalog_); return QVariantMap{{"valid",true}}; }
    if (op == "preferences.set") {
        // A published application keeps its data root and Python environment
        // immutable; theme and log path are the only user preferences.
        if (publishMode_ && (args.contains("read_only") || args.contains("python")))
            throw Error("发布运行模式不允许修改此设置");
        if (args.contains("theme")) { const auto theme = args.value("theme").toString(); if (theme != "dark" && theme != "light") throw Error("主题需要 dark 或 light"); theme_ = theme; settings_->setValue("theme",theme); }
        if (args.contains("python")) { if (engine_->observation().value("active").toBool()) throw Error("请先停止会话再切换 Python 环境"); python_->selectInterpreter(args.value("python").toString()); settings_->setValue("python",pythonPath()); }
        if (args.contains("read_only")) readOnly_ = args.value("read_only").toBool();
        emit preferencesChanged(); return appState();
    }
    if (op == "app.quit") {
        const bool discard = args.value("discard",false).toBool();
        if (dirty_ && !discard) throw Error("工程有未保存修改，请先保存或明确丢弃");
        if (busy_) throw Error("请等待工程操作完成");
        exitDiscarded_ = true;
        QTimer::singleShot(50,qApp,&QCoreApplication::quit); return QVariantMap{{"quitting",true}};
    }
    if (op == "app.restart") {
        if (dirty_ && !args.value("discard").toBool()) throw Error("请先保存当前工程，再重启 CoRig");
        if (busy_) throw Error("请等待工程操作完成");
        // A published application restarts itself without arguments: the payload
        // manifest decides the mode and the data root again, so no shortcut can
        // redirect it back into an editor session.
        QStringList arguments;
        if (!publishMode_) {
            arguments << "--data-dir" << dataDirectory_;
            const auto path = args.value("path",projectPath_).toString();
            if (!path.isEmpty()) arguments.append(path);
            if (args.contains("plugin_selections")) arguments << "--plugin-selections" << QString::fromLatin1(QJsonDocument::fromVariant(args.value("plugin_selections")).toJson(QJsonDocument::Compact).toBase64());
        }
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),arguments)) throw Error("无法重新启动 CoRig");
        exitDiscarded_ = true; QTimer::singleShot(50,qApp,&QCoreApplication::quit);
        return QVariantMap{{"restarting",true}};
    }
    if (op == "project.open" || op == "project.new") {
        // Switching projects ends the running session, so this is a decision the
        // window can offer to perform. Failing here would only reach the
        // status-bar notice, which looks like the click did nothing.
        if (engine_->observation().value("running").toBool()) {
            qWarning("工程切换推迟：运行中（%s）",qUtf8Printable(op));
            return QVariantMap{{"requires_runtime_stop",true},{"project",project_.value("name").toString()}};
        }
        requireEditable();
        if (dirty_ && !args.value("discard").toBool()) throw Error("请先保存当前工程，或明确丢弃修改");
        auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
        if (op == "project.open" && path.isEmpty()) throw Error("请选择工程文件");
        busy_ = true; emit busyChanged();
        QVariantMap request = op == "project.open" ? QVariantMap{{"path",path},{"plugin_selections",args.value("plugin_selections",QVariantMap{})}} : QVariantMap{{"project",emptyProject(args.value("name","未命名工程").toString())}};
        return launchEngine(op == "project.open" ? "project.load" : "project.replace",request,
            [this,path,args](const QVariant &result, const QString &error) {
                busy_ = false; emit busyChanged();
                // A failed open used to survive only in the job record. Startup
                // launches may not have loaded QML yet, so the notice and the log
                // are what tells the user why the window stayed empty.
                if (!error.isEmpty()) { qWarning("project.open 失败：%s",qUtf8Printable(error)); tell("工程打开失败：" + error,"error"); return; }
                const auto readiness = result.toMap();
                if (readiness.value("requires_plugin_selection").toBool() || readiness.value("requires_restart").toBool() || readiness.value("requires_plugins").toBool()) {
                    pendingPluginOpen_ = readiness; pendingPluginOpen_["path"] = path;
                    pendingPluginOpen_["plugin_selections"] = args.value("plugin_selections",QVariantMap{});
                    emit pluginOpenRequested(); return;
                }
                pendingPluginOpen_.clear();
                projectPath_ = path; undo_.clear(); redo_.clear(); views_.clear();
                // Qt can reuse layout items when another project has the same
                // item IDs. Keep their subscriptions, reset per-project ranges.
                for (auto &arguments : viewArguments_) arguments.clear();
                pluginDirectories_.clear(); selected_.clear(); selectedNodes_.clear(); emit selectionChanged(); adopt(result.toMap(),false); tell("工程已打开");
            });
    }
    if (op == "project.options") {
        if (args.contains("include_plugins")) {
            requireEditable(true);
            if (args.value("include_plugins").metaType().id() != QMetaType::Bool) throw Error("include_plugins 需要布尔值");
            auto next = project_; next["save_options"] = QVariantMap{{"include_plugins",args.value("include_plugins")}};
            return commit({next,{}, {},false},"工程保存选项已修改");
        }
        return project_.value("save_options",QVariantMap{{"include_plugins",true}});
    }
    if (op == "project.save" || op == "project.export") {
        if (busy_) throw Error("工程操作尚未完成");
        const bool exporting = op == "project.export";
        const auto includeValue = args.value("include_plugins",project_.value("save_options").toMap().value("include_plugins",true));
        if (includeValue.metaType().id() != QMetaType::Bool) throw Error("include_plugins 需要布尔值");
        const bool include = includeValue.toBool();
        auto path = args.value("path",projectPath_).toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
        if (exporting && !args.contains("path")) throw Error("请选择工程导出路径");
        if (path.isEmpty()) path = dataDirectory_ + "/project/" + project_.value("name").toString().replace(QRegularExpression("[\\\\/:*?\"<>|]"),"_") + ".crpj";
        if (path.endsWith(".corig",Qt::CaseInsensitive)) path.chop(6);
        if (!path.endsWith(".crpj",Qt::CaseInsensitive)) path += ".crpj";
        path = QFileInfo(path).absoluteFilePath(); const auto design = project_, snapshot = catalog_.embed(project_,include), loaded = catalog_.embed(project_,true);
        busy_ = true; emit busyChanged();
        return launchTask(op,[path,snapshot,include] { QDir().mkpath(QFileInfo(path).absolutePath()); saveProject(path,snapshot); return QVariantMap{{"path",path},{"include_plugins",include},{"dependencies",snapshot.value("plugin_dependencies")}}; },
            [this,path,design,snapshot,loaded,exporting](const QVariant &, const QString &error) {
                busy_ = false; emit busyChanged(); if (!error.isEmpty()) return;
                if (!exporting) {
                    projectPath_ = path;
                    if (project_ == design) {
                        const auto choices = project_.value("plugin_selections"); project_ = loaded;
                        project_["save_options"] = snapshot.value("save_options");
                        if (choices.isValid()) project_["plugin_selections"] = choices;
                        dirty_ = false;
                    }
                    emit projectChanged();
                }
                tell(exporting ? "工程已导出" : "工程已保存");
            });
    }
    if (op == "project.rename") { requireEditable(true); auto next = project_; const auto name = args.value("name").toString().trimmed(); if (name.isEmpty()) throw Error("工程名称不能为空"); next["name"] = name; return commit({next,{}, {},false},"工程已重命名"); }
    if (op == "project.info") {
        requireEditable(true);
        auto next = project_;
        if (args.contains("name")) {
            const auto name = args.value("name").toString().trimmed();
            if (name.isEmpty()) throw Error("工程名称不能为空");
            next["name"] = name;
        }
        for (const auto &field : {QStringLiteral("description"), QStringLiteral("author"), QStringLiteral("version")}) {
            if (!args.contains(field)) continue;
            const auto value = args.value(field).toString().trimmed();
            if (field == QStringLiteral("version") && value.isEmpty()) throw Error("工程版本不能为空");
            next[field] = value;
        }
        return commit({next,{}, {},false},"工程信息已修改");
    }
    if (op == "history.undo" || op == "history.redo") {
        requireEditable(); auto &source = op == "history.undo" ? undo_ : redo_; if (source.isEmpty()) throw Error("没有可" + QString(op == "history.undo" ? "撤销" : "重做") + "的操作");
        const auto candidate = source.last(), previous = project_; busy_ = true; emit busyChanged();
        return launchEngine("project.apply",{{"project",candidate}},[this,op,candidate,previous](const QVariant &, const QString &error) {
            busy_ = false; emit busyChanged(); if (!error.isEmpty()) return;
            auto &from = op == "history.undo" ? undo_ : redo_; auto &to = op == "history.undo" ? redo_ : undo_; from.removeLast(); to.append(previous); adopt(candidate,true);
        });
    }
    if (op == "graph.select") {
        selected_ = args.value("id").toString(); selectedKind_ = args.value("kind","node").toString(); selectedNodes_ = args.value("ids").toStringList();
        if (selectedKind_ == "node" && selectedNodes_.isEmpty() && !selected_.isEmpty()) selectedNodes_.append(selected_);
        emit selectionChanged(); return QVariantMap{{"id",selected_},{"kind",selectedKind_},{"ids",selectedNodes_}};
    }
    if (op == "graph.fit") { if (workspaces_) workspaces_->open("graph",{},false); emit graphFocusRequested({}); return QVariantMap{{"fitted",true}}; }
    if (op == "graph.activity") {
        const auto page = workspaces_ ? workspaces_->page("graph") : nullptr;
        const auto renderer = page ? page->property("wireRenderer").value<QObject *>() : nullptr;
        QVariantMap result;
        if (!renderer || !QMetaObject::invokeMethod(renderer,"activityState",Q_RETURN_ARG(QVariantMap,result))) throw Error("功能设计视图尚未打开");
        return result;
    }
    if (op == "graph.overlaps") { QVariantList overlaps; for (const auto &entry : graphNodes_) if (entry.toMap().value("overlap").toBool()) overlaps.append(entry.toMap().value("id")); return overlaps; }
    if (op == "graph.wire.handles") return wireEditState(args.value("id").toString(),args.value("x",0.).toDouble(),args.value("y",0.).toDouble());
    if (op == "project.export_original") {
        if (!publishMode_) throw Error("只有发布应用能导出留存工程");
        if (!retainOriginal_) throw Error("该应用发布时未留存可导出的工程");
        const auto target = args.value("path").toString();
        if (target.isEmpty()) throw Error("project.export_original 需要 path");
        const auto source = runOptions().payloadRoot + QStringLiteral("/project/runtime.crpj");
        if (!QFileInfo(source).isFile()) throw Error("发布载荷缺少工程快照");
        if (QFileInfo::exists(target) && !args.value("overwrite").toBool()) throw Error("目标已存在：" + target);
        QFile input(source); if (!input.open(QIODevice::ReadOnly)) throw Error("无法读取留存工程");
        QSaveFile output(target);
        if (!output.open(QIODevice::WriteOnly) || output.write(input.readAll()) < 0 || !output.commit())
            throw Error("无法写入工程文件：" + target);
        return QVariantMap{{"exported",true},{"path",QFileInfo(target).absoluteFilePath()}};
    }
    if (op == "params.set") {
        if (!publishMode_) throw Error("用户参数只在发布运行模式可用");
        const auto node = args.value("node").toString();
        const auto param = args.value("param").toString();
        const auto value = args.value("value");
        if (node.isEmpty() || param.isEmpty()) throw Error("params.set 需要 node 与 param");
        const auto design = nodeDesign(node); if (design.isEmpty()) throw Error("节点不存在：" + node);
        const auto plugin = design.value("plugin").toString();
        const auto definition = catalog_.definition(plugin);
        const auto properties = definition.value("properties").toMap();
        if (!properties.contains(param)) throw Error("节点未声明参数：" + param);
        const auto current = design.value("config").toMap();
        const bool controlValue = plugin.startsWith("control.") && param == QStringLiteral("value");
        // Control ranges live in sibling config values, not in the property
        // declaration, and a stopped session has no node to ask. Run the same
        // business check set_value uses before anything reaches the overlay.
        if (controlValue) {
            if (plugin == QStringLiteral("control.number") || plugin == QStringLiteral("control.slider")) {
                const auto number = finiteNumber(value,QStringLiteral("控件值"));
                if (current.contains(QStringLiteral("minimum")) && number < current.value(QStringLiteral("minimum")).toDouble()) throw Error("控件值小于下限");
                if (current.contains(QStringLiteral("maximum")) && number > current.value(QStringLiteral("maximum")).toDouble()) throw Error("控件值超过上限");
            } else if (plugin == QStringLiteral("control.checkbox")) {
                if (value.metaType().id() != QMetaType::Bool) throw Error("控件值需要布尔值");
            } else if (plugin == QStringLiteral("control.select")) {
                const auto choices = current.value(QStringLiteral("choices")).toList();
                if (!choices.isEmpty() && !choices.contains(value)) throw Error("控件值不在选项中");
            }
        }
        // Validate the complete resulting configuration, not just this value: a
        // value that changes dynamic ports must fail before anything is stored.
        const auto config = catalog_.checkedConfig(plugin,{{param,value}},current);
        const auto before = catalog_.ports(plugin,current);
        const auto after = catalog_.ports(plugin,config);
        bool samePorts = before.size() == after.size();
        for (qsizetype i = 0; samePorts && i < before.size(); ++i)
            if (before[i].describe() != after[i].describe()) samePorts = false;
        if (!samePorts) throw Error("发布运行模式不能改变节点端口或已连接的导线");
        const auto previous = current.value(param);
        const bool hadPrevious = current.contains(param);
        // The baseline always comes from the shipped snapshot, never from the
        // current in-memory copy: a temporary persist=false change must not
        // become the value reset() restores later.
        QVariantMap baselineConfig;
        for (const auto &entry : baselineProject_.value("nodes").toList())
            if (entry.toMap().value("id") == node) baselineConfig = entry.toMap().value("config").toMap();
        if (baselineConfig.contains(param)) params_->captureBaseline(node,param,baselineConfig.value(param));
        // The accepted value is visible to params.get immediately; the engine
        // executes the same change below and a rejected runtime change rolls the
        // overlay back, so a failed call never leaves a stored value behind.
        params_->setNode(node,param,value); params_->scheduleSave();
        auto next = project_; auto nodes = next.value("nodes").toList();
        for (auto &entry : nodes) { auto item = entry.toMap(); if (item.value("id") != node) continue; item["config"] = config; entry = item; }
        next["nodes"] = nodes; project_ = next; emit projectChanged();
        const auto rollback = [this,node,param,previous,hadPrevious,current] {
            if (hadPrevious) params_->setNode(node,param,previous); else params_->removeNode(node,param);
            params_->scheduleSave();
            auto restored = project_; auto restoredNodes = restored.value("nodes").toList();
            for (auto &entry : restoredNodes) {
                auto item = entry.toMap(); if (item.value("id") != node) continue;
                item["config"] = current; entry = item;
            }
            restored["nodes"] = restoredNodes; project_ = restored; emit projectChanged();
        };
        const bool active = engine_->observation().value("active").toBool();
        const bool running = engine_->observation().value("running").toBool();
        if (!active) {
            // No session yet: keep the engine's project copy in step so a later
            // runtime.start runs the value the overlay will replay on restart.
            engine_->submit("project.apply",{{"project",next}});
            return QVariantMap{{"updated",true},{"node",node},{"param",param}};
        }
        // Controls publish a value event, so their commit must use the same node
        // action the editor uses; it is valid while running and while paused.
        if (controlValue) {
            auto job = launchEngine("node.action",{{"node",node},{"action","set_value"},{"args",QVariantMap{{"value",value}}}},
                [rollback](const QVariant &, const QString &error) { if (!error.isEmpty()) rollback(); });
            job["updated"] = true; job["node"] = node; job["param"] = param;
            return job;
        }
        if (!running) {
            // Paused: a non-runtime_editable parameter is a legal design-value
            // change, exactly as node.configure treats it. Reload the project
            // instead of asking the engine's running-only configure path.
            engine_->submit("project.apply",{{"project",next}});
            return QVariantMap{{"updated",true},{"node",node},{"param",param}};
        }
        // Running: every other parameter goes through the engine's runtime
        // configure path, which needs the full design to rebuild the node.
        auto engineDesign = design; engineDesign["config"] = config;
        auto job = launchEngine("node.configure",{{"node",node},{"design",engineDesign},{"changes",QVariantMap{{param,value}}},{"persist",false}},
            [rollback](const QVariant &, const QString &error) { if (!error.isEmpty()) rollback(); });
        job["updated"] = true; job["node"] = node; job["param"] = param;
        return job;
    }
    if (op == "params.get") {
        if (!publishMode_) throw Error("用户参数只在发布运行模式可用");
        const auto node = args.value("node").toString(), param = args.value("param").toString();
        if (node.isEmpty()) return params_->values();
        return QVariantMap{{"node",node},{"param",param},{"value",params_->nodeValue(node,param)}};
    }
    if (op == "params.reset") {
        if (!publishMode_) throw Error("用户参数只在发布运行模式可用");
        // Reset restores the shipped snapshot itself, not a value captured from
        // the current session: temporary or saved changes must never survive it.
        const auto next = baselineProject_.isEmpty() ? project_ : baselineProject_;
        params_->reset(); project_ = next; emit projectChanged();
        // Restoring the shipped values must reach the live engine as well; the
        // overlay alone only changes what the next start replays. The session is
        // rebuilt so retained node state (a control's displayed value) resets too.
        const bool running = engine_->observation().value("running").toBool();
        engine_->submit("runtime.reset",{{"project",next}});
        if (running) engine_->submit("runtime.start");
        return QVariantMap{{"reset",true},{"scope",args.value("scope","all")}};
    }
    if (op == "node.configure") {
        const auto id = args.value("node").toString(); const auto design = nodeDesign(id); if (design.isEmpty()) throw Error("节点不存在：" + id);
        const bool running = engine_->observation().value("running").toBool();
        // Published applications have no editing mode, so every configure is a
        // parameter change: it goes through the engine and the user overlay.
        const bool persist = args.value("persist",publishMode_ || !readOnly_).toBool();
        const auto plan = nodeConfigurationPlan(project_,catalog_,id,args.value("changes").toMap());
        if (plan.value("requires_confirmation").toBool()) {
            if (running || !persist) throw Error("改变已连接端口需要先暂停，并修改工程设计");
            if (!args.value("confirmed").toBool() || args.value("revision") != plan.value("revision")) return plan;
        }
        if (publishMode_) {
            // A published application never edits the design. Every configure is a
            // validated parameter change with a fixed topology: a change that would
            // alter the node's ports (for example a dynamic output list) is refused
            // instead of deleting the wires that no longer match.
            if (busy_) throw Error("工程操作尚未完成");
            const auto changes = args.value("changes").toMap();
            const auto plugin = design.value("plugin").toString();
            const auto definition = catalog_.definition(plugin);
            const auto current = design.value("config").toMap();
            const auto config = catalog_.checkedConfig(plugin,changes,current);
            const auto before = catalog_.ports(plugin,current);
            const auto after = catalog_.ports(plugin,config);
            bool samePorts = before.size() == after.size();
            for (qsizetype i = 0; samePorts && i < before.size(); ++i)
                if (before[i].describe() != after[i].describe()) samePorts = false;
            if (!samePorts) throw Error("发布运行模式不能改变节点端口或已连接的导线");
            if (running) for (auto i = changes.cbegin(); i != changes.cend(); ++i)
                if (!definition.value("properties").toMap().value(i.key()).toMap().value("runtime_editable").toBool())
                    throw Error("该参数需要暂停后修改：" + i.key());
            if (persist) {
                QVariantMap baselineConfig;
                for (const auto &entry : baselineProject_.value("nodes").toList())
                    if (entry.toMap().value("id") == id) baselineConfig = entry.toMap().value("config").toMap();
                for (auto i = changes.cbegin(); i != changes.cend(); ++i)
                    if (baselineConfig.contains(i.key())) params_->captureBaseline(id,i.key(),baselineConfig.value(i.key()));
            }
            auto next = project_; auto nodes = next.value("nodes").toList();
            for (auto &entry : nodes) { auto item = entry.toMap(); if (item.value("id") != id) continue; item["config"] = config; entry = item; }
            next["nodes"] = nodes; project_ = next; emit projectChanged();
            if (persist) {
                for (auto i = changes.cbegin(); i != changes.cend(); ++i) params_->setNode(id,i.key(),i.value());
                params_->scheduleSave();
            }
            if (running) {
                const auto rollback = [this,id,persist,current,changes] {
                    if (persist) for (auto i = changes.cbegin(); i != changes.cend(); ++i) {
                        if (current.contains(i.key())) params_->setNode(id,i.key(),current.value(i.key()));
                        else params_->removeNode(id,i.key());
                    }
                    params_->scheduleSave();
                    auto restored = project_; auto items = restored.value("nodes").toList();
                    for (auto &entry : items) { auto item = entry.toMap(); if (item.value("id") == id) { item["config"] = current; entry = item; } }
                    restored["nodes"] = items; project_ = restored; emit projectChanged();
                };
                // The engine rebuilds the node from this design; without it a
                // configure request fails as "插件不存在".
                auto engineDesign = design; engineDesign["config"] = config;
                return launchEngine("node.configure",{{"node",id},{"design",engineDesign},{"changes",changes},{"persist",false}},
                    [this,id,persist,rollback](const QVariant &result, const QString &error) {
                        if (!error.isEmpty()) { rollback(); return; }
                        if (!persist) return;
                        // Store the normalized values the engine accepted, not the raw
                        // request, so the overlay replays exactly what ran.
                        const auto checked = result.toMap().value("config").toMap();
                        auto applied = project_; auto items = applied.value("nodes").toList();
                        for (auto &entry : items) { auto item = entry.toMap(); if (item.value("id") == id) { item["config"] = checked; entry = item; } }
                        applied["nodes"] = items; project_ = applied; emit projectChanged();
                    });
            }
            // Paused or stopped: reload the validated project so the engine sees the
            // same values the overlay will replay on the next start.
            engine_->submit("project.apply",{{"project",next}});
            return QVariantMap{{"updated",true},{"node",id},{"config",config}};
        }
        if (running || !persist) {
            if (busy_) throw Error("工程操作尚未完成");
            if (persist && readOnly_ && !publishMode_) throw Error("只读模式不能保存运行参数到设计");
            const auto prepared = editProject(project_,catalog_,op,args); auto changes = args.value("changes").toMap();
            QVariantMap normalized;
            for (const auto &entry : prepared.project.value("nodes").toList()) if (entry.toMap().value("id") == id) normalized = entry.toMap();
            const auto checked = normalized.value("config").toMap(); for (auto i = changes.begin(); i != changes.end(); ++i) i.value() = checked.value(i.key());
            return launchEngine(op,{{"node",id},{"design",normalized},{"changes",changes},{"resources",prepared.project.value("resources")}},[this,id,persist,changes](const QVariant &result, const QString &error) {
                if (!error.isEmpty()) return;
                const auto config = result.toMap().value("config");
                if (!persist) return;
                auto next = project_; auto nodes = next.value("nodes").toList();
                for (auto &entry : nodes) { auto item = entry.toMap(); if (item.value("id") == id) { item["config"] = config; entry = item; } }
                next["nodes"] = nodes; next["resources"] = result.toMap().value("__resources");
                if (publishMode_) {
                    // Published applications never mark the snapshot dirty: explicit
                    // parameter changes go to the user overlay and the in-memory copy.
                    for (auto i = changes.cbegin(); i != changes.cend(); ++i) params_->setNode(id,i.key(),i.value());
                    params_->scheduleSave(); project_ = next; emit projectChanged(); return;
                }
                undo_.append(project_); redo_.clear(); adopt(next,true,id);
            });
        }
        requireEditable(); return commit(editProject(project_,catalog_,op,args),"节点参数已更新");
    }
    if (op == "ui.view.appearance") {
        // Editors and automation share one entry: reading returns the declared
        // appearance schema and the checked current values, writing validates a
        // candidate project and commits it like any other UI edit.
        const auto node = args.value("node").toString(), view = args.value("view").toString();
        const auto schema = viewAppearanceSchema(project_,catalog_,node,view);
        if (!args.contains("changes") || args.value("changes").toMap().isEmpty())
            return QVariantMap{{"target",node+"/"+view},{"node",node},{"view",view},{"schema",schema},
                {"properties",checkedProperties(schema,{},viewAppearance(project_,node,view))},{"updated",false}};
        requireEditable(true);
        const auto edit = editProject(project_,catalog_,op,args);
        const auto receipt = commit(edit,"视图外观已更新");
        // Reading and writing return one shape: the checked appearance and the
        // schema stay at the top level so bridge.commit and automation see the
        // same reply. A view appearance never changes the engine, so the commit
        // is synchronous; the job branch is kept for a future engine edit.
        auto result = edit.result.toMap();
        result["updated"] = true;
        if (receipt.contains("job")) result["job"] = receipt.value("job");
        return result;
    }
    if (op == "batch" || op.startsWith("graph.node.") || op.startsWith("graph.wire.") || op.startsWith("ui.") || op.startsWith("editor.") || op == "sync.apply") {
        const bool layoutOnly = op.startsWith("ui.") || (op.startsWith("editor.") && args.value("page") == "layout") || op == "graph.node.move" || op == "graph.node.rotate" || op == "graph.node.patch" || op == "graph.wire.patch" || op == "sync.apply";
        requireEditable(layoutOnly || op == "batch");
        auto arguments = args;
        if (op == "editor.paste" && !arguments.contains("payload")) { const auto json = QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()); if (!json.isObject()) throw Error("剪贴板没有可粘贴对象"); arguments["payload"] = json.toVariant(); }
        const auto edit = editProject(project_,catalog_,op,arguments);
        if (edit.engineChanged && engine_->observation().value("running").toBool()) throw Error("请先暂停运行再修改功能设计");
        if (op == "graph.node.add") { auto recent = settings_->value("library/recent").toStringList(); const auto plugin = args.value("plugin").toString(); recent.removeAll(plugin); recent.prepend(plugin); while (recent.size() > 24) recent.removeLast(); settings_->setValue("library/recent",recent); emit preferencesChanged(); }
        return commit(edit,"设计已更新");
    }
    if (op == "plugin.export") {
        // Reading a project file does not need a live session. A project that
        // cannot start on this machine still carries its embedded packages, so
        // exporting them must not depend on opening it first.
        if (args.contains("project")) {
            auto projectFile = args.value("project").toString(); if (QUrl(projectFile).isLocalFile()) projectFile = QUrl(projectFile).toLocalFile();
            if (projectFile.isEmpty()) throw Error("请选择工程文件");
            auto directory = args.value("directory").toString(); if (QUrl(directory).isLocalFile()) directory = QUrl(directory).toLocalFile();
            if (directory.isEmpty()) throw Error("请选择插件导出目录");
            const auto document = loadProject(projectFile);
            const auto embedded = document.value("plugin_packages").toList();
            if (embedded.isEmpty()) throw Error("工程没有携带插件包：" + projectFile);
            const auto requested = args.value("plugins").toStringList();
            QList<QPair<PluginPackage,QString>> selected; QVariantList conflicts; QSet<QString> seen;
            for (const auto &entry : embedded) {
                const auto item = entry.toMap(); const auto id = item.value("id").toString();
                if (!requested.isEmpty() && !requested.contains(id)) continue;
                if (seen.contains(id)) continue; seen.insert(id);
                const auto package = PluginPackage::read(item.value("data").toByteArray());
                if (package.id() != id || package.hash() != item.value("sha256").toString()) throw Error("工程内插件包标识或校验值不一致：" + id);
                const auto path = QDir(directory).filePath(PluginPackage::checkedPath(item.value("relative_path",id + ".crpl").toString()));
                if (QFileInfo::exists(path)) conflicts.append(path);
                selected.append({package,path});
            }
            for (const auto &id : requested) if (!seen.contains(id)) throw Error("工程没有携带此插件：" + id);
            if (selected.isEmpty()) throw Error("工程没有携带所选插件：" + projectFile);
            if (!conflicts.isEmpty() && !args.value("overwrite").toBool()) return QVariantMap{{"requires_confirmation",true},{"conflicts",conflicts}};
            return launchTask(op,[selected,projectFile] {
                QVariantList written;
                for (const auto &entry : selected) { try { entry.first.save(entry.second); written.append(entry.second); } catch (const Error &error) { throw Error(QString("已导出 %1 个插件；%2").arg(written.size()).arg(error.message)); } }
                return QVariantMap{{"project",projectFile},{"paths",written},{"count",written.size()}};
            });
        }
        if (args.contains("plugins")) {
            auto directory = args.value("directory").toString(); if (QUrl(directory).isLocalFile()) directory = QUrl(directory).toLocalFile();
            if (directory.isEmpty()) throw Error("请选择插件导出目录");
            const auto inventory = plugins_->tree(project_,args.value("scope","project").toString()).value("plugins").toList();
            QMap<QString,QString> paths;
            for (const auto &entry : inventory) paths.insert(entry.toMap().value("id").toString(),entry.toMap().value("relative_path").toString());
            QList<QPair<PluginPackage,QString>> selected; QVariantList conflicts; QSet<QString> seen;
            for (const auto &entry : args.value("plugins").toList()) {
                const auto id = entry.toString(); if (seen.contains(id)) continue; seen.insert(id);
                if (!paths.contains(id)) throw Error("导出选择不属于当前工程：" + id);
                const auto path = QDir(directory).filePath(PluginPackage::checkedPath(paths.value(id)));
                if (QFileInfo::exists(path)) conflicts.append(path);
                selected.append({plugins_->find(id,args.value("scope")=="library" ? emptyProject() : project_),path});
            }
            if (selected.isEmpty()) throw Error("请先勾选要导出的插件");
            if (!conflicts.isEmpty() && !args.value("overwrite").toBool()) return QVariantMap{{"requires_confirmation",true},{"conflicts",conflicts}};
            return launchTask(op,[selected] {
                QVariantList written;
                for (const auto &entry : selected) { try { entry.first.save(entry.second); written.append(entry.second); } catch (const Error &error) { throw Error(QString("已导出 %1 个插件；%2").arg(written.size()).arg(error.message)); } }
                return QVariantMap{{"paths",written},{"count",written.size()}};
            });
        }
        auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
        if (path.isEmpty()) throw Error("请选择插件导出路径");
        if (!path.endsWith(".crpl",Qt::CaseInsensitive)) path += ".crpl";
        const auto package = plugins_->find(args.value("plugin").toString(),args.value("scope")=="library" ? emptyProject() : project_);
        return launchTask(op,[package,path] { package.save(path); return QVariantMap{{"path",path},{"id",package.id()},{"version",package.version()}}; });
    }
    if (op == "plugin.import" || op == "plugin.remove" || op == "plugin.enable") {
        requireEditable();
        if (op == "plugin.import") {
            auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
            const auto package = args.contains("manifest") ? PluginPackage::fromPython(fromJsonValue(args.value("manifest")).toMap())
                : path.endsWith(".crpl",Qt::CaseInsensitive) ? PluginPackage::open(path) : PluginPackage::fromPython(importPlugin(path));
            const auto installed = plugins_->install(package,path);
            if (installed.value("restart_required").toBool()) { emit pluginsChanged(); tell("插件更新已保存，重启 CoRig 后生效"); return installed; }
        } else if (op == "plugin.enable") {
            if (args.contains("plugin") == args.contains("plugins")) throw Error("请提供 plugin 或 plugins 其中一个");
            const auto ids = args.contains("plugins") ? args.value("plugins").toStringList() : QStringList{args.value("plugin").toString()};
            plugins_->setEnabled(ids,args.value("enabled").toBool(),project_);
        }
        else {
            const auto id = args.value("package",args.value("plugin")).toString();
            const auto package = plugins_->find(id,project_); plugins_->remove(package.id(),project_);
        }
        const auto design = project_; const bool wasDirty = dirty_;
        return launchEngine("project.apply",{{"project",design}},[this,design,wasDirty](const QVariant &,const QString &error) {
            if (error.isEmpty()) { adopt(design,wasDirty); tell("插件库已更新"); } emit pluginsChanged();
        });
    }
    if (op == "library.state") return libraryPreferences();
    if (op == "library.search") return catalogEntries(args.value("query").toString(),args.value("category").toStringList(),args.value("scope","all").toString());
    if (op == "library.preview") {
        const auto scope = args.value("scope","project").toString();
        if (scope != "project" && scope != "library") throw Error("预览范围需要 project 或 library");
        // Local management must never substitute an embedded project version
        // or hide a disabled node. Previewing does not create an engine node.
        const auto selected = scope == "library" ? plugins_->catalog({},true) : catalog_;
        const auto plugin = args.value("plugin").toString(); const auto spec = selected.definition(plugin);
        QVariantMap result{{"definition",spec},{"config",checkedProperties(spec.value("properties").toMap(),{})}};
        try {
            const auto config = selected.checkedConfig(plugin,{}); result["config"] = config;
            if (spec.value("kind","node") == "node") result["geometry"] = nodeGeometry({{"plugin",plugin},{"config",config}},selected);
            else result["geometry"] = QVariantMap{{"width",180},{"height",104},{"kind",spec.value("kind")},{"ports",QVariantList{}},{"center",QVariantList{0,0,180,104}}};
        } catch (const Error &error) {
            // A native version already loaded by the project can require a
            // restart. Keep its installed description and QML inspectable.
            result["geometry"] = QVariantMap{{"width",180},{"height",104},{"ports",QVariantList{}},
                {"center",QVariantList{0,0,180,104}},{"error",error.message}};
        }
        if (!spec.value("qml").toString().isEmpty()) {
            const auto stored = selected.package(plugin); const auto file = spec.value("qml").toString(); stored.file(file);
            result["presentation_url"] = QUrl::fromLocalFile(QDir(stored.materialize()).filePath(file)).toString();
        }
        QVariantMap urls; const auto views = spec.value("views").toMap();
        if (!views.isEmpty()) {
            const auto package = selected.package(plugin); const QDir folder(package.materialize());
            for (auto i = views.cbegin(); i != views.cend(); ++i)
                urls.insert(i.key(),QUrl::fromLocalFile(folder.filePath(i.value().toMap().value("qml").toString())).toString());
        }
        result["view_urls"] = urls; return result;
    }
    if (op == "library.favorite") {
        const auto id = args.value("plugin").toString(); catalog_.definition(id); auto items = settings_->value("library/favorites").toStringList(); items.removeAll(id); if (args.value("enabled").toBool()) items.append(id); settings_->setValue("library/favorites",items); emit preferencesChanged(); return libraryPreferences();
    }
    if (op == "library.configure") {
        if (args.contains("view")) { const auto view = args.value("view").toString(); if (view != "grid" && view != "list") throw Error("浏览方式需要 grid 或 list"); settings_->setValue("library/view",view); }
        if (args.contains("size")) settings_->setValue("library/size",std::clamp(finiteNumber(args.value("size"),"预览比例"),.6,2.));
        emit preferencesChanged(); return libraryPreferences();
    }
    if (op.startsWith("workspace.") || op.startsWith("window.")) {
        if (!workspaces_) throw Error("工作区尚未建立"); const auto page = args.value("page").toString();
        if (op == "workspace.open") return workspaces_->open(page,args.value("window").toString(),args.value("activate",false).toBool());
        if (op == "workspace.close") return workspaces_->closePage(page,args.value("discard").toBool());
        if (op == "workspace.detach") return workspaces_->detach(page,{args.value("x").toInt(),args.value("y").toInt()});
        if (op == "workspace.move") return workspaces_->movePage(page,args.value("window").toString(),args.value("index",-1).toInt());
        if (op == "window.pin") return workspaces_->pinWindow(args.value("window").toString(),args.value("enabled").toBool());
        return QVariantMap{{"closed",workspaces_->closeWindow(args.value("window").toString(),args.value("discard").toBool())}};
    }
    if (op == "publish.start") {
        if (publishMode_) throw Error("发布运行模式不能再次导出软件");
        const auto started = startSoftwareExport(args);
        if (started.contains("error")) throw Error(started.value("error").toString());
        return started;
    }
    if (op == "publish.state") {
        auto result = softwareExport_;
        if (!softwareExportJobId_.isEmpty()) result[QStringLiteral("job_state")] = jobs_.value(softwareExportJobId_).value(QStringLiteral("state"));
        if (result.isEmpty()) result = QVariantMap{{QStringLiteral("state"),QStringLiteral("idle")}};
        return result;
    }
    if (op == "publish.cancel") {
        const auto requested = args.value("job").toString();
        if (!softwareExportProcess_) throw Error("没有正在进行的发布任务");
        if (!requested.isEmpty() && requested != softwareExportJobId_) throw Error("发布任务不匹配：" + requested);
        cancelSoftwareExport();
        return QVariantMap{{QStringLiteral("job"),softwareExportJobId_},{QStringLiteral("state"),QStringLiteral("cancelling")}};
    }
    if (op == "publish.log") return publishLog();
    if (op == "capture") return capture(args);
    if (op.startsWith("controls.")) return controls(op,args);
    if (op == "view.actions") return viewActions(args.value("node").toString(),args.value("view").toString());
    if (op == "view.call") return viewCall(args);
    if (op == "runtime.start") {
        if (busy_) throw Error("请等待工程操作完成");
        return launchEngine(op,args);
    }
    // Reset restores the document's initial design, including parameters that
    // were overridden only for this run in read-only mode.
    if (op == "runtime.reset") return launchEngine(op,{{"project",project_}});
    if (op.startsWith("runtime.") || op.startsWith("log.") || op == "node.state" || op == "node.action" || op == "view.read" || op == "python.probe") {
        auto normalized = args;
        // With no explicit observation arguments, inspect the same range/FFT
        // settings currently displayed by the user's view.
        if (op == "view.read" && !args.contains("args")) {
            const auto key = args.value("node").toString() + "/" + args.value("view").toString();
            if (viewArguments_.contains(key)) normalized["args"] = viewArguments_.value(key);
        }
        if (normalized.contains("path") && QUrl(normalized.value("path").toString()).isLocalFile()) normalized["path"] = QUrl(normalized.value("path").toString()).toLocalFile();
        return launchEngine(op,normalized);
    }
    throw Error("操作尚未提供处理入口：" + op);
}

namespace {
// Built-in views execute these actions in their own QML call(). The table is
// the single source for view.actions discovery and view.call routing: an action
// cannot be advertised without being dispatched to the view, or dispatched
// without being discoverable. requires_view marks actions that need a loaded
// view instance; range and fft also update the polled arguments without one.
QVariantList builtinViewActions(const QString &kind)
{
    const auto camera = [] { return QVariantMap{
        {"description","调整方位、俯仰和缩放"},{"requires_view",true},{"args",QVariantMap{
            {"azimuth",QVariantMap{{"type","number"}}},{"elevation",QVariantMap{{"type","number"}}},
            {"distance",QVariantMap{{"type","number"}}},{"pan",QVariantMap{{"type","array"}}}}}}; };
    const auto exportImage = [](const QString &description) { return QVariantMap{
        {"description",description},{"requires_view",true},{"args",QVariantMap{
            {"path",QVariantMap{{"type","string"}}},{"width",QVariantMap{{"type","integer"}}},
            {"height",QVariantMap{{"type","integer"}}}}}}; };
    const auto action = [](const QString &name, const QVariantMap &contract) {
        QVariantMap entry{{"action",name}}; for (auto i = contract.cbegin(); i != contract.cend(); ++i) entry[i.key()] = i.value(); return entry; };
    if (kind == "plot.curves") return {
        action("range",{{"description","设置坐标范围与跟随"},{"requires_view",false},{"args",QVariantMap{
            {"follow",QVariantMap{{"type","boolean"}}},{"fft",QVariantMap{{"type","boolean"}}},
            {"x_min",QVariantMap{{"type","number"}}},{"x_max",QVariantMap{{"type","number"}}},
            {"y_min",QVariantMap{{"type","number"}}},{"y_max",QVariantMap{{"type","number"}}}}}}),
        action("fft",{{"description","切换频谱"},{"requires_view",false},{"args",QVariantMap{
            {"enabled",QVariantMap{{"type","boolean"}}},{"follow",QVariantMap{{"type","boolean"}}}}}}),
        action("export_image",exportImage("导出 PNG 或 SVG"))};
    if (kind == "plot.attitude") return {
        action("camera",camera()), action("reset_camera",{{"description","恢复相机"},{"requires_view",true}}),
        action("export_image",exportImage("导出 PNG"))};
    if (kind == "plot.sixdof") return {
        action("camera",camera()), action("reset_camera",{{"description","恢复相机"},{"requires_view",true}}),
        // Restores the view's rendering defaults; saved appearance is untouched.
        action("restore_defaults",{{"description","恢复相机与渲染默认值（不修改已保存外观）"},{"requires_view",true}}),
        action("export_image",exportImage("导出 PNG"))};
    return {};
}
} // namespace

QVariantList Controller::viewActions(const QString &node, const QString &view) const
{
    const auto design = nodeDesign(node); if (design.isEmpty()) throw Error("节点不存在");
    const auto kind = design.value("plugin").toString(); QVariantList actions = builtinViewActions(kind);
    const auto declared = definition(kind).value("actions").toMap();
    for (auto i = declared.cbegin(); i != declared.cend(); ++i) actions.append(QVariantMap{{"action",i.key()},{"contract",i.value()}});
    Q_UNUSED(view); return actions;
}

QVariant Controller::viewCall(const QVariantMap &args)
{
    const auto node = args.value("node").toString(), view = args.value("view").toString(), action = args.value("action").toString();
    const auto arguments = args.value("args").toMap(); const auto key = node + "/" + view;
    bool viewOwned = false;
    for (const auto &entry : builtinViewActions(nodeDesign(node).value("plugin").toString()))
        if (entry.toMap().value("action").toString() == action) viewOwned = true;
    if (viewOwned && (action == "range" || action == "fft")) {
        for (const auto &candidate : viewItems_.value(key)) if (candidate && candidate->property("visible").toBool()) {
            QVariant result;
            if (QMetaObject::invokeMethod(candidate,"viewAction",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant(action)),Q_ARG(QVariant,QVariant(arguments)))) return result;
        }
        auto options = viewArguments_.value(key);
        for (auto i = arguments.cbegin(); i != arguments.cend(); ++i) options[i.key()] = i.value();
        if (action == "fft") options["fft"] = arguments.value("enabled",true);
        observeView(node,view,options); return QVariantMap{{"updated",true}};
    }
    if (viewOwned) {
        QPointer<QObject> item;
        for (const auto &candidate : viewItems_.value(key)) if (candidate && candidate->property("visible").toBool()) { item = candidate; break; }
        if (!item) throw Error("请先打开目标视图");
        QVariant result;
        if (!QMetaObject::invokeMethod(item,"viewAction",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QVariant(action)),Q_ARG(QVariant,QVariant(arguments)))) throw Error("目标视图不支持该操作");
        return result;
    }
    return launchEngine("node.action",{{"node",node},{"action",action},{"args",arguments}});
}

QVariantMap Controller::capture(const QVariantMap &args)
{
    if (!workspaces_) throw Error("界面尚未打开");
    const auto target = args.value("target","window").toString();
    if (target.isEmpty()) throw Error("截图目标不能为空");
    const auto requested = QSize(args.value("width",0).toInt(),args.value("height",0).toInt());
    if ((requested.width() && (requested.width() < 1 || requested.width() > 8192)) ||
        (requested.height() && (requested.height() < 1 || requested.height() > 8192)))
        throw Error("截图尺寸需要在 1～8192 之间");
    const auto dependency = args.value("job").toString();
    if (!dependency.isEmpty() && !jobs_.contains(dependency)) throw Error("前置任务不存在：" + dependency);
    auto path = args.value("path").toString(); if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
    if (path.isEmpty()) { QDir().mkpath(dataDirectory_ + "/captures"); path = dataDirectory_ + "/captures/" + newId(target) + ".png"; }
    const auto parent = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent)) throw Error("无法创建截图目录：" + parent);

    const auto id = newId("job");
    auto state = QSharedPointer<CaptureState>::create();
    state->arguments = args;
    state->target = target;
    state->path = path;
    state->requested = requested;
    state->requestObservation = QVariantMap{{"batch",runtime_.value("batch")},{"epoch",runtime_.value("epoch")}};
    jobs_[id] = {{"id",id},{"op","capture"},{"state","running"}};
    captures_.insert(id,state);

    state->deadline = new QTimer(this);
    state->deadline->setSingleShot(true);
    connect(state->deadline,&QTimer::timeout,this,[this,id] {
        finishCapture(id,{},QStringLiteral("截图超时（30 秒）"));
    });
    state->deadline->start(30000);

    startCapture(id);
    const auto current = jobs_.value(id);
    return QVariantMap{{"job",id},{"state",current.value("state")},{"error",current.value("error")}};
}

void Controller::startCapture(const QString &id)
{
    auto state = captures_.value(id); if (!state || state->done) return;
    const auto dependency = state->arguments.value("job").toString();
    if (dependency.isEmpty()) { captureStep(id); return; }
    const auto dependencyJob = jobs_.value(dependency);
    const auto dependencyState = dependencyJob.value("state").toString();
    if (dependencyState == "failed" || dependencyState == "cancelled" || dependencyState == "missing") {
        finishCapture(id,{},QStringLiteral("前置任务未完成：") + dependencyJob.value("error").toString());
        return;
    }
    if (dependencyState == "complete") { state->arguments.remove("job"); captureStep(id); return; }
    if (dependencyState != "running") { finishCapture(id,{},QStringLiteral("前置任务状态未知：")); return; }
    // Waiting for a prior business task is one asynchronous step; the same
    // deadline covers it and the subsequent page load/render.
    state->dependency = connect(this,&Controller::jobFinished,this,[this,id,dependency](const QString &finished) {
        if (finished != dependency) return;
        auto current = captures_.value(id); if (!current || current->done) return;
        disconnect(current->dependency); current->dependency = {};
        const auto result = jobs_.value(dependency);
        if (result.value("state").toString() != "complete") {
            finishCapture(id,{},QStringLiteral("前置任务失败：") + result.value("error").toString());
            return;
        }
        current->arguments.remove("job");
        captureStep(id);
    });
}

void Controller::captureStep(const QString &id)
{
    auto state = captures_.value(id); if (!state || state->done) return;
    if (jobs_.value(id).value("state").toString() != "running") return;
    const auto retry = [this,id] {
        auto current = captures_.value(id);
        if (current && !current->done)
            QTimer::singleShot(16,this,[this,id] { captureStep(id); });
    };

    const auto target = state->target;
    QQuickItem *item = nullptr;
    QQuickWindow *window = nullptr;
    if (target == "window") {
        const auto requestedWindow = state->arguments.value("window").toString();
        if (!requestedWindow.isEmpty()) {
            window = workspaces_->window(requestedWindow);
            if (!window) { finishCapture(id,{},QStringLiteral("窗口不存在：") + requestedWindow); return; }
        } else {
            const auto windows = workspaces_->nativeWindows();
            if (windows.isEmpty()) { retry(); return; }
            window = windows.first();
        }
        if (window) item = window->contentItem();
    } else if (target.startsWith("view.")) {
        const auto key = target.mid(5);
        const auto separator = key.lastIndexOf('.');
        if (separator < 0) { finishCapture(id,{},QStringLiteral("截图视图目标不合法：")); return; }
        const auto viewKey = key.left(separator) + "/" + key.mid(separator + 1);
        for (const auto &candidate : viewItems_.value(viewKey))
            if (candidate && candidate->property("visible").toBool()) { item = qobject_cast<QQuickItem *>(candidate); break; }
        if (!item) { retry(); return; }
        window = item->window();
    } else {
        if (!state->opened) {
            QVariantMap opened;
            try { opened = workspaces_->open(target,{},false); }
            catch (const std::exception &error) {
                finishCapture(id,{},QString::fromUtf8(error.what()));
                return;
            }
            if (opened.contains("drafts") || opened.value("requires_draft_resolution").toBool()) {
                finishCapture(id,{},QStringLiteral("截图目标有未处理的草稿"));
                return;
            }
            state->opened = true;
            // The page is created now, but its QML tree needs one event-loop
            // turn to install. Do not call processEvents while pointers are live.
            QTimer::singleShot(0,this,[this,id] { captureStep(id); });
            return;
        }
        item = workspaces_->page(target);
        if (!item) { retry(); return; }
        window = item->window();
        if (!window) { retry(); return; }
        if (!state->pageTurn) {
            state->pageTurn = true;
            QTimer::singleShot(0,this,[this,id] { captureStep(id); });
            return;
        }
    }
    if (!item || !window) { retry(); return; }

    // Revalidate ownership after every event-loop turn. A window can close or a
    // project switch can delete the page while a capture is pending.
    state->window = window;
    state->item = item;
    const auto windowId = window->property("hostId").toString();
    const auto metadata = [this,state](const QImage &image, const QString &id) {
        const auto completion = QVariantMap{{"batch",runtime_.value("batch")},{"epoch",runtime_.value("epoch")}};
        return QVariantMap{
            {"target",state->target},{"window",id},{"path",state->path},
            {"width",image.width()},{"height",image.height()},
            {"captured_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"observation",state->requestObservation},
            {"observation_scope","request"},
            {"request_observation",state->requestObservation},
            {"completion_observation",completion},
            {"batch_associated",false},
            {"single_batch",false}
        };
    };

    if (!window->isExposed()) {
        auto image = window->grabWindow();
        if (image.isNull()) { finishCapture(id,{},QStringLiteral("窗口最小化且无法生成最新画面")); return; }
        if (target != "window") {
            const auto dpr = window->devicePixelRatio();
            const auto scene = item->mapRectToScene(item->boundingRect());
            const auto rect = QRect(qRound(scene.x() * dpr),qRound(scene.y() * dpr),
                                    qRound(scene.width() * dpr),qRound(scene.height() * dpr)).intersected(image.rect());
            if (rect.isEmpty()) { finishCapture(id,{},QStringLiteral("截图目标没有可渲染区域")); return; }
            image = image.copy(rect);
        }
        if (!state->requested.isEmpty()) image = image.scaled(state->requested,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        if (!image.save(state->path)) { finishCapture(id,{},QStringLiteral("截图保存失败")); return; }
        finishCapture(id,metadata(image,windowId),{});
        return;
    }

    const auto size = !state->requested.isEmpty() ? state->requested : QSize(item->width(),item->height());
    if (size.width() < 1 || size.height() < 1) { finishCapture(id,{},QStringLiteral("截图尺寸需要在 1～8192 之间")); return; }
    const auto grab = item->grabToImage(size);
    if (!grab) { finishCapture(id,{},QStringLiteral("无法启动内部截图")); return; }
    state->grab = grab;
    state->ready = connect(grab.data(),&QQuickItemGrabResult::ready,this,[this,id,metadata] {
        auto current = captures_.value(id); if (!current || current->done || !current->grab) return;
        const auto image = current->grab->image();
        if (image.isNull()) { finishCapture(id,{},QStringLiteral("截图结果为空")); return; }
        const auto window = current->window;
        if (!window) { finishCapture(id,{},QStringLiteral("截图窗口已关闭")); return; }
        const auto saved = current->grab->saveToFile(current->path);
        finishCapture(id,metadata(image,window->property("hostId").toString()),saved ? QString{} : QStringLiteral("截图保存失败"));
    });
    state->destroyed = connect(grab.data(),&QObject::destroyed,this,[this,id] {
        finishCapture(id,{},QStringLiteral("截图目标已关闭"));
    });
    window->update();
}

void Controller::finishCapture(const QString &id, const QVariant &result, const QString &error)
{
    auto state = captures_.take(id);
    if (!state || state->done) return;
    state->done = true;
    if (state->deadline) { state->deadline->stop(); state->deadline->deleteLater(); state->deadline = nullptr; }
    if (state->dependency) { disconnect(state->dependency); state->dependency = {}; }
    if (state->ready) { disconnect(state->ready); state->ready = {}; }
    if (state->destroyed) { disconnect(state->destroyed); state->destroyed = {}; }
    state->grab.clear();
    if (jobs_.contains(id)) finishJob(id,result,error);
}


} // namespace corig
