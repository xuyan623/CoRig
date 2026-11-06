#include "project.h"
#include "resource_policy.h"
#include <QCborValue>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace corig {
static const QByteArray Magic = QByteArrayLiteral("CoRig\0CPP3\n");

QVariantMap emptyProject(const QString &name)
{
    return {{"format","corig-project"}, {"schema_version",3}, {"name",name},
        {"description",""}, {"author",""}, {"version","1.0.0"},
        {"nodes",QVariantList{}}, {"wires",QVariantList{}}, {"user_plugins",QVariantList{}}, {"resources",QVariantMap{}},
        {"ui",QVariantMap{{"root","root"}, {"items",QVariantList{QVariantMap{{"id","root"}, {"plugin","layout.canvas"}, {"children",QVariantList{}}, {"properties",QVariantMap{{"width",1280.},{"height",720.},{"grid_size",16},{"background","#111923"}}}}}}}}};
}

void alignUiToGrid(QVariantMap &project)
{
    auto ui = project.value("ui").toMap(); auto items = ui.value("items").toList(); int grid = 16;
    for (const auto &entry : items) if (entry.toMap().value("id") == ui.value("root")) grid = entry.toMap().value("properties").toMap().value("grid_size",16).toInt();
    if (grid<1 || grid>128) throw Error("网格粒度需要在 1～128 像素之间");
    for (auto &entry : items) { auto item = entry.toMap();auto props = item.value("properties").toMap();
        for (const auto &key : {"x","y","width","height","padding","gap"}) if (props.contains(key)) {
            const auto value = finiteNumber(props.value(key),QString::fromLatin1(key));
            const double snapped = std::round(value / grid)*grid;
            props[key] = key == QString("width") || key == QString("height") ? std::max(std::ceil((key==QString("width")?40.:32.)/grid)*grid,snapped) : snapped;
        }
        item["properties"] = props; entry = item;
    }
    ui["items"] = items;project["ui"] = ui;
}

QVector<QString> executionOrder(const QVariantMap &project, const Catalog &catalog)
{
    QHash<QString, int> incoming;
    QHash<QString, QStringList> outgoing;
    QVector<QString> sequence;
    QHash<QString, QString> types;
    for (const auto &item : project.value("nodes").toList()) {
        const auto n = item.toMap();
        if (!n.value("placed",true).toBool()) continue;
        const auto id = n.value("id").toString();
        types.insert(id, n.value("plugin").toString());
        incoming.insert(id, 0);
    }
    for (const auto &item : project.value("wires").toList()) {
        const auto w = item.toMap();
        const auto source = w.value("source").toList().value(0).toString();
        const auto target = w.value("target").toList().value(0).toString();
        if (catalog.definition(types.value(target)).value("breaks_cycle").toBool()) continue;
        outgoing[source].append(target);
        ++incoming[target];
    }
    QStringList ready;
    for (auto i = incoming.cbegin(); i != incoming.cend(); ++i) if (i.value() == 0) ready.append(i.key());
    ready.sort();
    while (!ready.isEmpty()) {
        const auto id = ready.takeFirst();
        sequence.append(id);
        for (const auto &target : outgoing.value(id)) if (--incoming[target] == 0) { ready.append(target); ready.sort(); }
    }
    if (sequence.size() != incoming.size()) throw Error("同步连线形成环路；请通过实际异步通信节点隔开");
    return sequence;
}

namespace {
// Geometry and hierarchy belong to placement. The view-level appearance store
// keeps only visual fields, so every reader and writer shares this filter.
const QSet<QString> &uiLayoutFields()
{
    static const QSet<QString> fields{"grid_size","x","y","width","height","stretch","min_width","min_height","max_width","max_height","row","column","rowspan","colspan","padding","gap","current_index"};
    return fields;
}
QVariantMap withoutLayoutFields(QVariantMap values)
{
    for (const auto &field : uiLayoutFields()) values.remove(field);
    return values;
}
QStringList declaredViews(const QVariantMap &definition)
{
    const auto declared = definition.value("views");
    return declared.metaType().id() == QMetaType::QVariantMap ? declared.toMap().keys() : declared.toStringList();
}
}

