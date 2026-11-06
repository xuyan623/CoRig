#pragma once

#include "value.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

namespace corig {

// Per-application parameter overlay (W2).
//
// A published application must remember the values a user dialled in without ever
// touching the shipped snapshot: only an explicit node.configure(persist),
// params.set or control commit reaches this file. The overlay is replayed onto the
// project copy the runtime loaded (loadProject -> apply -> attachUi -> engine start)
// and is never written into the .crpj, never marks the project dirty and never
// enters undo.
class CORIG_API UserParams final : public QObject {
public:
    // dataRoot is the application data root; the file lives in <dataRoot>/params.
    explicit UserParams(const QString &dataRoot, QObject *parent = nullptr);
    ~UserParams() override;

    // Absolute path of user-params.json, for diagnostics and tests.
    QString path() const;
    QString releaseHash() const;
    // The hash of the snapshot this overlay belongs to. A different hash keeps the
    // stored values (same node and parameter names still mean the same thing) and is
    // only refreshed on the next write.
    void setReleaseHash(const QString &hash);

    // False when the file is missing or unreadable. A corrupt file is renamed to
    // <path>.bad first, so the application starts from the shipped defaults.
    bool load();
    // Atomic write through QSaveFile. False when the directory or file cannot be
    // written; nothing is retried in the background.
    bool save();
    // Coalesces bursts of edits into one write 500 ms later.
    void scheduleSave();
    // Exit path: writes a pending change immediately.
    void flush();

    void setNode(const QString &node, const QString &key, const QVariant &value);
    void setControl(const QString &itemId, const QVariant &value);
    // Records the shipped value the first time a parameter is overridden, so a
    // later reset() can restore it even when the overlay entry did not exist at
    // startup (for example a parameter first changed during this session).
    void captureBaseline(const QString &node, const QString &key, const QVariant &value);
    // Removes one override after a rejected runtime change; the caller restores
    // the previous value with setNode when one existed.
    void removeNode(const QString &node, const QString &key);
    QVariant nodeValue(const QString &node, const QString &key) const;
    QVariant controlValue(const QString &itemId) const;
    // {"nodes": {node: {key: value}}, "controls": {itemId: value}}
    QVariantMap values() const;

    // Replays the overlay onto a loaded project copy. A node or parameter that no
    // longer exists, or a value whose shape differs from the declared one, is
    // skipped instead of failing startup: {"applied": n, "skipped": n}.
    QVariantMap apply(QVariantMap &project);

    // Drops every override. The overload first restores the values captured by the
    // last apply(), which is what a published runtime shows after "reset to shipped".
    void reset();
    void reset(QVariantMap &project);

private:
    bool write();
    void restore(QVariantMap &project) const;

    QString file_;
    QString releaseHash_;
    QVariantMap nodes_;      // node -> key -> value
    QVariantMap controls_;   // ui item -> value
    QVariantMap snapshots_;  // node -> key -> value as shipped, captured by apply()
    QTimer timer_;
    bool pending_ = false;
    int skipped_ = 0;
};

} // namespace corig
