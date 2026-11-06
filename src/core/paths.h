#pragma once

#include "run_mode.h"
#include "value.h"

namespace corig {
// Editor builds keep user data beside the executable; a published application
// answers with the data directory inside its portable work root beside the exe.
CORIG_API QString defaultDataDirectory();
// Called before constructing the GUI application. Editor builds probe and use
// caches beside the executable and never fall back. Publish points every cache and
// the child-process temporary directory at the work root, because the payload
// directory is engine-owned and stays read-only.
CORIG_API void configureRuntimePaths();
// Creates project, plugins and cache beside the executable and explains the
// project folder once. Repeated calls are harmless. Publish creates only its data
// root: the installer owns the payload and nothing may be written inside it.
CORIG_API void ensureDataDirectory(const QString &directory, RunMode mode);
// Removes this process's temporary directory on a clean exit. A published
// application must not leave per-process folders behind in its work root;
// the editor keeps its historical behaviour where QTemporaryFile owns the files.
CORIG_API void cleanupRuntimeTemp();
} // namespace corig
