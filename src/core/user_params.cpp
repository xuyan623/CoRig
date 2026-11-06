#include "user_params.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace corig {
namespace {

constexpr int kVersion = 1;
constexpr int kDebounceMs = 500;
constexpr qint64 kMaxBytes = 4 * 1024 * 1024;

// Values cross the JSON boundary, so an integer parameter comes back as a double.
// Shape compatibility therefore compares families and never exact meta types.
bool sameShape(const QVariant &declared, const QVariant &candidate)
{
    if (!declared.isValid() || !candidate.isValid()) return false;
    const auto numeric = [](const QVariant &value) {
        switch (static_cast<QMetaType::Type>(value.userType())) {
        case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
        case QMetaType::Short: case QMetaType::UShort: case QMetaType::Double: case QMetaType::Float:
            return true;
        default: return false;
        }
    };
    const auto type = static_cast<QMetaType::Type>(declared.userType());
    if (type == QMetaType::Bool) return candidate.userType() == QMetaType::Bool;
    if (type == QMetaType::QString) return candidate.userType() == QMetaType::QString;
    if (type == QMetaType::QVariantList) return candidate.userType() == QMetaType::QVariantList;
    if (type == QMetaType::QVariantMap) return candidate.userType() == QMetaType::QVariantMap;
    return numeric(declared) && numeric(candidate);
}

int nodeIndex(const QVariantList &nodes, const QString &id)
{
    for (int index = 0; index < nodes.size(); ++index)
        if (nodes.at(index).toMap().value("id").toString() == id) return index;
    return -1;
}

int itemIndex(const QVariantList &items, const QString &id)
{
    for (int index = 0; index < items.size(); ++index)
        if (items.at(index).toMap().value("id").toString() == id) return index;
    return -1;
}

QVariantMap readMap(const QJsonObject &object)
{
    QVariantMap map;
    for (auto entry = object.begin(); entry != object.end(); ++entry)
        map.insert(entry.key(), fromJsonValue(entry.value().toVariant()));
    return map;
}

QJsonObject writeMap(const QVariantMap &map)
{
    return QJsonObject::fromVariantMap(toJsonValue(map).toMap());
}

} // namespace

UserParams::UserParams(const QString &dataRoot, QObject *parent)
    : QObject(parent), file_(dataRoot.isEmpty() ? QString() : QDir(dataRoot).filePath("params/user-params.json"))
{
    timer_.setSingleShot(true);
    timer_.setInterval(kDebounceMs);
    connect(&timer_, &QTimer::timeout, this, [this] { flush(); });
}

UserParams::~UserParams() { flush(); }

QString UserParams::path() const { return file_; }

QString UserParams::releaseHash() const { return releaseHash_; }

void UserParams::setReleaseHash(const QString &hash) { releaseHash_ = hash; }

bool UserParams::load()
{
    // A caller-supplied hash survives a missing or corrupt file: it only describes
    // the snapshot currently running, which load() cannot know.
    const auto configured = releaseHash_;
    nodes_.clear();
    controls_.clear();
    snapshots_.clear();
    releaseHash_ = configured;
    if (file_.isEmpty()) return false;
    QFile file(file_);
    const auto miss = [this, configured] {
        QFile::remove(file_ + ".bad");
        QFile::rename(file_, file_ + ".bad");
        releaseHash_ = configured;
        return false;
    };
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxBytes) { file.close(); return miss(); }
    const auto bytes = file.readAll();
    file.close();
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return miss();
    const auto root = document.object();
    if (root.value("format").toString() != QStringLiteral("corig-user-params")
        || root.value("version").toInt() != kVersion) return miss();
    releaseHash_ = root.value("release_hash").toString();
    nodes_ = readMap(root.value("values").toObject());
    controls_ = readMap(root.value("controls").toObject());
    return true;
}

bool UserParams::save()
{
    pending_ = false;
    timer_.stop();
    return write();
}

bool UserParams::write()
{
    if (file_.isEmpty()) return false;
    if (!QDir().mkpath(QFileInfo(file_).absolutePath())) return false;
    QSaveFile file(file_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QJsonObject payload{
        {"format", "corig-user-params"},
        {"version", kVersion},
        {"release_hash", releaseHash_},
        {"values", writeMap(nodes_)},
        {"controls", writeMap(controls_)}};
    const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) return false;
    return true;
}

void UserParams::scheduleSave()
{
    pending_ = true;
    timer_.start();
}

void UserParams::flush()
{
    if (!pending_) return;
    if (!write()) pending_ = true;
    else pending_ = false;
}

void UserParams::setNode(const QString &node, const QString &key, const QVariant &value)
{
    if (node.isEmpty() || key.isEmpty()) return;
    auto values = nodes_.value(node).toMap();
    values.insert(key, value);
    nodes_.insert(node, values);
    scheduleSave();
}

void UserParams::setControl(const QString &itemId, const QVariant &value)
{
    if (itemId.isEmpty()) return;
    controls_.insert(itemId, value);
    scheduleSave();
}

void UserParams::captureBaseline(const QString &node, const QString &key, const QVariant &value)
{
    if (node.isEmpty() || key.isEmpty() || !value.isValid() || value.isNull()) return;
    auto snapshot = snapshots_.value(node).toMap();
    if (!snapshot.contains(key)) snapshot.insert(key, value);
    snapshots_.insert(node, snapshot);
}

