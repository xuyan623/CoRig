#include <QtTest>
#include "app/describe.h"

using namespace corig;

// The describe document is the contract an external AI reads before it calls
// anything: instance identity must never leak the token, and the export
// capability must match the commands the instance actually exposes.
class DescribeTest final : public QObject
{
    Q_OBJECT
private slots:
    void instanceNeverLeaksToken()
    {
        DescribeContext context;
        context.instance = QVariantMap{{"instance_id","abc"},{"pid",42},{"url","http://127.0.0.1:5000"},
                                       {"token","SECRET-TOKEN"},{"data_dir","D:/app.data"}};
        const auto document = buildDescribe(context);
        const auto instance = document.value("instance").toMap();
        QCOMPARE(instance.value("instance_id").toString(), QString("abc"));
        QCOMPARE(instance.contains("token"), false);
        QCOMPARE(document.value("instance").toMap().contains("token"), false);
    }

    void exportCapabilityFollowsRetainAndCommands()
    {
        DescribeContext context;
        context.retainOriginal = false;
        QVERIFY(buildDescribe(context).value("capabilities").toMap().value("exports").toList().isEmpty());

        context.retainOriginal = true;
        context.commands = QSet<QString>{"project.get"};
        auto exports = buildDescribe(context).value("capabilities").toMap().value("exports").toList();
        QCOMPARE(exports.size(), 1);
        auto entry = exports.first().toMap();
        QCOMPARE(entry.value("op").toString(), QString("project.export_original"));
        QCOMPARE(entry.value("available").toBool(), false);
        QCOMPARE(entry.value("reason").toString(), QString("命令尚未接入"));

        context.commands.insert("project.export_original");
        exports = buildDescribe(context).value("capabilities").toMap().value("exports").toList();
        QCOMPARE(exports.first().toMap().value("available").toBool(), true);
    }

    void connectionTextOmitsToken()
    {
        const QVariantMap record{{"discovery","D:/app.data/data/.sessions/api.json"},
                                 {"url","http://127.0.0.1:5123"},
                                 {"token","SECRET-TOKEN"},{"app_name","FunctionLab"}};
        const auto connection = connectionDocument(record);
        const auto text = connection.value("text").toString();
        QVERIFY(text.contains("FunctionLab"));
        QVERIFY(text.contains("api.json"));
        QVERIFY(!text.contains("SECRET-TOKEN"));
        QCOMPARE(connection.value("path").toString(), QString("D:/app.data/data/.sessions/api.json"));
    }
};

QTEST_MAIN(DescribeTest)
#include "describe_test.moc"
