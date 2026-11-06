// CoRig command registry: declarative signatures, publish policy, and digest.
//
// W2 (ai-api) scope: description only.  The table is generated from the
// current docs/COMMAND_SCHEMA.json command/argument names, the W2 user-parameter
// and connection commands, and annotated from Controller::command() behavior.
// Types/required/default/enum/range are part of the W2 protocol; args that
// cannot be pinned down without inventing a contract are deliberately marked
// "unknown" and listed by unknownTypeArgs().

#include "command_registry.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace corig {
namespace {

QString publishPolicyName(PublishPolicy policy)
{
    switch (policy) {
    case PublishPolicy::Allow: return QStringLiteral("allow");
    case PublishPolicy::Deny: return QStringLiteral("deny");
    case PublishPolicy::Conditional: return QStringLiteral("conditional");
    }
    return QStringLiteral("deny");
}

QStringList errorsFor(const QString &operation, const QString &partition)
{
    // Per-command overrides first; everything else gets the stable baseline for
    // its partition.  These are protocol codes, not the current Chinese error
    // strings returned by Controller::invoke().
    if (operation == QStringLiteral("job.get")) {
        return {QStringLiteral("CORIG_ARG_INVALID"), QStringLiteral("CORIG_JOB_MISSING"),
                QStringLiteral("CORIG_JOB_FAILED"), QStringLiteral("CORIG_NOT_FOUND")};
    }
    if (operation == QStringLiteral("job.cancel")) {
        return {QStringLiteral("CORIG_ARG_INVALID"), QStringLiteral("CORIG_JOB_MISSING"),
                QStringLiteral("CORIG_NOT_CANCELLABLE")};
    }
    if (operation == QStringLiteral("job.list"))
        return {QStringLiteral("CORIG_ARG_INVALID")};
    if (operation.startsWith(QStringLiteral("params.")))
        return {QStringLiteral("CORIG_ARG_INVALID"), QStringLiteral("CORIG_NOT_FOUND"),
                QStringLiteral("CORIG_PRECONDITION")};
    if (operation == QStringLiteral("project.export_original")) {
        return {QStringLiteral("CORIG_ARG_INVALID"), QStringLiteral("CORIG_PRECONDITION"),
                QStringLiteral("CORIG_EXPORT_DISABLED")};
    }
    if (operation == QStringLiteral("project.export") || operation == QStringLiteral("plugin.export")) {
        return {QStringLiteral("CORIG_ARG_INVALID"), QStringLiteral("CORIG_PRECONDITION"),
                QStringLiteral("CORIG_READ_ONLY"), QStringLiteral("CORIG_EXPORT_DISABLED")};
    }
    if (operation == QStringLiteral("batch")) {
        return {QStringLiteral("CORIG_ARG_INVALID"), QStringLiteral("CORIG_PRECONDITION"),
                QStringLiteral("CORIG_READ_ONLY")};
    }

    QStringList codes{QStringLiteral("CORIG_ARG_INVALID"), QStringLiteral("CORIG_PRECONDITION")};
    if (partition == QStringLiteral("read")) {
        codes << QStringLiteral("CORIG_NOT_FOUND");
    } else {
        codes << QStringLiteral("CORIG_READ_ONLY");
        if (partition == QStringLiteral("authoring"))
            codes << QStringLiteral("CORIG_NOT_FOUND");
    }
    return codes;
}

void writeJsonString(QByteArray &out, const QString &value)
{
    out += '"';
    const QByteArray utf8 = value.toUtf8();
    for (const char raw : utf8) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                out += "\\u";
                out += QByteArray::number(ch, 16).rightJustified(4, '0');
            } else {
                out += static_cast<char>(ch);
            }
        }
    }
    out += '"';
}

void writeJsonNumber(QByteArray &out, double value)
{
    if (!std::isfinite(value)) { out += "null"; return; }
    if (value == 0) { out += '0'; return; }
    if (std::floor(value) == value && std::abs(value) <= 9007199254740991.0) {
        out += QByteArray::number(static_cast<qint64>(value));
        return;
    }
    // Shortest representation that round-trips; keeps the digest stable across
    // Qt/platform versions without emitting locale-dependent text.
    for (int precision = 1; precision <= 17; ++precision) {
        const QByteArray text = QByteArray::number(value, 'g', precision);
        if (text.toDouble() == value) { out += text; return; }
    }
    out += QByteArray::number(value, 'g', 17);
}

