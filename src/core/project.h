#pragma once

// A project is portable design data. Running sessions, local Python paths and
// view-window placement are deliberately outside this saved document.
#include "catalog.h"

namespace corig {

QVariantMap emptyProject(const QString &name = "未命名工程");
void validateProject(const QVariantMap &project, const Catalog &catalog);
// Appearance fields shared by placed UI items and unplaced node views. Both
// the editor write path and project loading check the same schema so a saved
// document cannot smuggle keys the editor would reject.
QVariantMap uiPropertySchema(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project);
QVariantMap checkedUiProperties(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project,
                               const QVariantMap &changes, const QVariantMap &previous = {});
// A node view that is not placed on the layout has no UI item; its appearance
// is one view-level slot per node/view pair, never per instance and never
// containing canvas geometry.
QVariantMap viewAppearance(const QVariantMap &project, const QString &node, const QString &view);
QVariantMap viewAppearanceSchema(const QVariantMap &project, const Catalog &catalog, const QString &node, const QString &view);
QVariantMap checkedViewAppearance(const QVariantMap &project, const Catalog &catalog, const QString &node, const QString &view,
                                 const QVariantMap &changes, const QVariantMap &previous = {});
// Quantize saved UI geometry to the design canvas's configurable paper grid.
void alignUiToGrid(QVariantMap &project);
QVector<QString> executionOrder(const QVariantMap &project, const Catalog &catalog);
QVariantMap loadProject(const QString &path);
void saveProject(const QString &path, const QVariantMap &project);
QString importResource(QVariantMap &project, const QString &path);
QByteArray resourceBytes(const QVariantMap &project, const QString &reference);

} // namespace corig
