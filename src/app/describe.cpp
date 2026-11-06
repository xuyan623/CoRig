// CoRig AI self-description (W2: ai-api).
#include "describe.h"
#include "command_registry.h"
#include "controller.h"
#include "core/run_mode.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QStringList>

namespace corig {
namespace {

QString nonEmpty(const QString &value, const QString &fallback)
{
    return value.isEmpty() ? fallback : value;
}

QStringList declaredViews(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) return value.toMap().keys();
    return value.toStringList();
}

QVariantMap rangeOf(const QVariantMap &spec)
{
    QVariantMap range;
    if (spec.contains(QStringLiteral("min"))) range.insert(QStringLiteral("min"), spec.value(QStringLiteral("min")));
    if (spec.contains(QStringLiteral("max"))) range.insert(QStringLiteral("max"), spec.value(QStringLiteral("max")));
    return range;
}

QString effectOf(const QVariantMap &spec)
{
    const auto declared = spec.value(QStringLiteral("effect")).toString();
    if (!declared.isEmpty()) return declared;
    // runtime_editable is the catalog's existing declaration for a parameter
    // that may change while the engine is live.  Everything else is applied to
    // the design while paused and observed on the next run.
    return spec.value(QStringLiteral("runtime_editable"), false).toBool()
        ? QStringLiteral("immediate") : QStringLiteral("on_pause");
}

QVariantMap readNodeState(const QString &node)
{
    return QVariantMap{{QStringLiteral("op"), QStringLiteral("node.state")},
        {QStringLiteral("args"), QVariantMap{{QStringLiteral("node"), node}, {QStringLiteral("detailed"), true}}}};
}

QVariantMap readView(const QString &node, const QString &view)
{
    return QVariantMap{{QStringLiteral("op"), QStringLiteral("view.read")},
        {QStringLiteral("args"), QVariantMap{{QStringLiteral("node"), node}, {QStringLiteral("view"), view}}}};
}

QVariantMap capabilitiesFor(const DescribeContext &context)
{
    QVariantList parameters, metrics, actions, views, exports;
    QHash<QString, QVariantMap> definitions;
    for (const auto &entry : context.registry) {
        const auto definition = entry.toMap();
        const auto id = definition.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) definitions.insert(id, definition);
    }

    QVariantList nodes = context.project.value(QStringLiteral("nodes")).toList();
    if (nodes.isEmpty()) nodes = context.graphNodes;
    const auto runtimeNodes = context.runtime.value(QStringLiteral("nodes")).toMap();
    const bool runtimeActive = context.runtime.value(QStringLiteral("active")).toBool();
    const bool running = context.runtime.value(QStringLiteral("running")).toBool();