QVariantMap uiPropertySchema(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project)
{
    QVariantMap result{
        {"grid_size",QVariantMap{{"type","integer"},{"default",16},{"min",1},{"max",128}}},
        {"x",QVariantMap{{"type","number"},{"default",24.}}}, {"y",QVariantMap{{"type","number"},{"default",24.}}},
        {"width",QVariantMap{{"type","number"},{"default",320.},{"min",40.},{"max",16384.}}}, {"height",QVariantMap{{"type","number"},{"default",200.},{"min",32.},{"max",16384.}}},
        {"title",QVariantMap{{"type","string"},{"default",""}}}, {"stretch",QVariantMap{{"type","integer"},{"default",1},{"min",0},{"max",1000}}},
        {"min_width",QVariantMap{{"type","integer"},{"default",80},{"min",0}}}, {"min_height",QVariantMap{{"type","integer"},{"default",60},{"min",0}}},
        {"max_width",QVariantMap{{"type","integer"},{"default",16777215},{"min",1}}}, {"max_height",QVariantMap{{"type","integer"},{"default",16777215},{"min",1}}},
        {"row",QVariantMap{{"type","integer"},{"default",-1},{"min",-1}}}, {"column",QVariantMap{{"type","integer"},{"default",-1},{"min",-1}}},
        {"rowspan",QVariantMap{{"type","integer"},{"default",1},{"min",1}}}, {"colspan",QVariantMap{{"type","integer"},{"default",1},{"min",1}}},
        {"font_size",QVariantMap{{"type","number"},{"default",13},{"min",6},{"max",96}}}, {"font_family",QVariantMap{{"type","string"},{"default","Segoe UI"}}},
        {"color",QVariantMap{{"type","string"},{"default","#E6EDF5"}}}, {"background",QVariantMap{{"type","string"},{"default","#182330"}}},
        {"label",QVariantMap{{"type","string"},{"default",""}}}, {"gap",QVariantMap{{"type","integer"},{"default",8},{"min",0},{"max",100}}},
        {"padding",QVariantMap{{"type","integer"},{"default",8},{"min",0},{"max",100}}}
    };
    for (auto i = result.begin(); i != result.end(); ++i) { auto field = i.value().toMap(); field["section"] = "base"; i.value() = field; }
    if (item.value("plugin") == "layout.grid") result.insert("columns",QVariantMap{{"type","integer"},{"default",2},{"min",1},{"max",64}});
    if (item.value("id") != project.value("ui").toMap().value("root")) result.remove("grid_size");
    if (item.value("plugin") == "layout.tabs") result.insert("current_index",QVariantMap{{"type","integer"},{"default",0},{"min",0}});
    // Plugins own business appearance fields. Geometry and hierarchy retain
    // their editor meaning; authors can read/write those common fields too.
    QVariantMap extra;
    const auto container = item.value("plugin").toString();
    const bool builtinContainer = QStringList{"layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"}.contains(container);
    if (!container.isEmpty() && !builtinContainer) extra = catalog.definition(container).value("properties").toMap();
    else if (container.isEmpty()) {
        auto binding = item.value("binding").toMap();
        for (const auto &entry : project.value("nodes").toList()) if (entry.toMap().value("id") == item.value("node")) {
            const auto live = entry.toMap(); if (binding.isEmpty() || binding.value("plugin") == live.value("plugin")) binding = live;
        }
        if (!binding.value("plugin").toString().isEmpty()) {
            const auto spec = catalog.definition(binding.value("plugin").toString());
            extra = spec.value("view_properties").toMap();
            const auto local = spec.value("views").toMap().value(item.value("view").toString()).toMap().value("properties").toMap();
            for (auto i = local.cbegin(); i != local.cend(); ++i) extra[i.key()] = i.value();
        }
    }
    for (auto i = extra.cbegin(); i != extra.cend(); ++i) {
        if (uiLayoutFields().contains(i.key()) && result.contains(i.key())) continue;
        result[i.key()] = i.value();
    }
    return result;
}

QVariantMap checkedUiProperties(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project,
                               const QVariantMap &changes, const QVariantMap &previous)
{
    const auto schema = uiPropertySchema(item,catalog,project); const auto plugin = item.value("plugin").toString();
    if (!plugin.isEmpty()) {
        if (QStringList{"layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"}.contains(plugin)) return checkedProperties(schema,changes,previous);
        return catalog.checkedPresentation(plugin,"container",schema,changes,previous);
    }
    auto binding = item.value("binding").toMap();
    for (const auto &entry : project.value("nodes").toList()) if (entry.toMap().value("id") == item.value("node")) {
        const auto live = entry.toMap(); if (binding.isEmpty() || binding.value("plugin") == live.value("plugin")) binding = live;
    }
    const auto id = binding.value("plugin").toString();
    return id.isEmpty() ? checkedProperties(schema,changes,previous) : catalog.checkedPresentation(id,"view",schema,changes,previous);
}

QVariantMap viewAppearance(const QVariantMap &project, const QString &node, const QString &view)
{
    return project.value("ui").toMap().value("view_properties").toMap().value(node+"/"+view).toMap();
}