void writeJsonValue(QByteArray &out, const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null: out += "null"; break;
    case QJsonValue::Bool: out += value.toBool() ? "true" : "false"; break;
    case QJsonValue::Double: writeJsonNumber(out, value.toDouble()); break;
    case QJsonValue::String: writeJsonString(out, value.toString()); break;
    case QJsonValue::Array: {
        out += '[';
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            if (i) out += ',';
            writeJsonValue(out, array.at(i));
        }
        out += ']';
        break;
    }
    case QJsonValue::Object: {
        out += '{';
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
            return left < right;
        });
        for (int i = 0; i < keys.size(); ++i) {
            if (i) out += ',';
            writeJsonString(out, keys.at(i));
            out += ':';
            writeJsonValue(out, object.value(keys.at(i)));
        }
        out += '}';
        break;
    }
    case QJsonValue::Undefined: out += "null"; break;
    }
}

} // namespace

bool PublishDecision::allowed() const { return policy == PublishPolicy::Allow; }
bool PublishDecision::conditional() const { return policy == PublishPolicy::Conditional; }
QString PublishDecision::name() const { return publishPolicyName(policy); }

int CommandRegistry::apiVersion() { return 3; }

const CommandRegistry &CommandRegistry::instance()
{
    static const CommandRegistry registry;
    return registry;
}