    for (const auto &entry : nodes) {
        const auto node = entry.toMap();
        if (!node.value(QStringLiteral("placed"), true).toBool()) continue;
        const auto nodeId = node.value(QStringLiteral("id")).toString();
        const auto plugin = node.value(QStringLiteral("plugin")).toString();
        if (nodeId.isEmpty() || plugin.isEmpty()) continue;
        const auto definition = definitions.value(plugin);
        if (definition.value(QStringLiteral("kind"), QStringLiteral("node")).toString() != QStringLiteral("node")) continue;

        const auto live = runtimeNodes.value(nodeId).toMap();
        QVariantMap config = node.value(QStringLiteral("config")).toMap();
        const auto liveConfig = live.value(QStringLiteral("config")).toMap();
        for (auto i = liveConfig.cbegin(); i != liveConfig.cend(); ++i) config[i.key()] = i.value();

        const auto properties = definition.value(QStringLiteral("properties")).toMap();
        for (auto i = properties.cbegin(); i != properties.cend(); ++i) {
            const auto name = i.key();
            const auto spec = i.value().toMap();
            // runtime=false marks a declaration that is not part of the running
            // application's capability surface.  Absent means runtime-visible.
            if (spec.contains(QStringLiteral("runtime")) && !spec.value(QStringLiteral("runtime")).toBool()) continue;
            const bool secret = spec.value(QStringLiteral("secret")).toBool();
            const bool writable = context.writable && !secret
                && spec.value(QStringLiteral("writable"), true).toBool();
            QVariantMap parameter{
                {QStringLiteral("node"), nodeId},
                {QStringLiteral("plugin"), plugin},
                {QStringLiteral("name"), name},
                {QStringLiteral("label"), nonEmpty(spec.value(QStringLiteral("label")).toString(), name)},
                {QStringLiteral("type"), nonEmpty(spec.value(QStringLiteral("type")).toString(), QStringLiteral("unknown"))},
                {QStringLiteral("unit"), spec.value(QStringLiteral("unit")).toString()},
                {QStringLiteral("range"), rangeOf(spec)},
                {QStringLiteral("writable"), writable},
                {QStringLiteral("effect"), effectOf(spec)},
            };
            if (spec.contains(QStringLiteral("choices"))) parameter.insert(QStringLiteral("choices"), spec.value(QStringLiteral("choices")));
            if (spec.contains(QStringLiteral("step"))) parameter.insert(QStringLiteral("step"), spec.value(QStringLiteral("step")));
            if (secret) {
                // A secret declaration is discoverable, but its default and
                // current value are never projected into an AI-readable document.
                parameter.insert(QStringLiteral("secret"), true);
                parameter.insert(QStringLiteral("read"), QVariantMap{});
                parameter.insert(QStringLiteral("write"), QVariantMap{});
            } else {
                if (spec.contains(QStringLiteral("default"))) parameter.insert(QStringLiteral("default"), spec.value(QStringLiteral("default")));
                if (config.contains(name)) parameter.insert(QStringLiteral("current"), config.value(name));
                else if (spec.contains(QStringLiteral("default"))) parameter.insert(QStringLiteral("current"), spec.value(QStringLiteral("default")));
                parameter.insert(QStringLiteral("read"), readNodeState(nodeId));
                QVariantMap changes;
                if (parameter.contains(QStringLiteral("current"))) changes.insert(name, parameter.value(QStringLiteral("current")));
                parameter.insert(QStringLiteral("write"), QVariantMap{
                    {QStringLiteral("op"), QStringLiteral("node.configure")},
                    {QStringLiteral("args"), QVariantMap{{QStringLiteral("node"), nodeId}, {QStringLiteral("changes"), changes}}}});
            }
            parameters.append(parameter);
        }

        QSet<QString> outputPorts;
        const auto outputs = live.value(QStringLiteral("outputs")).toMap();
        const auto addPort = [&](const QString &port, const QString &type, const QString &unit, const QString &label) {
            outputPorts.insert(port);
            QVariantMap metric{
                {QStringLiteral("node"), nodeId},
                {QStringLiteral("plugin"), plugin},
                {QStringLiteral("kind"), QStringLiteral("port")},
                {QStringLiteral("port"), port},
                {QStringLiteral("label"), nonEmpty(label, port)},
                {QStringLiteral("type"), nonEmpty(type, QStringLiteral("unknown"))},
                {QStringLiteral("unit"), unit},
                {QStringLiteral("read"), readNodeState(nodeId)},
                {QStringLiteral("path"), QStringLiteral("nodes.%1.outputs.%2").arg(nodeId, port)},
            };
            if (outputs.contains(port)) metric.insert(QStringLiteral("value"), outputs.value(port));
            metrics.append(metric);
        };
        for (const auto &portEntry : definition.value(QStringLiteral("ports")).toList()) {
            const auto port = portEntry.toMap();
            if (port.value(QStringLiteral("direction")).toString() != QStringLiteral("out")) continue;
            addPort(port.value(QStringLiteral("id")).toString(), port.value(QStringLiteral("data_type")).toString(),
                    port.value(QStringLiteral("unit")).toString(), port.value(QStringLiteral("label")).toString());
        }
        for (auto i = outputs.cbegin(); i != outputs.cend(); ++i)
            if (!outputPorts.contains(i.key())) addPort(i.key(), QStringLiteral("unknown"), QString{}, i.key());

        const auto viewIds = declaredViews(definition.value(QStringLiteral("views")));
        for (const auto &view : viewIds) {
            metrics.append(QVariantMap{
                {QStringLiteral("node"), nodeId},
                {QStringLiteral("plugin"), plugin},
                {QStringLiteral("kind"), QStringLiteral("view")},
                {QStringLiteral("view"), view},
                {QStringLiteral("label"), view},
                {QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("unit"), QString{}},
                {QStringLiteral("read"), readView(nodeId, view)},
                {QStringLiteral("path"), QStringLiteral("views.%1.%2").arg(nodeId, view)},
            });
            views.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("node_view")},
                {QStringLiteral("node"), nodeId},
                {QStringLiteral("plugin"), plugin},
                {QStringLiteral("view"), view},
                {QStringLiteral("read"), readView(nodeId, view)},
                {QStringLiteral("actions"), QVariantMap{
                    {QStringLiteral("op"), QStringLiteral("view.actions")},
                    {QStringLiteral("args"), QVariantMap{{QStringLiteral("node"), nodeId}, {QStringLiteral("view"), view}}}}},
            });
        }

        const auto declaredActions = definition.value(QStringLiteral("actions")).toMap();
        for (auto i = declaredActions.cbegin(); i != declaredActions.cend(); ++i) {
            const auto spec = i.value().toMap();
            const bool requiresRunning = spec.value(QStringLiteral("requires_running"), true).toBool();
            const bool requiresPaused = spec.value(QStringLiteral("requires_paused"), false).toBool();
            bool available = true;
            QString reason;
            if (requiresRunning && !runtimeActive) { available = false; reason = QStringLiteral("运行会话尚未建立"); }
            else if (requiresPaused && running) { available = false; reason = QStringLiteral("动作需要在暂停状态执行"); }
            QVariantMap action{
                {QStringLiteral("node"), nodeId},
                {QStringLiteral("plugin"), plugin},
                {QStringLiteral("action"), i.key()},
                {QStringLiteral("description"), spec.value(QStringLiteral("description")).toString()},
                {QStringLiteral("args"), spec.value(QStringLiteral("args")).toMap()},
                {QStringLiteral("async"), spec.value(QStringLiteral("async")).toBool()},
                {QStringLiteral("requires_running"), requiresRunning},
                {QStringLiteral("requires_paused"), requiresPaused},
                {QStringLiteral("immediate"), spec.value(QStringLiteral("immediate")).toBool()},
                {QStringLiteral("available"), available},
                {QStringLiteral("call"), QVariantMap{
                    {QStringLiteral("op"), QStringLiteral("node.action")},
                    {QStringLiteral("args"), QVariantMap{{QStringLiteral("node"), nodeId},
                        {QStringLiteral("action"), i.key()}, {QStringLiteral("args"), QVariantMap{}}}}}},
            };
            if (!available) action.insert(QStringLiteral("reason"), reason);
            actions.append(action);
        }
    }

    const auto workspaces = context.project.value(QStringLiteral("workspaces")).toMap();
    for (auto i = workspaces.cbegin(); i != workspaces.cend(); ++i) {
        views.append(QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("workspace")},
            {QStringLiteral("workspace"), i.key()},
            {QStringLiteral("plugin"), i.key()},
            {QStringLiteral("list"), QVariantMap{{QStringLiteral("op"), QStringLiteral("workspace.list")},
                {QStringLiteral("args"), QVariantMap{}}}},
        });
    }

    if (context.retainOriginal) {
        const bool available = context.commands.isEmpty()
            ? CommandRegistry::instance().contains(QStringLiteral("project.export_original"))
            : context.commands.contains(QStringLiteral("project.export_original"));
        QVariantMap exportEntry{
            {QStringLiteral("op"), QStringLiteral("project.export_original")},
            {QStringLiteral("args"), QVariantMap{}},
            {QStringLiteral("description"), QStringLiteral("导出发布时保留的原始工程")},
            {QStringLiteral("available"), available},
        };
        if (!available) exportEntry.insert(QStringLiteral("reason"), QStringLiteral("命令尚未接入"));
        exports.append(exportEntry);
    }

    return QVariantMap{
        {QStringLiteral("parameters"), parameters},
        {QStringLiteral("metrics"), metrics},
        {QStringLiteral("actions"), actions},
        {QStringLiteral("views"), views},
        {QStringLiteral("exports"), exports},
    };
}