QVariantMap viewAppearanceSchema(const QVariantMap &project, const Catalog &catalog, const QString &node, const QString &view)
{
    QVariantMap design;
    for (const auto &entry : project.value("nodes").toList()) if (entry.toMap().value("id") == node) { design = entry.toMap(); break; }
    if (design.isEmpty()) throw Error("节点不存在："+node);
    if (!declaredViews(catalog.definition(design.value("plugin").toString())).contains(view)) throw Error("节点没有声明该视图："+view);
    QVariantMap item{{"id",node+"/"+view},{"node",node},{"view",view},{"binding",design}};
    return withoutLayoutFields(uiPropertySchema(item,catalog,project));
}

QVariantMap checkedViewAppearance(const QVariantMap &project, const Catalog &catalog, const QString &node, const QString &view,
                                 const QVariantMap &changes, const QVariantMap &previous)
{
    for (auto i = changes.cbegin(); i != changes.cend(); ++i)
        if (uiLayoutFields().contains(i.key())) throw Error("视图外观不支持布局字段："+i.key());
    QVariantMap item{{"id",node+"/"+view},{"node",node},{"view",view}};
    for (const auto &entry : project.value("nodes").toList()) if (entry.toMap().value("id") == node) { item["binding"] = entry.toMap(); break; }
    if (item.value("binding").toMap().isEmpty()) throw Error("节点不存在："+node);
    return withoutLayoutFields(checkedUiProperties(item,catalog,project,changes,withoutLayoutFields(previous)));
}

void validateProject(const QVariantMap &project, const Catalog &catalog)
{
    if (project.value("format") != "corig-project" || project.value("schema_version").toInt() != 3)
        throw Error("工程格式不受支持，需要新版 C++ 工程包");
    const auto nodes = project.value("nodes").toList();
    if (nodes.size() > 10000) throw Error("节点数量超过工程上限");
    QHash<QString, QHash<QString, Port>> ports;
    QSet<QString> unplaced;
    for (const auto &item : nodes) {
        const auto n = item.toMap();
        const auto id = n.value("id").toString();
        if (id.isEmpty() || ports.contains(id)) throw Error("节点 id 为空或重复");
        const auto type = n.value("plugin").toString();
        if (catalog.kind(type) != "node") throw Error("功能设计只能使用计算节点："+type);
        const auto config = catalog.checkedConfig(type, n.value("config").toMap());
        QHash<QString, Port> resolved;
        for (const auto &port : catalog.ports(type, config)) resolved.insert(port.id, port);
        ports.insert(id, resolved);
        if (!n.value("placed",true).toBool()) unplaced.insert(id);
        const auto position = n.value("position").toList();
        if (position.size() != 2) throw Error("节点需要两个位置坐标");
        finiteNumber(position[0], "横坐标"); finiteNumber(position[1], "纵坐标");
        if (n.contains("size")) { const auto size = n.value("size").toList(); if (size.size()!=2 || finiteNumber(size[0],"节点宽度")<40. || finiteNumber(size[1],"节点高度")<32.) throw Error("节点尺寸无效"); }
        const auto angle = n.value("orientation", 0).toInt();
        if (angle != 0 && angle != 90 && angle != 180 && angle != 270) throw Error("方向需要 0、90、180 或 270 度");
    }
    QSet<QString> occupied, identifiers;
    for (const auto &item : project.value("wires").toList()) {
        const auto w = item.toMap();
        const auto id = w.value("id").toString();
        const auto source = w.value("source").toList(), target = w.value("target").toList();
        if (id.isEmpty() || identifiers.contains(id) || source.size() != 2 || target.size() != 2) throw Error("导线声明无效");
        identifiers.insert(id);
        if (!ports.value(source[0].toString()).contains(source[1].toString()) || !ports.value(target[0].toString()).contains(target[1].toString())) throw Error("导线引用的端口不存在");
        const auto a = ports.value(source[0].toString()).value(source[1].toString());
        const auto b = ports.value(target[0].toString()).value(target[1].toString());
        if (unplaced.contains(source[0].toString()) || unplaced.contains(target[0].toString())) throw Error("待放置节点不能连接导线");
        if (a.direction != "out" || b.direction != "in") throw Error("导线需要从输出连接到输入");
        if (a.type != b.type && a.type != "any" && b.type != "any") throw Error("端口类型不一致，请使用显式处理节点");
        for (const auto &endpoint : {source, target}) {
            const auto key = endpoint[0].toString() + "/" + endpoint[1].toString();
            if (occupied.contains(key)) throw Error("每个端口只允许一条导线，请通过分发节点连接多个消费者");
            occupied.insert(key);
        }
        const auto route = w.value("points").toList();
        if (route.size() > 4096) throw Error("导线路径点过多");
        for (const auto &point : route) {
            const auto xy = point.toList();
            if (xy.size() != 2) throw Error("路径点需要 [x,y]");
            finiteNumber(xy[0], "路径横坐标"); finiteNumber(xy[1], "路径纵坐标");
        }
    }
    executionOrder(project, catalog);
    const auto ui = project.value("ui").toMap();
    QHash<QString, QVariantMap> items;
    for (const auto &entry : ui.value("items").toList()) {
        const auto item = entry.toMap(); const auto id = item.value("id").toString();
        if (id.isEmpty() || items.contains(id)) throw Error("界面对象 id 为空或重复");
        const auto plugin = item.value("plugin").toString();
        if (!plugin.isEmpty() && (catalog.kind(plugin) != "container" || !item.contains("children"))) throw Error("界面布局需要容器插件和 children 列表："+id);
        if (plugin.isEmpty() && item.contains("children")) throw Error("普通视图不能包含子对象："+id);
        items.insert(id, item);
    }
    if (!items.contains(ui.value("root").toString())) throw Error("界面缺少根容器");
    if (items.value(ui.value("root").toString()).value("plugin") != "layout.canvas") throw Error("CoRig 工程的根布局必须是自由网格画布");
    QSet<QString> visited, parents;
    std::function<void(QString)> visit = [&](const QString &id) {
        if (!items.contains(id) || visited.contains(id)) throw Error("界面树存在环路或无效对象");
        visited.insert(id);
        const auto item = items.value(id);
        for (const auto &child : item.value("children").toList()) {
            if (parents.contains(child.toString())) throw Error("界面对象不能有多个父容器");
            parents.insert(child.toString()); visit(child.toString());
        }
    };
    visit(ui.value("root").toString());
    if (visited.size() != items.size()) throw Error("界面树存在未归属对象");
    // The view-level appearance store is saved document data. Loading and the
    // project validator enforce exactly the rules the editor write path uses,
    // so an external project cannot introduce unchecked keys or values.
    const auto appearances = ui.value("view_properties").toMap();
    for (auto i = appearances.cbegin(); i != appearances.cend(); ++i) {
        const auto separator = i.key().indexOf('/');
        if (separator <= 0 || separator == i.key().size()-1) throw Error("视图外观键需要 node/view："+i.key());
        checkedProperties(viewAppearanceSchema(project,catalog,i.key().left(separator),i.key().mid(separator+1)),{},i.value().toMap());
    }
    const auto workspaces = project.value("workspaces").toMap();
    for (auto i = workspaces.cbegin(); i != workspaces.cend(); ++i) {
        if (catalog.kind(i.key()) != "workspace") throw Error("工程引用的插件不是工作区："+i.key());
        catalog.checkedConfig(i.key(),i.value().toMap());
    }
}