void UserParams::removeNode(const QString &node, const QString &key)
{
    auto values = nodes_.value(node).toMap();
    if (!values.contains(key)) return;
    values.remove(key);
    if (values.isEmpty()) nodes_.remove(node); else nodes_.insert(node, values);
    scheduleSave();
}

QVariant UserParams::nodeValue(const QString &node, const QString &key) const
{
    return nodes_.value(node).toMap().value(key);
}

QVariant UserParams::controlValue(const QString &itemId) const { return controls_.value(itemId); }

QVariantMap UserParams::values() const { return {{"nodes", nodes_}, {"controls", controls_}}; }

QVariantMap UserParams::apply(QVariantMap &project)
{
    skipped_ = 0;
    snapshots_.clear();
    int applied = 0;
    auto nodes = project.value("nodes").toList();
    const auto ui = project.value("ui").toMap();
    const auto items = ui.value("items").toList();

    // Capture the shipped values before overwriting anything: reset() must restore
    // them even when a stored control and a stored node parameter share one key.
    for (auto entry = nodes_.cbegin(); entry != nodes_.cend(); ++entry) {
        const auto index = nodeIndex(nodes, entry.key());
        if (index < 0) continue;
        const auto config = nodes.at(index).toMap().value("config").toMap();
        const auto stored = entry.value().toMap();
        // Keep a baseline captured before this apply (a parameter changed during
        // the session): only fill values that were never recorded.
        auto snapshot = snapshots_.value(entry.key()).toMap();
        for (auto value = stored.cbegin(); value != stored.cend(); ++value)
            if (config.contains(value.key()) && !snapshot.contains(value.key()))
                snapshot.insert(value.key(), config.value(value.key()));
        snapshots_.insert(entry.key(), snapshot);
    }
    for (auto entry = controls_.cbegin(); entry != controls_.cend(); ++entry) {
        const auto index = itemIndex(items, entry.key());
        if (index < 0) continue;
        const auto node = items.at(index).toMap().value("node").toString();
        const auto position = nodeIndex(nodes, node);
        if (position < 0) continue;
        const auto config = nodes.at(position).toMap().value("config").toMap();
        if (!config.contains(QStringLiteral("value"))) continue;
        auto snapshot = snapshots_.value(node).toMap();
        if (!snapshot.contains(QStringLiteral("value"))) snapshot.insert(QStringLiteral("value"), config.value(QStringLiteral("value")));
        snapshots_.insert(node, snapshot);
    }

    for (auto entry = nodes_.cbegin(); entry != nodes_.cend(); ++entry) {
        const auto stored = entry.value().toMap();
        const auto index = nodeIndex(nodes, entry.key());
        if (index < 0) { skipped_ += stored.size(); continue; }
        auto node = nodes.at(index).toMap();
        auto config = node.value("config").toMap();
        for (auto value = stored.cbegin(); value != stored.cend(); ++value) {
            if (!config.contains(value.key()) || !sameShape(config.value(value.key()), value.value())) { ++skipped_; continue; }
            config[value.key()] = value.value();
            ++applied;
        }
        node["config"] = config;
        nodes[index] = node;
    }

    // A stored control names a UI item, the item names its node, and the runtime
    // value lives in that node's "value" parameter. An explicit node value wins.
    for (auto entry = controls_.cbegin(); entry != controls_.cend(); ++entry) {
        const auto index = itemIndex(items, entry.key());
        if (index < 0) { ++skipped_; continue; }
        const auto nodeId = items.at(index).toMap().value("node").toString();
        const auto position = nodeIndex(nodes, nodeId);
        if (nodeId.isEmpty() || position < 0) { ++skipped_; continue; }
        auto node = nodes.at(position).toMap();
        auto config = node.value("config").toMap();
        if (!config.contains(QStringLiteral("value")) || !sameShape(config.value(QStringLiteral("value")), entry.value())) { ++skipped_; continue; }
        if (nodes_.value(nodeId).toMap().contains(QStringLiteral("value"))) continue;
        config[QStringLiteral("value")] = entry.value();
        node["config"] = config;
        nodes[position] = node;
        ++applied;
    }

    project["nodes"] = nodes;
    return {{"applied", applied}, {"skipped", skipped_}};
}

void UserParams::reset()
{
    nodes_.clear();
    controls_.clear();
    snapshots_.clear();
    skipped_ = 0;
    scheduleSave();
}

void UserParams::reset(QVariantMap &project)
{
    restore(project);
    reset();
}

void UserParams::restore(QVariantMap &project) const
{
    if (snapshots_.isEmpty()) return;
    auto nodes = project.value("nodes").toList();
    for (auto entry = snapshots_.cbegin(); entry != snapshots_.cend(); ++entry) {
        const auto index = nodeIndex(nodes, entry.key());
        if (index < 0) continue;
        auto node = nodes.at(index).toMap();
        auto config = node.value("config").toMap();
        const auto stored = entry.value().toMap();
        for (auto value = stored.cbegin(); value != stored.cend(); ++value) config.insert(value.key(), value.value());
        node["config"] = config;
        nodes[index] = node;
    }
    project["nodes"] = nodes;
}

} // namespace corig