CommandRegistry::CommandRegistry()
{
    const auto arg = [](const QString &type) {
        return QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("required"), false}};
    };
    const auto reqArg = [](const QString &type) {
        return QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("required"), true}};
    };
    const auto withDefault = [](QJsonObject object, const QJsonValue &value) {
        object.insert(QStringLiteral("default"), value);
        return object;
    };
    const auto withEnum = [](QJsonObject object, std::initializer_list<const char *> values) {
        QJsonArray array;
        for (const char *value : values) array.append(QString::fromUtf8(value));
        object.insert(QStringLiteral("enum"), array);
        return object;
    };
    const auto withRange = [](QJsonObject object, double minimum, double maximum, const QString &unit) {
        object.insert(QStringLiteral("min"), minimum);
        object.insert(QStringLiteral("max"), maximum);
        if (!unit.isEmpty()) object.insert(QStringLiteral("unit"), unit);
        return object;
    };
    const auto allow = [](const QString &reason) {
        return PublishDecision{PublishPolicy::Allow, reason};
    };
    const auto deny = [](const QString &reason) {
        return PublishDecision{PublishPolicy::Deny, reason};
    };
    const auto conditional = [](const QString &reason) {
        return PublishDecision{PublishPolicy::Conditional, reason};
    };
    const auto add = [this](const QString &operation, const QJsonObject &args, bool async,
                            const QString &partition, const PublishDecision &publish) {
        QJsonObject signature;
        signature.insert(QStringLiteral("args"), args);
        signature.insert(QStringLiteral("async"), async);
        signature.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errorsFor(operation, partition)));
        signature.insert(QStringLiteral("partition"), partition);
        QJsonObject publishObject;
        publishObject.insert(QStringLiteral("policy"), publish.name());
        publishObject.insert(QStringLiteral("reason"), publish.reason);
        signature.insert(QStringLiteral("publish"), publishObject);
        if (operation == QStringLiteral("batch"))
            signature.insert(QStringLiteral("two_phase"), true);
        signatures_.insert(operation, signature);
        publish_.insert(operation, publish);
    };

    add("app.quit", QJsonObject{
        {"discard", withDefault(arg("boolean"), false)},
    }, false, "control", allow("应用生命周期控制"));
    add("app.restart", QJsonObject{
        {"discard", withDefault(arg("boolean"), false)},
        {"path", arg("string")},
        {"plugin_selections", arg("object")},
    }, false, "control", allow("应用生命周期控制"));
    add("app.state", QJsonObject{}, false, "read", allow("应用状态只读查询"));
    add("app.connection", QJsonObject{}, false, "read", allow("复制接入信息"));
    add("batch", QJsonObject{
        {"commands", reqArg("array")},
    }, true, "authoring", deny("发布模式只读，批处理在发布模式下不可达"));
    add("capture", QJsonObject{
        {"target", withDefault(arg("string"), QStringLiteral("window"))},
        {"path", arg("string")},
        {"width", withRange(arg("integer"), 1, 8192, "px")},
        {"height", withRange(arg("integer"), 1, 8192, "px")},
        {"job", arg("string")},
        {"window", arg("string")},
    }, true, "control", allow("截图"));
    add("clipboard.read", QJsonObject{}, false, "control", deny("剪贴板读取接口，发布模式不开放"));
    add("container.layout", QJsonObject{
        {"id", reqArg("string")},
        {"width", arg("number")},
        {"height", arg("number")},
        {"grid", withRange(withDefault(arg("integer"), 16), 1, 128, "")},
    }, false, "read", deny("布局查询/编辑接口，发布模式不开放"));
    add("controls.click", QJsonObject{
        {"id", reqArg("string")},
        {"page", arg("string")},
    }, false, "control", allow("运行时控件操作"));
    add("controls.commit", QJsonObject{
        {"id", reqArg("string")},
        {"page", arg("string")},
    }, false, "control", allow("运行时控件操作"));
    add("controls.focus", QJsonObject{
        {"id", reqArg("string")},
        {"page", arg("string")},
    }, false, "control", allow("运行时控件操作"));
    add("controls.get", QJsonObject{
        {"id", reqArg("string")},
        {"page", arg("string")},
    }, false, "read", allow("运行时控件操作"));
    add("controls.key", QJsonObject{
        {"id", reqArg("string")},
        {"page", arg("string")},
        {"key", arg("string")},
        {"text", arg("string")},
        {"modifiers", arg("string")},
    }, false, "control", allow("运行时控件操作"));
    add("controls.list", QJsonObject{
        {"page", arg("string")},
    }, false, "read", allow("运行时控件操作"));
    add("controls.pointer", QJsonObject{
        {"id", reqArg("string")},
        {"page", arg("string")},
        {"type", withEnum(withDefault(arg("string"), QStringLiteral("click")), {"click", "press", "move", "release", "double_click", "wheel"})},
        {"x", arg("number")},
        {"y", arg("number")},
        {"button", arg("string")},
        {"buttons", arg("string")},
        {"delta_x", withDefault(arg("number"), 0)},
        {"delta_y", withDefault(arg("number"), 0)},
        {"modifiers", arg("string")},
    }, false, "control", allow("运行时控件操作"));
    add("controls.set", QJsonObject{
        {"id", reqArg("string")},
        {"page", arg("string")},
        {"property", withDefault(arg("string"), QStringLiteral("text"))},
        {"value", arg("unknown")},
    }, false, "control", allow("运行时控件操作"));
    add("editor.add", QJsonObject{
        {"id", arg("string")},
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"plugin", reqArg("string")},
    }, true, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.configure", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"zoom", arg("number")},
        {"pan_x", arg("number")},
        {"pan_y", arg("number")},
        {"left_width", arg("number")},
        {"right_width", arg("number")},
        {"fit", withDefault(arg("boolean"), false)},
        {"animation", withDefault(arg("boolean"), false)},
    }, false, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.copy", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"ids", reqArg("array")},
    }, false, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.paste", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"payload", reqArg("object")},
    }, true, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.place", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"id", arg("string")},
        {"ids", arg("array")},
        {"position", arg("array")},
        {"parent", arg("string")},
        {"index", withDefault(arg("integer"), -1)},
    }, true, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.plan", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"ids", arg("array")},
        {"operation", withEnum(reqArg("string"), {"remove", "replace", "withdraw"})},
        {"plugin", arg("string")},
    }, false, "read", deny("编辑器接口，发布模式不开放"));
    add("editor.remove", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"id", arg("string")},
        {"ids", arg("array")},
        {"confirmed", reqArg("boolean")},
        {"revision", arg("string")},
    }, true, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.reorder", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"id", reqArg("string")},
        {"index", reqArg("integer")},
    }, false, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.replace", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"id", arg("string")},
        {"ids", arg("array")},
        {"plugin", reqArg("string")},
        {"confirmed", reqArg("boolean")},
        {"revision", arg("string")},
    }, true, "authoring", deny("编辑器接口，发布模式不开放"));
    add("editor.state", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
    }, false, "read", deny("编辑器接口，发布模式不开放"));
    add("editor.withdraw", QJsonObject{
        {"page", withEnum(reqArg("string"), {"graph", "layout"})},
        {"id", arg("string")},
        {"ids", arg("array")},
    }, true, "authoring", deny("编辑器接口，发布模式不开放"));
    add("graph.activity", QJsonObject{}, false, "read", deny("图编辑接口，发布模式不开放"));
    add("graph.connection.options", QJsonObject{
        {"node", reqArg("string")},
        {"port", reqArg("string")},
    }, false, "read", deny("图编辑接口，发布模式不开放"));
    add("graph.fit", QJsonObject{}, false, "control", deny("图编辑接口，发布模式不开放"));
    add("graph.node.add", QJsonObject{
        {"id", arg("string")},
        {"plugin", reqArg("string")},
        {"title", arg("string")},
        {"config", arg("object")},
        {"position", withDefault(arg("array"), QJsonArray{QJsonValue(80), QJsonValue(80)})},
        {"orientation", withRange(withDefault(arg("integer"), 0), 0, 360, "deg")},
        {"placed", withDefault(arg("boolean"), true)},
    }, true, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.node.move", QJsonObject{
        {"id", reqArg("string")},
        {"position", reqArg("array")},
    }, false, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.node.patch", QJsonObject{
        {"id", reqArg("string")},
        {"changes", reqArg("object")},
    }, false, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.node.remove", QJsonObject{
        {"id", arg("string")},
        {"ids", arg("array")},
    }, true, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.node.rotate", QJsonObject{
        {"id", reqArg("string")},
        {"degrees", withDefault(arg("integer"), 90)},
    }, false, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.overlaps", QJsonObject{}, false, "read", deny("图编辑接口，发布模式不开放"));
    add("graph.select", QJsonObject{
        {"id", arg("string")},
        {"kind", withDefault(arg("string"), QStringLiteral("node"))},
        {"ids", arg("array")},
    }, false, "control", deny("图编辑接口，发布模式不开放"));
    add("graph.wire.add", QJsonObject{
        {"id", arg("string")},
        {"source", reqArg("array")},
        {"target", reqArg("array")},
        {"routing", withEnum(withDefault(arg("string"), QStringLiteral("curve")), {"curve", "polyline"})},
        {"color", withDefault(arg("string"), QStringLiteral("#5E9DCB"))},
        {"width", withRange(withDefault(arg("integer"), 1.6), 0.5, 10, "px")},
        {"points", withDefault(arg("array"), QJsonArray())},
    }, true, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.wire.handles", QJsonObject{
        {"id", reqArg("string")},
        {"x", withDefault(arg("number"), 0)},
        {"y", withDefault(arg("number"), 0)},
    }, false, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.wire.patch", QJsonObject{
        {"id", reqArg("string")},
        {"changes", reqArg("object")},
    }, false, "authoring", deny("图编辑接口，发布模式不开放"));
    add("graph.wire.remove", QJsonObject{
        {"id", reqArg("string")},
    }, true, "authoring", deny("图编辑接口，发布模式不开放"));
    add("history.redo", QJsonObject{}, true, "authoring", deny("编辑历史接口，发布模式不开放"));
    add("history.undo", QJsonObject{}, true, "authoring", deny("编辑历史接口，发布模式不开放"));
    add("io.ports", QJsonObject{}, true, "read", allow("I/O 端口查询"));
    add("job.cancel", QJsonObject{
        {"id", reqArg("string")},
    }, false, "control", allow("异步任务查询/取消"));
    add("job.get", QJsonObject{
        {"id", reqArg("string")},
    }, false, "read", allow("异步任务查询/取消"));
    add("job.list", QJsonObject{}, false, "read", allow("异步任务查询/取消"));
    add("library.configure", QJsonObject{
        {"view", withEnum(arg("string"), {"grid", "list"})},
        {"size", withRange(arg("number"), 0.6, 2, "")},
    }, false, "admin", deny("库管理接口，发布模式不开放"));
    add("library.favorite", QJsonObject{
        {"plugin", reqArg("string")},
        {"enabled", reqArg("boolean")},
    }, false, "admin", deny("库管理接口，发布模式不开放"));
    add("library.preview", QJsonObject{
        {"plugin", reqArg("string")},
        {"scope", withEnum(withDefault(arg("string"), QStringLiteral("project")), {"project", "library"})},
    }, false, "read", deny("库管理接口，发布模式不开放"));
    add("library.search", QJsonObject{
        {"query", arg("string")},
        {"category", arg("array")},
        {"scope", withEnum(withDefault(arg("string"), QStringLiteral("all")), {"all", "project", "library"})},
    }, false, "read", deny("库管理接口，发布模式不开放"));
    add("library.state", QJsonObject{}, false, "read", deny("库管理接口，发布模式不开放"));
    add("log.clear", QJsonObject{}, true, "control", allow("运行日志读取/配置/导出"));
    add("log.configure", QJsonObject{
        {"detail", withDefault(arg("string"), false)},
    }, true, "control", allow("运行日志读取/配置/导出"));
    add("log.export", QJsonObject{
        {"path", reqArg("string")},
        {"node", arg("string")},
        {"level", arg("string")},
        {"text", arg("string")},
        {"batch", arg("unknown")},
    }, true, "control", allow("运行日志读取/配置/导出"));
    add("log.query", QJsonObject{
        {"after", arg("integer")},
        {"limit", arg("integer")},
        {"node", arg("string")},
        {"level", arg("string")},
        {"text", arg("string")},
        {"batch", arg("unknown")},
    }, true, "read", allow("运行日志读取/配置/导出"));
    add("node.action", QJsonObject{
        {"node", reqArg("string")},
        {"action", reqArg("string")},
        {"args", arg("object")},
    }, true, "control", allow("节点运行控制"));
    add("node.configure", QJsonObject{
        {"node", reqArg("string")},
        {"changes", reqArg("object")},
        {"persist", arg("boolean")},
        {"confirmed", withDefault(arg("boolean"), false)},
        {"revision", arg("string")},
    }, true, "control", allow("节点运行控制"));
    add("node.configure.plan", QJsonObject{
        {"node", reqArg("string")},
        {"changes", reqArg("object")},
    }, false, "read", deny("只读配置计划未列入发布白名单（W2 评估）"));
    add("node.state", QJsonObject{
        {"node", reqArg("string")},
        {"detailed", withDefault(arg("boolean"), false)},
    }, true, "read", allow("节点运行控制"));
    add("params.get", QJsonObject{
        {"node", reqArg("string")},
        {"param", reqArg("string")},
    }, false, "read", allow("用户参数读取"));
    add("params.set", QJsonObject{
        {"node", reqArg("string")},
        {"param", reqArg("string")},
        {"value", reqArg("unknown")},
    }, false, "control", allow("用户参数覆盖"));
    add("params.reset", QJsonObject{
        {"scope", arg("string")},
    }, false, "control", allow("用户参数恢复"));
    add("plugin.directory", QJsonObject{}, false, "read", deny("插件管理接口，发布模式不开放"));
    add("plugin.enable", QJsonObject{
        {"plugin", arg("string")},
        {"plugins", arg("array")},
        {"enabled", reqArg("boolean")},
    }, true, "admin", deny("插件管理接口，发布模式不开放"));
    add("plugin.export", QJsonObject{
        {"plugin", arg("string")},
        {"path", arg("string")},
        {"project", arg("string")},
        {"plugins", arg("array")},
        {"directory", arg("string")},
        {"overwrite", withDefault(arg("boolean"), false)},
        {"scope", withEnum(withDefault(arg("string"), QStringLiteral("project")), {"project", "library"})},
    }, true, "admin", deny("插件管理接口，发布模式不开放"));
    add("plugin.export.dialog", QJsonObject{
        {"scope", withEnum(withDefault(arg("string"), QStringLiteral("project")), {"project", "library"})},
        {"plugin", arg("string")},
    }, false, "admin", deny("插件管理接口，发布模式不开放"));
    add("plugin.import", QJsonObject{
        {"path", arg("string")},
        {"manifest", arg("object")},
    }, true, "admin", deny("插件管理接口，发布模式不开放"));
    add("plugin.remove", QJsonObject{
        {"plugin", arg("string")},
        {"package", arg("string")},
    }, true, "admin", deny("插件管理接口，发布模式不开放"));
    add("plugin.settings", QJsonObject{}, false, "read", deny("插件管理接口，发布模式不开放"));
    add("plugin.state", QJsonObject{}, false, "read", deny("插件管理接口，发布模式不开放"));
    add("plugin.tree", QJsonObject{
        {"scope", withEnum(withDefault(arg("string"), QStringLiteral("project")), {"project", "library"})},
    }, false, "read", deny("插件管理接口，发布模式不开放"));
    add("preferences.set", QJsonObject{
        {"theme", withEnum(arg("string"), {"dark", "light"})},
        {"python", arg("string")},
        {"read_only", arg("boolean")},
    }, false, "admin", conditional("发布模式仅允许不会切换运行环境的偏好设置（theme/python/read_only），具体参数条件由 W2 落地"));
    add("project.export", QJsonObject{
        {"path", reqArg("string")},
        {"include_plugins", arg("boolean")},
    }, true, "authoring", deny("工程/设计管理接口，发布模式不开放"));
    add("project.export_original", QJsonObject{
        {"path", reqArg("string")},
        {"overwrite", withDefault(arg("boolean"), false)},
    }, true, "authoring", conditional("仅 retain_original=true 的发布实例允许导出原始工程"));
    add("project.get", QJsonObject{}, false, "read", deny("工程/设计管理接口，发布模式不开放"));
    add("project.new", QJsonObject{
        {"name", withDefault(arg("string"), QStringLiteral("未命名工程"))},
        {"discard", withDefault(arg("boolean"), false)},
    }, true, "authoring", deny("工程/设计管理接口，发布模式不开放"));
    add("project.open", QJsonObject{
        {"path", reqArg("string")},
        {"discard", withDefault(arg("boolean"), false)},
        {"plugin_selections", arg("object")},
    }, true, "authoring", deny("工程/设计管理接口，发布模式不开放"));
    add("project.options", QJsonObject{
        {"include_plugins", arg("boolean")},
    }, false, "authoring", deny("工程/设计管理接口，发布模式不开放"));
    add("project.rename", QJsonObject{
        {"name", reqArg("string")},
    }, false, "authoring", deny("工程/设计管理接口，发布模式不开放"));
    add("project.info", QJsonObject{
        {"name", arg("string")},
        {"description", arg("string")},
        {"author", arg("string")},
        {"version", arg("string")},
    }, false, "authoring", deny("工程/设计管理接口，发布模式不开放"));
    add("project.save", QJsonObject{
        {"path", arg("string")},
        {"include_plugins", arg("boolean")},
    }, true, "authoring", deny("工程/设计管理接口，发布模式不开放"));
    add("project.validate", QJsonObject{}, false, "read", deny("工程/设计管理接口，发布模式不开放"));
    add("property.commit", QJsonObject{
        {"scope", withEnum(reqArg("string"), {"node", "ui", "workspace"})},
        {"target", reqArg("array")},
        {"changes", reqArg("object")},
        {"confirmed", arg("boolean")},
        {"revision", arg("string")},
    }, true, "authoring", deny("属性编辑接口，发布模式不开放"));
    add("property.discard", QJsonObject{
        {"scope", withEnum(reqArg("string"), {"node", "ui", "workspace"})},
        {"target", reqArg("array")},
    }, false, "authoring", deny("属性编辑接口，发布模式不开放"));
    add("property.drafts", QJsonObject{}, false, "read", deny("属性编辑接口，发布模式不开放"));
    add("property.get", QJsonObject{
        {"scope", withEnum(reqArg("string"), {"node", "ui", "workspace"})},
        {"target", reqArg("array")},
    }, false, "read", deny("属性编辑接口，发布模式不开放"));
    add("property.stage", QJsonObject{
        {"scope", withEnum(reqArg("string"), {"node", "ui", "workspace"})},
        {"target", reqArg("array")},
        {"changes", reqArg("object")},
    }, false, "authoring", deny("属性编辑接口，发布模式不开放"));
    add("property.text", QJsonObject{
        {"scope", withEnum(reqArg("string"), {"node", "ui", "workspace"})},
        {"target", reqArg("array")},
        {"field", reqArg("string")},
        {"text", reqArg("string")},
    }, false, "authoring", deny("属性编辑接口，发布模式不开放"));
    add("python.probe", QJsonObject{
        {"dependencies", arg("array")},
    }, true, "read", deny("Python 环境探测接口，发布模式不开放"));
    add("registry", QJsonObject{}, false, "read", allow("自描述接口"));
    add("resources.get", QJsonObject{}, false, "read", allow("资源状态查询"));
    add("resources.set", QJsonObject{
        {"limits", arg("object")},
        {"reset", withDefault(arg("boolean"), false)},
    }, false, "admin", deny("资源限制配置接口，发布模式不开放"));
    add("runtime.debug", QJsonObject{
        {"enabled", reqArg("boolean")},
    }, true, "control", allow("运行控制"));
    add("runtime.emit", QJsonObject{
        {"node", reqArg("string")},
        {"outputs", reqArg("object")},
    }, true, "control", allow("运行控制"));
    add("runtime.pause", QJsonObject{}, true, "control", allow("运行控制"));
    add("runtime.reset", QJsonObject{}, true, "control", allow("运行控制"));
    add("runtime.start", QJsonObject{
        {"debug", withDefault(arg("boolean"), false)},
    }, true, "control", allow("运行控制"));
    add("runtime.state", QJsonObject{}, true, "read", allow("运行控制"));
    add("runtime.step", QJsonObject{
        {"mode", withEnum(reqArg("string"), {"batch", "node"})},
        {"source", arg("unknown")},
    }, true, "control", allow("运行控制"));
    add("runtime.stop", QJsonObject{}, true, "control", allow("运行控制"));
    add("schema", QJsonObject{}, false, "read", allow("自描述接口"));
    add("settings.open", QJsonObject{
        {"page", withDefault(arg("string"), QStringLiteral("general"))},
    }, false, "control", allow("打开设置界面"));
    add("publish.start", QJsonObject{
        {"project", arg("string")},
        {"directory", reqArg("string")},
        {"name", reqArg("string")},
        {"version", reqArg("string")},
        {"app_id", arg("string")},
        {"retain_original", withDefault(arg("boolean"), false)},
        {"overwrite", withDefault(arg("boolean"), false)},
        {"python", arg("string")},
    }, true, "control", deny("软件发布是编辑器功能，发布运行模式不可用"));
    add("publish.state", QJsonObject{}, false, "read", deny("软件发布是编辑器功能，发布运行模式不可用"));
    add("publish.cancel", QJsonObject{
        {"job", arg("string")},
    }, false, "control", deny("软件发布是编辑器功能，发布运行模式不可用"));
    add("publish.log", QJsonObject{}, false, "read", deny("软件发布是编辑器功能，发布运行模式不可用"));
    add("sync.apply", QJsonObject{
        {"direction", withEnum(reqArg("string"), {"graph_to_ui", "ui_to_graph"})},
        {"keys", reqArg("array")},
        {"revision", arg("string")},
    }, true, "authoring", deny("双向同步接口，发布模式不开放"));
    add("sync.plan", QJsonObject{
        {"direction", withEnum(reqArg("string"), {"graph_to_ui", "ui_to_graph"})},
    }, false, "authoring", deny("双向同步接口，发布模式不开放"));
    add("ui.container.add", QJsonObject{
        {"id", arg("string")},
        {"parent", arg("string")},
        {"plugin", withDefault(arg("string"), QStringLiteral("layout.column"))},
        {"properties", arg("object")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.item.move", QJsonObject{
        {"id", reqArg("string")},
        {"parent", reqArg("string")},
        {"index", arg("integer")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.item.patch", QJsonObject{
        {"id", reqArg("string")},
        {"properties", reqArg("object")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.item.remove", QJsonObject{
        {"id", reqArg("string")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.item.schema", QJsonObject{
        {"id", reqArg("string")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.update", QJsonObject{}, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.view.add", QJsonObject{
        {"id", arg("string")},
        {"parent", arg("string")},
        {"node", reqArg("string")},
        {"view", reqArg("string")},
        {"properties", arg("object")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.view.appearance", QJsonObject{
        {"node", reqArg("string")},
        {"view", reqArg("string")},
        {"changes", arg("object")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("ui.view.bind", QJsonObject{
        {"id", reqArg("string")},
        {"node", reqArg("string")},
        {"view", reqArg("string")},
    }, false, "authoring", deny("UI 编辑接口，发布模式不开放"));
    add("update.check", QJsonObject{}, true, "admin", deny("更新管理接口，发布模式不开放"));
    add("update.install", QJsonObject{}, false, "admin", deny("更新管理接口，发布模式不开放"));
    add("update.state", QJsonObject{}, false, "read", deny("更新管理接口，发布模式不开放"));
    add("view.actions", QJsonObject{
        {"node", reqArg("string")},
        {"view", reqArg("string")},
    }, false, "read", allow("视图交互"));
    add("view.call", QJsonObject{
        {"node", reqArg("string")},
        {"view", reqArg("string")},
        {"action", reqArg("string")},
        {"args", arg("object")},
    }, true, "control", allow("视图交互"));
    add("view.read", QJsonObject{
        {"node", reqArg("string")},
        {"view", reqArg("string")},
        {"args", arg("object")},
    }, true, "read", allow("视图交互"));
    add("window.close", QJsonObject{
        {"window", reqArg("string")},
        {"discard", arg("boolean")},
    }, false, "control", allow("关闭窗口"));
    add("window.configure", QJsonObject{
        {"window", reqArg("string")},
        {"x", withRange(arg("number"), -100000, 100000, "px")},
        {"y", withRange(arg("number"), -100000, 100000, "px")},
        {"width", withRange(arg("integer"), 1, 16384, "px")},
        {"height", withRange(arg("integer"), 1, 16384, "px")},
        {"state", withEnum(arg("string"), {"normal", "minimized", "maximized", "fullscreen"})},
        {"focus", withDefault(arg("boolean"), false)},
    }, false, "control", deny("窗口配置接口，发布模式不开放"));
    add("window.pin", QJsonObject{
        {"window", reqArg("string")},
        {"enabled", reqArg("boolean")},
    }, false, "control", deny("窗口配置接口，发布模式不开放"));
    add("workspace.close", QJsonObject{
        {"page", reqArg("string")},
        {"discard", arg("boolean")},
    }, false, "control", allow("工作区页面打开/关闭/列表"));
    add("workspace.configure", QJsonObject{
        {"page", reqArg("string")},
        {"changes", reqArg("object")},
    }, false, "authoring", deny("工作区配置/管理接口，发布模式不开放"));
    add("workspace.detach", QJsonObject{
        {"page", reqArg("string")},
        {"x", arg("integer")},
        {"y", arg("integer")},
    }, false, "control", deny("工作区配置/管理接口，发布模式不开放"));
    add("workspace.list", QJsonObject{}, false, "read", allow("工作区页面打开/关闭/列表"));
    add("workspace.move", QJsonObject{
        {"page", reqArg("string")},
        {"window", arg("string")},
        {"index", withDefault(arg("integer"), -1)},
    }, false, "control", deny("工作区配置/管理接口，发布模式不开放"));
    add("workspace.open", QJsonObject{
        {"page", reqArg("string")},
        {"window", arg("string")},
        {"activate", withDefault(arg("boolean"), false)},
    }, false, "control", allow("工作区页面打开/关闭/列表"));
    add("workspace.remove", QJsonObject{
        {"page", reqArg("string")},
    }, false, "authoring", deny("工作区配置/管理接口，发布模式不开放"));

}

QJsonObject CommandRegistry::signatures() const { return signatures_; }

QJsonObject CommandRegistry::document() const
{
    QJsonObject document;
    document.insert(QStringLiteral("api_version"), apiVersion());
    document.insert(QStringLiteral("signatures"), signatures_);
    return document;
}

QString CommandRegistry::canonicalJson() const
{
    QByteArray bytes;
    writeJsonValue(bytes, document());
    return QString::fromUtf8(bytes);
}

QString CommandRegistry::digest() const
{
    const QByteArray hash = QCryptographicHash::hash(canonicalJson().toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

PublishDecision CommandRegistry::publishDecision(const QString &operation) const
{
    const auto it = publish_.constFind(operation);
    if (it == publish_.cend())
        return PublishDecision{PublishPolicy::Deny, QStringLiteral("未知操作：%1").arg(operation)};
    return it.value();
}

bool CommandRegistry::contains(const QString &operation) const
{
    return signatures_.contains(operation);
}

QStringList CommandRegistry::operationNames() const { return signatures_.keys(); }

QStringList CommandRegistry::unknownTypeArgs() const
{
    QStringList result;
    for (auto command = signatures_.cbegin(); command != signatures_.cend(); ++command) {
        const QJsonObject args = command.value().toObject().value(QStringLiteral("args")).toObject();
        for (auto arg = args.cbegin(); arg != args.cend(); ++arg) {
            if (arg.value().toObject().value(QStringLiteral("type")).toString() == QStringLiteral("unknown"))
                result.append(command.key() + QLatin1Char('.') + arg.key());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

QStringList CommandRegistry::partitions() const
{
    return {QStringLiteral("read"), QStringLiteral("control"), QStringLiteral("authoring"), QStringLiteral("admin")};
}

} // namespace corig
