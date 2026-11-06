#pragma once

// Package workers own their QProcess on a native thread. Callers exchange
// futures and immutable events; node state stays separate inside each worker.
#include "value.h"
#include <functional>
#include <memory>
#include <mutex>

namespace corig {

// Where a worker comes from. The editor borrows the interpreter the user picked
// in settings and ships the worker script as a compiled resource. A published
// application owns a payload runtime instead: the interpreter, the worker script
// and every imported module must stay inside payloadRoot, and the worker runs
// with -I -X utf8 so the developer machine cannot leak into it.
struct PythonEnvironment {
    QString interpreter;
    // Absolute script file. Empty keeps the compiled :/worker.py path.
    QString workerScript;
    // Non-empty marks a bundled runtime; all paths are then asserted inside it.
    QString payloadRoot;
    // Working directory of the worker process; empty keeps the host process cwd.
    QString workingDirectory;
    // Pass -I -B -X utf8. Implied by a bundled runtime.
    bool isolated = false;
    bool bundled() const { return !payloadRoot.isEmpty(); }
};

class PythonHost {
public:
    using Events = std::function<void(const QVariantMap &)>;
    explicit PythonHost(std::function<bool()> interrupted, Events events = {});
    ~PythonHost();
    void selectInterpreter(const QString &path);
    // Installs a complete environment (bundled runtime or editor interpreter).
    // Rejects interpreters or worker scripts outside payloadRoot.
    void setEnvironment(const PythonEnvironment &environment);
    PythonEnvironment environment() const;
    QString interpreter() const;
    QVariantMap call(const QVariantMap &request);
    QVariantMap task(const QString &id) const;
    QVariantMap cancelTask(const QString &id);
    void close();
private:
    struct Worker;
    void event(const QString &package, quint64 generation, const QVariantMap &message);
    std::function<bool()> interrupted_;
    Events events_;
    mutable std::mutex mutex_;
    PythonEnvironment environment_;
    QMap<QString,std::shared_ptr<Worker>> workers_;
    QMap<QString,QVariantMap> tasks_;
    QMap<QString,QString> taskPackages_;
    QMap<QString,quint64> taskGenerations_;
    quint64 nextWorker_ = 0;
};

// True when path equals root or lives below it (case-insensitive on Windows).
CORIG_API bool pythonPathInside(const QString &path, const QString &root);

}