QVariantMap loadProject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw Error("无法打开工程：" + file.errorString());
    if (file.size() > 256 * 1024 * 1024) throw Error("工程包超过 256 MiB");
    const auto bytes = file.readAll();
    if (!bytes.startsWith(Magic) || bytes.size() < Magic.size() + 4) throw Error("不是新版 CoRig 工程包");
    const auto compressed = bytes.mid(Magic.size());
    if (qFromBigEndian<quint32>(compressed.constData()) > 256u * 1024u * 1024u) throw Error("工程解压后超过 256 MiB");
    const auto payload = qUncompress(compressed);
    QCborParserError error;
    const auto cbor = QCborValue::fromCbor(payload, &error);
    if (error.error != QCborError::NoError || !cbor.isMap()) throw Error("工程包内容损坏");
    auto project = cbor.toVariant().toMap();
    alignUiToGrid(project);
    return project;
}

void saveProject(const QString &path, const QVariantMap &project)
{
    const auto payload = QCborValue::fromVariant(project).toCbor();
    if (payload.size() > 256 * 1024 * 1024) throw Error("工程资源超过 256 MiB");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw Error("无法保存工程：" + file.errorString());
    const auto bytes = Magic + qCompress(payload, 6);
    if (file.write(bytes) != bytes.size() || !file.commit()) throw Error("保存失败，原工程未替换：" + file.errorString());
}

QString importResource(QVariantMap &project, const QString &path)
{
    if (path.isEmpty() || path.startsWith("asset://")) return path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > resourceLimit("resource_mib") * 1024 * 1024) throw Error("资源无法读取或超过 resource_mib 额度：" + path);
    const auto bytes = file.readAll();
    const auto key = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(20)) + "/" + QFileInfo(path).fileName();
    auto assets = project.value("resources").toMap(); assets.insert(key, bytes); project.insert("resources", assets);
    return "asset://" + key;
}

QByteArray resourceBytes(const QVariantMap &project, const QString &reference)
{
    if (reference.startsWith("asset://")) {
        const auto assets = project.value("resources").toMap();
        if (!assets.contains(reference.mid(8))) throw Error("工程资源不存在：" + reference);
        return assets.value(reference.mid(8)).toByteArray();
    }
    QFile file(reference);
    if (!file.open(QIODevice::ReadOnly)) throw Error("无法读取资源：" + reference);
    return file.readAll();
}

} // namespace corig
