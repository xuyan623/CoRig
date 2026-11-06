#pragma once

// Publishing turns one saved project into a self-contained application payload:
// a forced-embed project snapshot, the runtime tree produced by Qt's deployment
// tool, and a manifest binding the two. Publishing reads the project file and
// copies bytes; it never edits the design document, its save preferences or the
// editor session that produced it.
#include "value.h"
#include <functional>

namespace corig {

struct PublishRequest {
    QString projectPath;   // existing .crpj; never rewritten
    QString outputPath;    // payload directory; replaced atomically
    QString appId;         // stable application identity, e.g. org.example.lab
    QString appName;       // display name shown by the launcher
    QString appVersion;    // application version, independent of the CoRig build
    bool retainOriginal = false;  // also ship the untouched project file
    bool overwrite = false;       // replace an existing output directory
    QString qtPrefix;      // Qt root or windeployqt.exe; empty uses this build's Qt
    QString qmlDir;        // QML source tree for windeployqt; empty derives <exeDir>/../../src/qml
    bool singleExe = false;      // also assemble <output>.exe from the directory payload
    QString launcherPath;        // CoRig-launcher.exe; empty looks beside CoRig-publish.exe
    QString developerInterpreter; // absolute python.exe used to collect a Python payload
    // Optional stage callback for GUI progress; phase names match PublishResult::error["phase"].
    std::function<void(const QString &phase,int percent)> progress;
};

struct PublishResult {
    bool ok = false;
    QString output;
    QString releaseHash;   // SHA-256 of project/runtime.crpj
    qint64 payloadBytes = 0;
    QString artifactPath;  // single-exe artifact when singleExe was requested
    qint64 artifactBytes = 0;
    QString indexHash;     // SHA-256 of the container index
    QVariantMap python;    // collector summary when the payload bundles Python
    QVariantList pythonChecks; // verifyPythonBundle checks, or [] when Python is absent
    QStringList warnings;
    QVariantMap error;     // {code, phase, detail} when ok is false
};

// Stages, in order: prepare (snapshot), collect (runtime deployment),
// verify (runtime closure), assemble (manifest and index), commit (atomic
// rename). A failure removes the staging directory and leaves an existing
// output untouched. Stage names appear in PublishResult::error["phase"].
CORIG_API PublishResult publishApplication(const PublishRequest &request);

} // namespace corig
