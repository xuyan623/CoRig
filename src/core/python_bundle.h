#pragma once

// Offline Python closure collection and payload verification (W2).
//
// A published application never borrows the developer's interpreter. Publishing
// copies the base CPython runtime into <payload>/python and resolves every
// declared plugin dependency with python/collect.py; this header owns both ends
// of that contract. collectPython() prepares the payload while publishing, and
// probePythonBundle()/verifyPythonBundle() re-check it afterwards without
// starting a GUI.
//
// The collector is deliberately a separate Python program: it evaluates
// Requires-Dist markers with the same interpreter that owns the packages, which
// is exactly the interpreter whose site-packages are copied. Nothing downloads
// and pip is never invoked.
#include "plugin_package.h"
#include "python_host.h"
#include "value.h"

namespace corig {

// One plugin's optional python block, exactly as plugin.json declares it:
//   python: {distributions: {导入名: 分发名},
//            requires:      {分发名: specifier},
//            extras:        {分发名: [extra]}}
struct PythonPluginDeclaration {
    QString package;
    QString plugin;
    QVariantMap python;
};

struct PythonBundleRequest {
    // Absolute python.exe of the developer environment that owns the packages.
    QString developerInterpreter;
    // Publish payload root; the runtime is written to <payloadRoot>/python.
    QString payloadRoot;
    // Project snapshot (.crpj). Used when plugins is empty: the declarations come
    // from the snapshot's embedded plugin packages.
    QString projectPath;
    // Explicit declarations for callers that already parsed packages, and tests.
    QVariantList plugins;
    // Override for python/collect.py; otherwise searched next to the executable
    // and around the build tree.
    QString collectorScript;
    int timeoutMs = 600000;
};

struct PythonBundleResult {
    bool ok = false;
    // Collector document: python version, distributions, files, bytes, warnings.
    QVariantMap summary;
    // {code, phase, detail}; codes reported by the collector are passed through
    // (PY_DECL_INVALID, PY_DECL_UNRESOLVED, PY_CONFLICT, PY_DEV_PATH ...).
    QVariantMap error;
    QString pythonRoot;
    QStringList warnings;
};

// Collects the closure into <payloadRoot>/python. worker.py is written from the
// compiled resource first, so the payload worker always matches this binary.
// Codes added here: PY_INTERPRETER, PY_COLLECTOR_MISSING, PY_COLLECT_FAILED.
CORIG_API PythonBundleResult collectPython(const PythonBundleRequest &request);

// Declarations embedded in a project snapshot, one entry per plugin carrying a
// python block: {package, id, python}.
CORIG_API QVariantList pythonDeclarations(const QList<PluginPackage> &packages);
CORIG_API QVariantList pythonDeclarations(const QString &projectPath);

struct PythonProbeResult {
    bool ok = false;
    // executable, prefix, base_prefix, version, site_packages, modules{...}.
    QVariantMap info;
    // {code: PY_PROBE_FAILED, detail, violations}.
    QVariantMap error;
};

// Runs <payloadRoot>/python/python.exe with -I -B -X utf8 and reports the
// interpreter, its prefix and every module's __file__/version. ok is false when a
// reported path escapes the payload or a module cannot be imported. An empty
// module list probes the modules recorded in python-runtime.json.
CORIG_API PythonProbeResult probePythonBundle(const QString &payloadRoot,
                                              const QStringList &modules = {});

// Publish-time self check: layout, runtime manifest, absence of developer paths
// and a probe of every declared module. Returns {ok, checks:[{name, ok, detail}],
// error?} and never starts a GUI.
CORIG_API QVariantMap verifyPythonBundle(const QString &payloadRoot);

// Mode-aware runtime environment. Publish uses the payload interpreter, the
// payload worker script and cwd=tempRoot. Editor returns an empty environment
// because its interpreter still comes from settings.
CORIG_API PythonEnvironment pythonEnvironmentForRun();

} // namespace corig
