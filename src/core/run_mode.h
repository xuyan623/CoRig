#pragma once

#include "value.h"

#include <QByteArray>
#include <QString>

namespace corig {

// How the current process was started. Editor owns projects and may write beside
// the executable; Publish runs an installed application whose payload directory
// belongs to the installer and may be read-only.
enum class RunMode { Editor, Publish };

// Structured reason why a payload was rejected. W1 defines one code,
// PAYLOAD_INCOMPLETE: every manifest or layout mismatch reports it, so callers
// never have to guess which rule failed from a translated message.
struct RunModeError {
    QString code;
    QString message;
    bool isEmpty() const { return code.isEmpty(); }
};

// Paths owned by one process. Publishing never writes into payloadRoot; the
// editor keeps the historical layout beside the executable.
struct RuntimePaths {
    QString payloadRoot;
    QString dataRoot;
    QString cacheRoot;
    QString tempRoot;
    QString pluginRoot;
    QString pluginCacheRoot;
    QString crashRoot;
    QString pythonRoot;
};

// Everything the process needs to know about how it was started. Resolved once,
// before QGuiApplication exists, and never mutated afterwards.
struct RunOptions {
    RunMode mode = RunMode::Editor;
    // Identity of the published application; empty in editor mode.
    QString appId;
    QString appName;
    QString appVersion;
    QString releaseHash;
    // Kept from the manifest so a later stage can decide whether the payload must
    // preserve the original project file; W1 only carries the flag.
    bool retainOriginal = false;
    // Roots in the order W1 names them. Publish fills all of them from the payload
    // and the portable work root beside the executable; Editor reproduces the paths
    // used before W1.
    QString payloadRoot;
    QString dataRoot;
    QString cacheRoot;
    QString tempRoot;
    QString pluginRoot;
    QString pluginCacheRoot;
    QString crashRoot;
    QString pythonRoot;
    // Script handed to the worker interpreter; a compiled resource in both modes.
    QString workerScript;
};

// Pure decision function: no filesystem, environment or global state. exePath must
// be the canonical path of the running executable. On success fills mode, identity
// and payloadRoot; the portable roots come from computePublishPaths.
// Every rejected manifest reports PAYLOAD_INCOMPLETE and leaves *options untouched.
CORIG_API bool detectRunMode(const QString &exePath, const QByteArray &manifestBytes,
                             RunOptions *options, RunModeError *error);

// Pure: completes a validated publish payload with the roots inside the portable
// work root. A published application keeps everything beside its executable, so
// appRoot is normally <exeDir>/<exeBase>.data; every generated root is asserted
// to stay inside it.
CORIG_API bool computePublishPaths(const QString &appRoot, RunOptions *options, RunModeError *error);

// Resolves and installs the process options: locates the executable, validates
// <exeDir>/../manifest.json when one exists and fills every root. Never throws.
// A manifest that claims to be a CoRig payload but fails validation reports
// PAYLOAD_INCOMPLETE and must stop startup; no manifest means Editor, which keeps
// editor builds working exactly as before.
CORIG_API bool initializeRunOptions(RunModeError *error);

// Read-only view of initializeRunOptions(). Editor defaults until it runs, so
// tools that never call it (uninstall, cli) keep their previous behaviour.
CORIG_API const RunOptions &runOptions();
CORIG_API RuntimePaths runtimePaths();

} // namespace corig