QVariantMap sanitizedInstance(const QVariantMap &source)
{
    return QVariantMap{
        {QStringLiteral("instance_id"), source.value(QStringLiteral("instance_id")).toString()},
        {QStringLiteral("pid"), source.value(QStringLiteral("pid")).toLongLong()},
        {QStringLiteral("url"), source.value(QStringLiteral("url")).toString()},
        {QStringLiteral("started_at"), source.value(QStringLiteral("started_at")).toDouble()},
        {QStringLiteral("data_dir"), source.value(QStringLiteral("data_dir")).toString()},
    };
}

} // namespace

QVariantMap buildDescribe(const DescribeContext &context)
{
    const auto appName = nonEmpty(context.appName, QStringLiteral("CoRig"));
    const auto appVersion = nonEmpty(context.appVersion, QStringLiteral("0.4.4"));
    return QVariantMap{
        {QStringLiteral("app"), QVariantMap{
            {QStringLiteral("name"), appName},
            {QStringLiteral("id"), context.appId},
            {QStringLiteral("version"), appVersion},
            {QStringLiteral("release_hash"), context.releaseHash},
            {QStringLiteral("mode"), nonEmpty(context.appMode, QStringLiteral("editor"))},
        }},
        {QStringLiteral("instance"), sanitizedInstance(context.instance)},
        {QStringLiteral("policy"), QVariantMap{
            {QStringLiteral("retain_original"), context.retainOriginal},
            {QStringLiteral("editable"), false},
        }},
        {QStringLiteral("schema"), QVariantMap{
            {QStringLiteral("version"), context.schemaVersion},
            {QStringLiteral("digest"), context.schemaDigest},
        }},
        {QStringLiteral("capabilities"), capabilitiesFor(context)},
    };
}

