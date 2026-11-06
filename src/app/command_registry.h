// CoRig command registry.
//
// W1 (ai-api): a read-only, declarative description of the command surface that
// Controller::schema() exposes today.  The registry adds the information that
// the legacy schema does not carry: argument types/required/default/enum/range,
// async completion, a partition for discovery, a publish-mode policy, and the
// stable protocol error codes each command can report.
//
// The registry is intentionally data-only: it does not execute commands and it
// is not wired into Controller in W1.  apiVersion() must stay in sync with the
// /v3 schema version and with Controller::schema().
//
// Partition vocabulary (t4 design): read = inspection/discovery, control =
// runtime/window/interaction control, authoring = project/graph/UI/property
// editing, admin = plugin/library/update/resource/preference management.
//
// Publish policy is independent of partition.  Unknown operation names are
// always denied by publishDecision(); they never fall back to allow.
//
// Stable protocol error codes used in signatures:
//   CORIG_UNKNOWN_COMMAND, CORIG_ARG_INVALID, CORIG_PRECONDITION,
//   CORIG_READ_ONLY, CORIG_EXPORT_DISABLED, CORIG_JOB_MISSING,
//   CORIG_JOB_FAILED, CORIG_NOT_FOUND, CORIG_NOT_CANCELLABLE.
// W1 does not replace the existing human-readable Controller errors; the codes
// are the machine-readable contract that W2 will map onto those failures.

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace corig {

enum class PublishPolicy {
    Allow,
    Deny,
    Conditional,
};

struct PublishDecision {
    PublishPolicy policy = PublishPolicy::Deny;
    QString reason;

    bool allowed() const;
    bool conditional() const;
    QString name() const;
};

class CommandRegistry {
public:
    static const CommandRegistry &instance();

    static int apiVersion();

    QJsonObject signatures() const;
    QJsonObject document() const;
    QString canonicalJson() const;
    QString digest() const;

    PublishDecision publishDecision(const QString &operation) const;
    bool contains(const QString &operation) const;

    QStringList operationNames() const;
    QStringList unknownTypeArgs() const;
    QStringList partitions() const;

private:
    CommandRegistry();

    QJsonObject signatures_;
    QHash<QString, PublishDecision> publish_;
};

} // namespace corig
