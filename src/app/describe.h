// CoRig AI self-description (W2: ai-api).
//
// /v3/describe is the domain view of a running instance: identity, policy,
// schema digest, and the capabilities that the current project, catalog and
// runtime actually expose.  Command signatures live in CommandRegistry; the
// capability entries here only reference operation names so callers resolve
// argument contracts from /v3/schema.

#pragma once

#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace corig {

class Controller;

// Pure input for the describe document.  Controller fills this in production;
// tests build a minimal catalog/project/runtime fixture without a GUI.
struct DescribeContext {
    QString appName;
    QString appId;
    QString appVersion;
    QString releaseHash;
    QString appMode;
    QVariantMap instance;
    bool retainOriginal = false;
    // True when the current instance accepts parameter writes. Published
    // applications keep the project structure fixed but still allow parameter
    // changes (node.configure/params.set), so this follows read-only mode.
    bool writable = false;
    int schemaVersion = 0;
    QString schemaDigest;
    QVariantMap project;
    QVariantList graphNodes;
    QVariantList registry;
    // Operation names currently reachable through Controller::command().  An
    // empty set means "no live table supplied" and capability availability
    // falls back to CommandRegistry.
    QSet<QString> commands;
    QVariantMap runtime;
};

// Builds the describe document from an explicit context.  The instance map is
// whitelisted (instance_id/pid/url/started_at/data_dir); a token supplied by
// mistake is never copied into the result.
QVariantMap buildDescribe(const DescribeContext &context);

// Production entry point.  app identity comes from Controller/runOptions; the
// instance map and the live command schema are supplied by ApiServer, which
// owns the discovery record and the authenticated request path.
QVariantMap describeDocument(const Controller &controller, const QVariantMap &instance,
                             const QVariantMap &schema);

// app.connection payload: {text,path,url}.  The text is safe to paste into a
// chat or script: it explains how to read the token but never includes it.
QVariantMap connectionDocument(const QVariantMap &record);

} // namespace corig