QVariantMap describeDocument(const Controller &controller, const QVariantMap &instance,
                             const QVariantMap &schema)
{
    DescribeContext context;
    context.appName = controller.appName();
    if (context.appName.isEmpty()) context.appName = QCoreApplication::applicationName();
    context.appId = runOptions().appId;
    context.appVersion = controller.appVersion();
    if (context.appVersion.isEmpty()) context.appVersion = QCoreApplication::applicationVersion();
    context.releaseHash = runOptions().releaseHash;
    context.appMode = controller.appMode();
    context.instance = instance;
    context.retainOriginal = controller.retainOriginal();
    // Published applications keep the project structure fixed but still accept
    // parameter changes through node.configure/params.set, so parameter
    // writability follows read-only mode rather than the app mode.
    context.writable = controller.publishMode() || !controller.readOnly();
    context.schemaVersion = CommandRegistry::apiVersion();
    context.schemaDigest = CommandRegistry::instance().digest();
    context.project = controller.project();
    context.graphNodes = controller.graphNodes();
    context.registry = controller.registry();
    const auto commands = schema.value(QStringLiteral("commands")).toMap();
    for (auto i = commands.cbegin(); i != commands.cend(); ++i) context.commands.insert(i.key());
    context.runtime = controller.runtime();
    return buildDescribe(context);
}

QVariantMap connectionDocument(const QVariantMap &record)
{
    const auto path = record.contains(QStringLiteral("discovery"))
        ? record.value(QStringLiteral("discovery")).toString()
        : record.value(QStringLiteral("path")).toString();
    const auto url = record.value(QStringLiteral("url")).toString();
    const auto appName = nonEmpty(record.value(QStringLiteral("app_name")).toString(), QStringLiteral("CoRig"));
    // Never read record["token"]: the copyable text must stay safe for chat,
    // scripts and logs.  The caller reads the token from the discovery record.
    const QString text = QStringLiteral(
        "CoRig AI 接口接入信息（api_version=3）\n"
        "1. 应用必须已运行；当前实例：%1。\n"
        "2. 连接记录：%2\n"
        "   多实例时按 app_id、pid 或工程路径选择；同目录 api.json 指向最近启动的实例。\n"
        "3. 地址：%3（仅接受 127.0.0.1 环回请求）。\n"
        "   请求头：Authorization: Bearer <token>；token 只从连接记录读取，不要写入脚本、日志或对话。\n"
        "4. 描述：GET %3/v3/describe；命令签名：GET %3/v3/schema。\n"
        "5. 调用：POST %3/v3/command，JSON {\"op\":\"...\",\"args\":{...}}。\n"
        "6. 异步：若返回 job，轮询 POST %3/v3/command {\"op\":\"job.get\",\"args\":{\"id\":\"...\"}}，直到 state=complete/failed/cancelled。\n"
        "7. 限制：单连接 10 秒超时，请求体不超过 8 MiB；每次连接只处理一个请求。\n"
        "8. 令牌不会出现在 /v3/describe 或本说明中；认证失败（401）时重新读取连接记录（重启后令牌会变化）。")
        .arg(appName).arg(path).arg(url);
    return QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("path"), path}, {QStringLiteral("url"), url}};
}

} // namespace corig