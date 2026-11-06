// Focused contract tests for the W1 command registry.  The registry is the
// single source of truth for command signatures, async semantics, partitions,
// publish policy, and the canonical digest; these tests intentionally do not
// touch Controller or the GUI.

#include <QtTest>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include "app/command_registry.h"

using namespace corig;

class CommandRegistryTest : public QObject {
    Q_OBJECT

private slots:
    void coversSchemaExactly();
    void everySignatureIsWellFormed();
    void publishPlanMatches();
    void partitionsAreClosedSet();
    void batchIsTwoPhase();
    void unknownTypesAreContained();
    void canonicalJsonAndDigestAreStable();
    void unknownOperationIsDenied();
};

void CommandRegistryTest::coversSchemaExactly()
{
    const QStringList expected{QStringLiteral("app.connection"), QStringLiteral("app.quit"), QStringLiteral("app.restart"), QStringLiteral("app.state"), QStringLiteral("batch"), QStringLiteral("capture"), QStringLiteral("clipboard.read"), QStringLiteral("container.layout"), QStringLiteral("controls.click"), QStringLiteral("controls.commit"), QStringLiteral("controls.focus"), QStringLiteral("controls.get"), QStringLiteral("controls.key"), QStringLiteral("controls.list"), QStringLiteral("controls.pointer"), QStringLiteral("controls.set"), QStringLiteral("editor.add"), QStringLiteral("editor.configure"), QStringLiteral("editor.copy"), QStringLiteral("editor.paste"), QStringLiteral("editor.place"), QStringLiteral("editor.plan"), QStringLiteral("editor.remove"), QStringLiteral("editor.reorder"), QStringLiteral("editor.replace"), QStringLiteral("editor.state"), QStringLiteral("editor.withdraw"), QStringLiteral("graph.activity"), QStringLiteral("graph.connection.options"), QStringLiteral("graph.fit"), QStringLiteral("graph.node.add"), QStringLiteral("graph.node.move"), QStringLiteral("graph.node.patch"), QStringLiteral("graph.node.remove"), QStringLiteral("graph.node.rotate"), QStringLiteral("graph.overlaps"), QStringLiteral("graph.select"), QStringLiteral("graph.wire.add"), QStringLiteral("graph.wire.handles"), QStringLiteral("graph.wire.patch"), QStringLiteral("graph.wire.remove"), QStringLiteral("history.redo"), QStringLiteral("history.undo"), QStringLiteral("io.ports"), QStringLiteral("job.cancel"), QStringLiteral("job.get"), QStringLiteral("job.list"), QStringLiteral("library.configure"), QStringLiteral("library.favorite"), QStringLiteral("library.preview"), QStringLiteral("library.search"), QStringLiteral("library.state"), QStringLiteral("log.clear"), QStringLiteral("log.configure"), QStringLiteral("log.export"), QStringLiteral("log.query"), QStringLiteral("node.action"), QStringLiteral("node.configure"), QStringLiteral("node.configure.plan"), QStringLiteral("node.state"), QStringLiteral("params.get"), QStringLiteral("params.reset"), QStringLiteral("params.set"), QStringLiteral("plugin.directory"), QStringLiteral("plugin.enable"), QStringLiteral("plugin.export"), QStringLiteral("plugin.export.dialog"), QStringLiteral("plugin.import"), QStringLiteral("plugin.remove"), QStringLiteral("plugin.settings"), QStringLiteral("plugin.state"), QStringLiteral("plugin.tree"), QStringLiteral("preferences.set"), QStringLiteral("project.export"), QStringLiteral("project.export_original"), QStringLiteral("project.get"), QStringLiteral("project.info"), QStringLiteral("project.new"), QStringLiteral("project.open"), QStringLiteral("project.options"), QStringLiteral("project.rename"), QStringLiteral("project.save"), QStringLiteral("project.validate"), QStringLiteral("property.commit"), QStringLiteral("property.discard"), QStringLiteral("property.drafts"), QStringLiteral("property.get"), QStringLiteral("property.stage"), QStringLiteral("property.text"), QStringLiteral("publish.cancel"), QStringLiteral("publish.log"), QStringLiteral("publish.start"), QStringLiteral("publish.state"), QStringLiteral("python.probe"), QStringLiteral("registry"), QStringLiteral("resources.get"), QStringLiteral("resources.set"), QStringLiteral("runtime.debug"), QStringLiteral("runtime.emit"), QStringLiteral("runtime.pause"), QStringLiteral("runtime.reset"), QStringLiteral("runtime.start"), QStringLiteral("runtime.state"), QStringLiteral("runtime.step"), QStringLiteral("runtime.stop"), QStringLiteral("schema"), QStringLiteral("settings.open"), QStringLiteral("sync.apply"), QStringLiteral("sync.plan"), QStringLiteral("ui.container.add"), QStringLiteral("ui.item.move"), QStringLiteral("ui.item.patch"), QStringLiteral("ui.item.remove"), QStringLiteral("ui.item.schema"), QStringLiteral("ui.update"), QStringLiteral("ui.view.add"), QStringLiteral("ui.view.appearance"), QStringLiteral("ui.view.bind"), QStringLiteral("update.check"), QStringLiteral("update.install"), QStringLiteral("update.state"), QStringLiteral("view.actions"), QStringLiteral("view.call"), QStringLiteral("view.read"), QStringLiteral("window.close"), QStringLiteral("window.configure"), QStringLiteral("window.pin"), QStringLiteral("workspace.close"), QStringLiteral("workspace.configure"), QStringLiteral("workspace.detach"), QStringLiteral("workspace.list"), QStringLiteral("workspace.move"), QStringLiteral("workspace.open"), QStringLiteral("workspace.remove")};
    const auto &registry = CommandRegistry::instance();
    const QStringList actual = registry.operationNames();
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual, QStringList(expected));
    QCOMPARE(registry.apiVersion(), 3);
    for (const QString &operation : expected) QVERIFY2(registry.contains(operation), qPrintable(operation));
    QVERIFY(!registry.contains(QStringLiteral("does.not.exist")));
}

void CommandRegistryTest::everySignatureIsWellFormed()
{
    const auto &registry = CommandRegistry::instance();
    const QJsonObject signatures = registry.signatures();
    QCOMPARE(static_cast<int>(signatures.size()), 134);
    const QSet<QString> types{QStringLiteral("string"), QStringLiteral("integer"), QStringLiteral("number"),
        QStringLiteral("boolean"), QStringLiteral("object"), QStringLiteral("array"), QStringLiteral("unknown")};
    const QSet<QString> partitions{QStringLiteral("read"), QStringLiteral("control"),
        QStringLiteral("authoring"), QStringLiteral("admin")};
    for (auto command = signatures.cbegin(); command != signatures.cend(); ++command) {
        const QJsonObject signature = command.value().toObject();
        QVERIFY2(signature.contains(QStringLiteral("args")), qPrintable(command.key()));
        QVERIFY2(signature.value(QStringLiteral("async")).isBool(), qPrintable(command.key()));
        QVERIFY2(signature.value(QStringLiteral("partition")).isString(), qPrintable(command.key()));
        QVERIFY2(partitions.contains(signature.value(QStringLiteral("partition")).toString()), qPrintable(command.key()));
        const QJsonObject publish = signature.value(QStringLiteral("publish")).toObject();
        QVERIFY2(publish.value(QStringLiteral("policy")).isString(), qPrintable(command.key()));
        QVERIFY2(publish.value(QStringLiteral("reason")).isString(), qPrintable(command.key()));
        QVERIFY2(!publish.value(QStringLiteral("reason")).toString().isEmpty(), qPrintable(command.key()));
        const QJsonArray errors = signature.value(QStringLiteral("errors")).toArray();
        QVERIFY2(!errors.isEmpty(), qPrintable(command.key()));
        for (const QJsonValue &error : errors) {
            QVERIFY2(error.isString(), qPrintable(command.key()));
            QVERIFY2(error.toString().startsWith(QStringLiteral("CORIG_")), qPrintable(command.key()));
        }
        const QJsonObject args = signature.value(QStringLiteral("args")).toObject();
        for (auto arg = args.cbegin(); arg != args.cend(); ++arg) {
            const QJsonObject spec = arg.value().toObject();
            QVERIFY2(types.contains(spec.value(QStringLiteral("type")).toString()), qPrintable(command.key() + "." + arg.key()));
            QVERIFY2(spec.value(QStringLiteral("required")).isBool(), qPrintable(command.key() + "." + arg.key()));
            if (spec.contains(QStringLiteral("default")))
                QVERIFY2(!spec.value(QStringLiteral("required")).toBool(), qPrintable(command.key() + "." + arg.key()));
            if (spec.contains(QStringLiteral("enum")))
                QVERIFY2(spec.value(QStringLiteral("enum")).isArray() && !spec.value(QStringLiteral("enum")).toArray().isEmpty(),
                         qPrintable(command.key() + "." + arg.key()));
            if (spec.contains(QStringLiteral("min")))
                QVERIFY2(spec.value(QStringLiteral("min")).isDouble(), qPrintable(command.key() + "." + arg.key()));
            if (spec.contains(QStringLiteral("max")))
                QVERIFY2(spec.value(QStringLiteral("max")).isDouble(), qPrintable(command.key() + "." + arg.key()));
            if (spec.contains(QStringLiteral("unit")))
                QVERIFY2(spec.value(QStringLiteral("unit")).isString() && !spec.value(QStringLiteral("unit")).toString().isEmpty(),
                         qPrintable(command.key() + "." + arg.key()));
        }
    }
    const QJsonObject capture = signatures.value(QStringLiteral("capture")).toObject().value(QStringLiteral("args")).toObject();
    QCOMPARE(capture.value(QStringLiteral("width")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("integer"));
    QCOMPARE(capture.value(QStringLiteral("width")).toObject().value(QStringLiteral("min")).toDouble(), 1.0);
    QCOMPARE(capture.value(QStringLiteral("width")).toObject().value(QStringLiteral("unit")).toString(), QStringLiteral("px"));
    const QJsonObject preferences = signatures.value(QStringLiteral("preferences.set")).toObject().value(QStringLiteral("args")).toObject();
    QCOMPARE(static_cast<int>(preferences.value(QStringLiteral("theme")).toObject().value(QStringLiteral("enum")).toArray().size()), 2);
    const QJsonObject projectOpen = signatures.value(QStringLiteral("project.open")).toObject().value(QStringLiteral("args")).toObject();
    QVERIFY(projectOpen.value(QStringLiteral("path")).toObject().value(QStringLiteral("required")).toBool());
    const QJsonObject nodeConfigure = signatures.value(QStringLiteral("node.configure")).toObject().value(QStringLiteral("args")).toObject();
    QCOMPARE(nodeConfigure.value(QStringLiteral("persist")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("boolean"));
}

void CommandRegistryTest::publishPlanMatches()
{
    const auto &registry = CommandRegistry::instance();
    const QSet<QString> expectedAllow{QStringLiteral("app.quit"), QStringLiteral("app.restart"), QStringLiteral("app.state"), QStringLiteral("capture"), QStringLiteral("controls.click"), QStringLiteral("controls.commit"), QStringLiteral("controls.focus"), QStringLiteral("controls.get"), QStringLiteral("controls.key"), QStringLiteral("controls.list"), QStringLiteral("controls.pointer"), QStringLiteral("controls.set"), QStringLiteral("io.ports"), QStringLiteral("job.cancel"), QStringLiteral("job.get"), QStringLiteral("job.list"), QStringLiteral("log.clear"), QStringLiteral("log.configure"), QStringLiteral("log.export"), QStringLiteral("log.query"), QStringLiteral("node.action"), QStringLiteral("node.configure"), QStringLiteral("node.state"), QStringLiteral("registry"), QStringLiteral("resources.get"), QStringLiteral("runtime.debug"), QStringLiteral("runtime.emit"), QStringLiteral("runtime.pause"), QStringLiteral("runtime.reset"), QStringLiteral("runtime.start"), QStringLiteral("runtime.state"), QStringLiteral("runtime.step"), QStringLiteral("runtime.stop"), QStringLiteral("schema"), QStringLiteral("settings.open"), QStringLiteral("view.actions"), QStringLiteral("view.call"), QStringLiteral("view.read"), QStringLiteral("window.close"), QStringLiteral("workspace.close"), QStringLiteral("workspace.list"), QStringLiteral("workspace.open"), QStringLiteral("app.connection"), QStringLiteral("params.get"), QStringLiteral("params.reset"), QStringLiteral("params.set")};
    int allowCount = 0, conditionalCount = 0, denyCount = 0;
    for (const QString &operation : registry.operationNames()) {
        const PublishDecision decision = registry.publishDecision(operation);
        if (expectedAllow.contains(operation)) {
            QVERIFY2(decision.policy == PublishPolicy::Allow, qPrintable(operation));
            QVERIFY2(decision.allowed(), qPrintable(operation));
            QVERIFY2(!decision.conditional(), qPrintable(operation));
            ++allowCount;
        } else if (operation == QStringLiteral("preferences.set") || operation == QStringLiteral("project.export_original")) {
            QVERIFY2(decision.policy == PublishPolicy::Conditional, qPrintable(operation));
            QVERIFY2(decision.conditional(), qPrintable(operation));
            ++conditionalCount;
        } else {
            QVERIFY2(decision.policy == PublishPolicy::Deny, qPrintable(operation));
            QVERIFY2(!decision.allowed(), qPrintable(operation));
            QVERIFY2(!decision.reason.isEmpty(), qPrintable(operation));
            ++denyCount;
        }
    }
    QCOMPARE(allowCount, 46);
    QCOMPARE(conditionalCount, 2);
    QCOMPARE(denyCount, 86);
    // A representative from every denied family must remain closed in publish mode.
    for (const QString &operation : {QStringLiteral("publish.start"), QStringLiteral("project.export"),
             QStringLiteral("project.get"), QStringLiteral("plugin.import"), QStringLiteral("graph.node.add"),
             QStringLiteral("editor.add"), QStringLiteral("ui.update"), QStringLiteral("property.commit"),
             QStringLiteral("history.undo"), QStringLiteral("library.search"), QStringLiteral("sync.apply"),
             QStringLiteral("update.check"), QStringLiteral("clipboard.read"), QStringLiteral("workspace.configure"),
             QStringLiteral("window.configure"), QStringLiteral("node.configure.plan"), QStringLiteral("python.probe"),
             QStringLiteral("batch")})
        QVERIFY2(registry.publishDecision(operation).policy == PublishPolicy::Deny, qPrintable(operation));
    QVERIFY(registry.publishDecision(QStringLiteral("app.state")).allowed());
    QVERIFY(registry.publishDecision(QStringLiteral("log.query")).allowed());
    QVERIFY(registry.publishDecision(QStringLiteral("runtime.state")).allowed());
    QVERIFY(registry.publishDecision(QStringLiteral("schema")).allowed());
    QVERIFY(registry.publishDecision(QStringLiteral("registry")).allowed());
    QVERIFY(registry.publishDecision(QStringLiteral("preferences.set")).conditional());
    QVERIFY(registry.publishDecision(QStringLiteral("project.export_original")).conditional());
    QVERIFY(registry.publishDecision(QStringLiteral("app.connection")).allowed());
    QVERIFY(registry.publishDecision(QStringLiteral("params.set")).allowed());
}

void CommandRegistryTest::partitionsAreClosedSet()
{
    const auto &registry = CommandRegistry::instance();
    QCOMPARE(static_cast<int>(registry.partitions().size()), 4);
    QStringList expectedPartitions = registry.partitions();
    expectedPartitions.sort();
    const QStringList partitionList{QStringLiteral("admin"), QStringLiteral("authoring"),
                                    QStringLiteral("control"), QStringLiteral("read")};
    QCOMPARE(expectedPartitions, partitionList);
    QSet<QString> seen;
    for (const QString &operation : registry.operationNames()) {
        const QString partition = registry.signatures().value(operation).toObject().value(QStringLiteral("partition")).toString();
        QVERIFY2(registry.partitions().contains(partition), qPrintable(operation));
        seen.insert(partition);
    }
    QStringList seenList = seen.values();
    seenList.sort();
    QCOMPARE(seenList, partitionList);
}

void CommandRegistryTest::batchIsTwoPhase()
{
    const auto &registry = CommandRegistry::instance();
    const QJsonObject batch = registry.signatures().value(QStringLiteral("batch")).toObject();
    QCOMPARE(batch.value(QStringLiteral("two_phase")).toBool(), true);
    QCOMPARE(batch.value(QStringLiteral("publish")).toObject().value(QStringLiteral("policy")).toString(), QStringLiteral("deny"));
    QVERIFY(registry.publishDecision(QStringLiteral("batch")).policy == PublishPolicy::Deny);
    QVERIFY(!registry.publishDecision(QStringLiteral("batch")).conditional());
    for (const QString &operation : registry.operationNames())
        if (operation != QStringLiteral("batch"))
            QVERIFY2(!registry.signatures().value(operation).toObject().contains(QStringLiteral("two_phase")), qPrintable(operation));
}

void CommandRegistryTest::unknownTypesAreContained()
{
    const auto &registry = CommandRegistry::instance();
    const QStringList unknown = registry.unknownTypeArgs();
    QVERIFY(!unknown.isEmpty());
    QVERIFY2(unknown.size() <= 8, qPrintable(QString::number(unknown.size())));
    QCOMPARE(unknown, QStringList({QStringLiteral("controls.set.value"), QStringLiteral("log.export.batch"),
                                   QStringLiteral("log.query.batch"), QStringLiteral("params.set.value"),
                                   QStringLiteral("runtime.step.source")}));
    for (const QString &entry : unknown) {
        const int dot = entry.lastIndexOf(QLatin1Char('.'));
        QVERIFY2(dot > 0 && dot < entry.size() - 1, qPrintable(entry));
        const QString operation = entry.left(dot);
        const QString argument = entry.mid(dot + 1);
        QVERIFY2(registry.contains(operation), qPrintable(entry));
        const QJsonObject args = registry.signatures().value(operation).toObject().value(QStringLiteral("args")).toObject();
        QCOMPARE(args.value(argument).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("unknown"));
    }
}

void CommandRegistryTest::canonicalJsonAndDigestAreStable()
{
    const auto &registry = CommandRegistry::instance();
    const QString canonical = registry.canonicalJson();
    QVERIFY(!canonical.contains(QLatin1Char('\n')));
    QVERIFY2(canonical.startsWith(QStringLiteral("{\"api_version\":3,\"signatures\":{")), qPrintable(canonical.left(80)));
    QJsonParseError parseError{};
    const QJsonDocument parsed = QJsonDocument::fromJson(canonical.toUtf8(), &parseError);
    QVERIFY(parseError.error == QJsonParseError::NoError);
    QCOMPARE(parsed.object().value(QStringLiteral("api_version")).toInt(), 3);
    QCOMPARE(static_cast<int>(parsed.object().value(QStringLiteral("signatures")).toObject().size()), 134);
    QCOMPARE(registry.canonicalJson(), canonical);
    const QString digest = registry.digest();
    QCOMPARE(static_cast<int>(digest.size()), 64);
    QCOMPARE(digest, QString::fromLatin1(QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex()));
    QCOMPARE(registry.digest(), digest);
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(digest).hasMatch());
}

void CommandRegistryTest::unknownOperationIsDenied()
{
    const auto &registry = CommandRegistry::instance();
    const PublishDecision decision = registry.publishDecision(QStringLiteral("does.not.exist"));
    QVERIFY(decision.policy == PublishPolicy::Deny);
    QVERIFY(!decision.allowed());
    QVERIFY(!decision.conditional());
    QVERIFY(decision.reason.contains(QStringLiteral("未知操作")));
    QCOMPARE(decision.name(), QStringLiteral("deny"));
}

QTEST_GUILESS_MAIN(CommandRegistryTest)
#include "command_registry_test.moc"
