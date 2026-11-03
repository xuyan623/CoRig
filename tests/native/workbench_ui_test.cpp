// Real Qt Quick windows and production controls receive native pointer/key
// events. This verifies gesture state independently of desktop focus/input.
#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QJSValue>
#include <QMimeData>
#include <QJsonDocument>
#include <QFile>
#include <cmath>
#include "app/controller.h"
#include "app/workspaces.h"
#include "quick/renderers.h"
#include "quick/icon_provider.h"

class WorkbenchUiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void bubbleReturnsToSocketAndMother();
    void rightClickCancelsHeldObjects();
    void containerDragUsesEditorGestures();
    void externalDropsReachContainerSlots();
    void outsideCanvasObjectsRemainDraggable();
    void nestedControlsAlignWithConfigurableGrid();
    void synchronizationRequiresChecklist();
    void graphResizeAndDeleteWarning();
    void designAndRuntimeShareContainerLayout();
    void publicEditorPresentationRoundTrip();
    void externalControlEditsReachRealNodes();
    void switchesPressedFeedbackAndCombinedEngineControl();
    void localPluginDetailsStayVisibleWhenDisabled();
    void propertyDraftsAndPortsAreAtomic();
    void qmlContainersAndWorkspacesTravelWithProject();
    void nodeViewBridgeContract();
    void sixDofViewActionsMatchCall();
    void sixDofAppearanceKeepsCamera();
    void placedViewBridgeUsesPropertySession();
    void viewBridgePreviewStaysReadOnly();
private:
    QVariant run(const QString &op, const QVariantMap &args = {});
    QQuickItem *findItem(const QString &name, QQuickItem *from = nullptr);
    QPoint portPoint(const QString &node, const QString &port);
    QVariantMap mapProperty(QObject *object, const char *name);
    void click(const QPoint &point, Qt::MouseButton button = Qt::LeftButton);
    std::unique_ptr<QTemporaryDir> folder_;
    std::unique_ptr<corig::Controller> app_;
    std::unique_ptr<QQmlApplicationEngine> qml_;
    QQuickWindow *window_ = nullptr;
    QQuickItem *graph_ = nullptr;
};

void WorkbenchUiTest::initTestCase()
{
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<corig::NodeSurface>("CoRig.Native",1,0,"NodeSurface");
    qmlRegisterType<corig::WireLayer>("CoRig.Native",1,0,"WireLayer");
    qmlRegisterType<corig::NativePlot>("CoRig.Native",1,0,"NativePlot");
    qmlRegisterType<corig::NativeAttitude>("CoRig.Native",1,0,"NativeAttitude");
    qmlRegisterType<corig::NativeSixDof>("CoRig.Native",1,0,"NativeSixDof");
}

void WorkbenchUiTest::init()
{
    folder_ = std::make_unique<QTemporaryDir>();
    app_ = std::make_unique<corig::Controller>(folder_->path());
    qml_ = std::make_unique<QQmlApplicationEngine>();qml_->addImageProvider("icons",new corig::IconProvider);
    app_->attachUi(qml_.get());
    const auto workspace = qobject_cast<corig::Workspaces *>(app_->workspaces());
    graph_ = workspace->page("graph");window_ = graph_->window();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    run("graph.node.add",{{"id","source"},{"plugin","control.number"},{"position",QVariantList{20.,40.}}});
    run("graph.node.add",{{"id","target"},{"plugin","function.exp"},{"position",QVariantList{360.,40.}}});
    run("graph.node.add",{{"id","text"},{"plugin","control.text"},{"position",QVariantList{20.,240.}}});
    run("graph.fit");QTest::qWait(30);
}

void WorkbenchUiTest::cleanup()
{
    app_->shutdown();qml_.reset();app_.reset();folder_.reset();graph_ = nullptr;window_ = nullptr;
}

QVariant WorkbenchUiTest::run(const QString &op, const QVariantMap &args)
{
    auto result = app_->invoke(op,args);const auto map = result.toMap();
    // Checked query results may carry an intentionally empty error field.
    // Only an actual message is a command failure.
    if (!map.value("error").toString().isEmpty()) { QTest::qFail(qPrintable(map.value("error").toString()),__FILE__,__LINE__);return {}; }
    const auto job = map.value("job").toString();if (job.isEmpty()) return result;
    QElapsedTimer timer;timer.start();
    while (timer.elapsed()<10000) {
        const auto state = app_->job(job);if (state.value("state") == "complete") return state.value("result");
        if (state.value("state") == "failed") { QTest::qFail(qPrintable(state.value("error").toString()),__FILE__,__LINE__);return {}; }
        QTest::qWait(1);
    }
    QTest::qFail("Application job timed out",__FILE__,__LINE__);return {};
}

QVariantMap WorkbenchUiTest::mapProperty(QObject *object, const char *name)
{
    const auto value = object->property(name);return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
}

QQuickItem *WorkbenchUiTest::findItem(const QString &name, QQuickItem *from)
{
    if (!from) from = window_->contentItem();if (from->objectName() == name) return from;
    for (auto child : from->childItems()) if (auto item = findItem(name,child)) return item;
    return nullptr;
}

QPoint WorkbenchUiTest::portPoint(const QString &id, const QString &port)
{
    const auto viewport = findItem("graph.viewport");
    for (const auto &entry : app_->graphNodes()) { const auto node = entry.toMap();if (node.value("id") != id) continue;
        for (const auto &item : node.value("geometry").toMap().value("ports").toList()) { const auto p = item.toMap();if (p.value("id") != port) continue;
            const auto pos = node.value("position").toList();const auto zoom = graph_->property("zoom").toDouble();
            return viewport->mapToScene({graph_->property("panX").toDouble()+(pos[0].toDouble()+p.value("x").toDouble())*zoom,graph_->property("panY").toDouble()+(pos[1].toDouble()+p.value("y").toDouble())*zoom}).toPoint();
        }
    }
    QTest::qFail("Port not found",__FILE__,__LINE__);return {};
}

void WorkbenchUiTest::click(const QPoint &point, Qt::MouseButton button)
{
    QTest::mouseMove(window_,point);QTest::mouseClick(window_,button,Qt::NoModifier,point);QTest::qWait(25);
}

void WorkbenchUiTest::bubbleReturnsToSocketAndMother()
{
    click(portPoint("source","value"));QVERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    QVERIFY(mapProperty(graph_,"options").value("target/x").toMap().value("compatible").toBool());
    click(portPoint("target","x"));QTRY_COMPARE(app_->project().value("wires").toList().size(),1);
    QTRY_VERIFY(mapProperty(graph_,"linkFrom").isEmpty());
    click(portPoint("target","x"));QTRY_VERIFY(app_->project().value("wires").toList().isEmpty());
    QTRY_VERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    QVERIFY(mapProperty(graph_,"options").value("target/x").toMap().value("compatible").toBool());
    click(portPoint("target","x"));QTRY_COMPARE(app_->project().value("wires").toList().size(),1);
    click(portPoint("target","x"));QTRY_VERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    click(portPoint("source","value"));QTRY_VERIFY(mapProperty(graph_,"linkFrom").isEmpty());
    QVERIFY(app_->project().value("wires").toList().isEmpty());
    // A different data contract cannot be accepted; the held bubble survives.
    click(portPoint("text","value"));QVERIFY(!mapProperty(graph_,"options").value("target/x").toMap().value("compatible").toBool());
    click(portPoint("target","x"));QVERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    click(findItem("graph.viewport")->mapToScene({100.,100.}).toPoint(),Qt::RightButton);
    QVERIFY(mapProperty(graph_,"linkFrom").isEmpty());
}

void WorkbenchUiTest::rightClickCancelsHeldObjects()
{
    click(portPoint("source","value"));
    const auto point = findItem("graph.viewport")->mapToScene({130.,130.}).toPoint();click(point,Qt::RightButton);
    QVERIFY(mapProperty(graph_,"linkFrom").isEmpty());
    auto quick = graph_->findChild<QObject *>("graph.quickmenu");QVERIFY(quick);QVERIFY(!quick->property("visible").toBool());
    const auto before = app_->project().value("nodes").toList().size();
    graph_->setProperty("placement",QVariantMap{{"plugin","function.cos"}});
    click(point,Qt::RightButton);QVERIFY(mapProperty(graph_,"placement").isEmpty());
    QVERIFY(!quick->property("visible").toBool());QCOMPARE(app_->project().value("nodes").toList().size(),before);
    run("workspace.open",{{"page","layout"}});QTest::qWait(30);auto page = qobject_cast<corig::Workspaces *>(app_->workspaces())->page("layout");QVERIFY(page);
    page->setProperty("placement",QVariantMap{{"plugin","control.button"}});
    click(findItem("layout.viewport")->mapToScene({150.,150.}).toPoint(),Qt::RightButton);
    QVERIFY(mapProperty(page,"placement").isEmpty());
    QCOMPARE(app_->project().value("nodes").toList().size(),before);
    click(findItem("layout.viewport")->mapToScene({150.,150.}).toPoint(),Qt::RightButton);
    auto layoutQuick = page->findChild<QObject *>("layout.quickmenu");QVERIFY(layoutQuick);QVERIFY(layoutQuick->property("visible").toBool());
}

void WorkbenchUiTest::containerDragUsesEditorGestures()
{
    run("editor.add",{{"page","layout"},{"id","control_view"},{"plugin","control.text"}});
    run("editor.add",{{"page","layout"},{"id","container"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","control_view"},{"parent","root"},{"position",QVariantList{40.,40.}}});
    run("editor.place",{{"page","layout"},{"id","container"},{"parent","root"},{"position",QVariantList{500.,100.}}});
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto control = findItem("ui.control_view"), container = findItem("ui.container");QVERIFY(control);QVERIFY(container);
    const auto from = control->mapToScene({control->width()/2.,control->height()/2.}).toPoint(), to = container->mapToScene({container->width()/2.,container->height()/2.}).toPoint();
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,from);QTest::mouseMove(window_,from+QPoint(12,6),20);QTest::mouseMove(window_,to,30);QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,to);QTest::qWait(50);
    QVERIFY(app_->uiItem("container").value("children").toList().contains("control_view"));
    // A text field inside the dragged control never obtained focus/edited data.
    QCOMPARE(app_->project().value("nodes").toList().size(),3);
}

void WorkbenchUiTest::synchronizationRequiresChecklist()
{
    const auto before = app_->project().value("ui").toMap().value("items").toList().size();
    auto button = findItem("graph.sync");QVERIFY(button);click(button->mapToScene({button->width()/2.,button->height()/2.}).toPoint());
    auto dialog = graph_->findChild<QObject *>("sync.dialog");QVERIFY(dialog);QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(app_->project().value("ui").toMap().value("items").toList().size(),before);
    const auto plan = mapProperty(dialog,"plan");QVERIFY(!plan.value("changes").toList().isEmpty());
    const auto chosen = dialog->property("chosen").value<QJSValue>().toVariant().toList();QCOMPARE(chosen.size(),plan.value("changes").toList().size());
    QMetaObject::invokeMethod(dialog,"reject");
    QCOMPARE(app_->project().value("ui").toMap().value("items").toList().size(),before);
}

void WorkbenchUiTest::graphResizeAndDeleteWarning()
{
    run("graph.select",{{"id","target"}});QTest::qWait(20);
    const auto before = app_->graphNodes();auto handle = findItem("graph.resize.target");QVERIFY(handle);
    const auto point = handle->mapToScene({handle->width()/2.,handle->height()/2.}).toPoint();
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(window_,point+QPoint(50,30),25);QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,point+QPoint(50,30));QTest::qWait(30);
    QVERIFY(app_->nodeDesign("target").value("size").toList().size()==2);
    const auto count = app_->project().value("nodes").toList().size();findItem("graph.viewport")->forceActiveFocus();
    QTest::keyClick(window_,Qt::Key_Delete);QTest::qWait(20);
    auto dialog = graph_->findChild<QObject *>("editor.confirm");QVERIFY(dialog);QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(app_->project().value("nodes").toList().size(),count);
    QMetaObject::invokeMethod(dialog,"accept");QTRY_COMPARE(app_->project().value("nodes").toList().size(),count-1);
    run("history.undo");QVERIFY(!app_->nodeDesign("target").isEmpty());
}

void WorkbenchUiTest::externalDropsReachContainerSlots()
{
    run("editor.add",{{"page","layout"},{"id","container"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","container"},{"parent","root"},{"position",QVariantList{60.,60.}}});
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto container = findItem("ui.container");QVERIFY(container);
    const auto point = container->mapToScene({container->width()/3.,container->height()/2.}).toPoint();
    QMimeData plugin;plugin.setData("application/x-corig-plugin","control.button");
    QDragEnterEvent enter(point,Qt::CopyAction,&plugin,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&enter);QVERIFY(enter.isAccepted());
    QDragMoveEvent move(point,Qt::CopyAction,&plugin,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&move);
    QDropEvent drop(point,Qt::CopyAction,&plugin,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&drop);QTest::qWait(30);QVERIFY(drop.isAccepted());
    QCOMPARE(app_->uiItem("container").value("children").toList().size(),1);
    run("editor.add",{{"page","layout"},{"id","pending_control"},{"plugin","control.text"}});
    QMimeData pending;pending.setData("application/x-corig-object",QJsonDocument::fromVariant(QVariantMap{{"page","layout"},{"id","pending_control"}}).toJson(QJsonDocument::Compact));
    QDragEnterEvent enterPending(point,Qt::MoveAction,&pending,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&enterPending);QVERIFY(enterPending.isAccepted());
    QDropEvent dropPending(point,Qt::MoveAction,&pending,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&dropPending);QTest::qWait(30);QVERIFY(dropPending.isAccepted());
    QVERIFY(app_->uiItem("container").value("children").toList().contains("pending_control"));
}

void WorkbenchUiTest::outsideCanvasObjectsRemainDraggable()
{
    run("editor.add",{{"page","layout"},{"id","outside"},{"plugin","control.text"}});
    run("editor.place",{{"page","layout"},{"id","outside"},{"parent","root"},{"position",QVariantList{40.,780.}}});
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto item = findItem("ui.outside");QVERIFY(item);
    const auto from = item->mapToScene({80.,40.}).toPoint(), to = from-QPoint(0,180);
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,from);QTest::mouseMove(window_,from-QPoint(0,12),20);QTest::mouseMove(window_,to,30);QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,to);QTest::qWait(40);
    QVERIFY(app_->uiItem("outside").value("properties").toMap().value("y").toDouble()<780.);
}

void WorkbenchUiTest::nestedControlsAlignWithConfigurableGrid()
{
    run("ui.item.patch",{{"id","root"},{"properties",QVariantMap{{"grid_size",10}}}});
    run("editor.add",{{"page","layout"},{"id","row"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","row"},{"parent","root"},{"position",QVariantList{43.,57.}}});
    for (const auto &id : {"a","b","c"}) { run("editor.add",{{"page","layout"},{"id",id},{"plugin","control.text"}});run("editor.place",{{"page","layout"},{"id",id},{"parent","row"}}); }
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto canvas = findItem("ui.root");QVERIFY(canvas);
    for (const auto &id : {"row","a","b","c"}) { const auto item = findItem("ui."+QString(id));QVERIFY(item);const auto p = item->mapToItem(canvas,{0.,0.});
        for (const auto value : {p.x(),p.y(),item->width(),item->height()}) QVERIFY2(std::abs(value/10.-std::round(value/10.))<1e-6,"A nested control missed the paper grid");
    }
}

void WorkbenchUiTest::designAndRuntimeShareContainerLayout()
{
    run("editor.add",{{"page","layout"},{"id","container"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","container"},{"parent","root"},{"position",QVariantList{32.,64.}}});
    run("ui.item.patch",{{"id","container"},{"properties",QVariantMap{{"title","仅用于左侧层级的容器名"}}}});
    run("editor.add",{{"page","layout"},{"id","child"},{"plugin","control.text"}});
    run("editor.place",{{"page","layout"},{"id","child"},{"parent","container"}});
    auto workspaces = qobject_cast<corig::Workspaces *>(app_->workspaces());
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto design = workspaces->page("layout"), designRoot = findItem("ui.root",design), designChild = findItem("ui.child",design);
    QVERIFY(designRoot);QVERIFY(designChild);const auto position = designChild->mapToItem(designRoot,{0.,0.});const auto size = designChild->size();
    auto designContainer = findItem("ui.container",design);QCOMPARE(designContainer->property("headerHeight").toDouble(),0.);
    run("workspace.open",{{"page","runtime"}});QTest::qWait(40);
    auto runtime = workspaces->page("runtime"), runtimeRoot = findItem("ui.root",runtime), runtimeChild = findItem("ui.child",runtime);
    QVERIFY(runtimeRoot);QVERIFY(runtimeChild);QCOMPARE(runtimeChild->mapToItem(runtimeRoot,{0.,0.}),position);QCOMPARE(runtimeChild->size(),size);
    auto runtimeContainer = findItem("ui.container",runtime);QCOMPARE(runtimeContainer->property("headerHeight").toDouble(),0.);
}

void WorkbenchUiTest::publicEditorPresentationRoundTrip()
{
    const auto before = app_->project();
    for (const auto &page : {QString("graph"),QString("layout")}) {
        run("workspace.open",{{"page",page}});QTest::qWait(25);
        run("editor.configure",{{"page",page},{"zoom",.75},{"pan_x",123.},{"pan_y",-45.},{"left_width",180.},{"right_width",224.}});
        QTest::qWait(25);const auto state = run("editor.state",{{"page",page}}).toMap();
        QCOMPARE(state.value("zoom").toDouble(),.75);QCOMPARE(state.value("pan_x").toDouble(),123.);QCOMPARE(state.value("pan_y").toDouble(),-45.);
        QCOMPARE(state.value("left_width").toDouble(),180.);QCOMPARE(state.value("right_width").toDouble(),224.);
        QVERIFY_EXCEPTION_THROWN(app_->command("editor.configure",{{"page",page},{"zoom",0.},{"pan_x",999.}}),corig::Error);
        QCOMPARE(run("editor.state",{{"page",page}}).toMap(),state);
        run("editor.configure",{{"page",page},{"left_width",0.},{"right_width",0.}});QTest::qWait(25);
        QCOMPARE(run("editor.state",{{"page",page}}).toMap().value("left_width").toDouble(),0.);
    }
    QCOMPARE(app_->project(),before);
}

void WorkbenchUiTest::externalControlEditsReachRealNodes()
{
    run("graph.node.add",{{"id","slider"},{"plugin","control.slider"}});
    run("graph.node.add",{{"id","select"},{"plugin","control.select"},{"config",QVariantMap{{"choices",QStringList{"a","b"}},{"value","a"}}}});
    run("ui.update");
    int row = 0;
    for (const auto &entry : app_->project().value("ui").toMap().value("items").toList()) if (entry.toMap().contains("node")) {
        run("editor.place",{{"page","layout"},{"id",entry.toMap().value("id")},{"parent","root"},{"position",QVariantList{16.,16.+row++*144.}}});
        run("ui.item.patch",{{"id",entry.toMap().value("id")},{"properties",QVariantMap{{"height",128.}}}});
    }
    run("workspace.open",{{"page","runtime"}});run("runtime.start",{{"debug",false}});QTest::qWait(60);
    run("controls.set",{{"id","control.slider"},{"page","runtime"},{"property","value"},{"value",37.5}});
    QTRY_COMPARE(app_->nodeState("slider").value("state").toMap().value("value").toDouble(),37.5);
    run("controls.set",{{"id","control.select"},{"page","runtime"},{"property","currentIndex"},{"value",1}});
    QTRY_COMPARE(app_->nodeState("select").value("state").toMap().value("value").toString(),QString("b"));
    run("controls.set",{{"id","control.source"},{"page","runtime"},{"property","text"},{"value","7"}});
    run("controls.commit",{{"id","control.source"},{"page","runtime"}});
    QTRY_COMPARE(app_->nodeState("source").value("state").toMap().value("value").toDouble(),7.);
    run("runtime.stop");run("workspace.open",{{"page","layout"}});QTest::qWait(25);
    run("controls.set",{{"id","layout.grid"},{"page","layout"},{"property","value"},{"value",24}});
    QTRY_COMPARE(app_->uiItem("root").value("properties").toMap().value("grid_size").toInt(),24);
    const auto controls = run("controls.list",{{"page","layout"}}).toList();
    QVERIFY(std::any_of(controls.begin(),controls.end(),[](const QVariant &value) { const auto item = value.toMap();return item.value("name").toString().isEmpty() && item.value("path").toString().startsWith("auto:layout:"); }));
}

void WorkbenchUiTest::switchesPressedFeedbackAndCombinedEngineControl()
{
    run("graph.node.add",{{"id","flag"},{"plugin","control.checkbox"}});
    run("graph.node.add",{{"id","button"},{"plugin","control.button"},{"config",QVariantMap{{"label","触发"}}}});
    run("ui.view.add",{{"id","flag_view"},{"node","flag"},{"view","control"},{"properties",QVariantMap{{"x",16.},{"y",16.},{"width",320.},{"height",64.},{"title","冗余标题"}}}});
    run("ui.view.add",{{"id","button_view"},{"node","button"},{"view","control"},{"properties",QVariantMap{{"x",16.},{"y",96.},{"width",320.},{"height",64.},{"title","冗余标题"}}}});
    run("workspace.open",{{"page","runtime"}}); run("runtime.start",{{"debug",false}}); QTest::qWait(60);
    auto runtime = qobject_cast<corig::Workspaces *>(app_->workspaces())->page("runtime");
    auto flag = findItem("control.flag",runtime); QVERIFY(flag); QVERIFY(flag->inherits("QQuickSwitch"));
    const auto indicator = flag->property("indicator").value<QQuickItem *>(); QVERIFY(indicator); QVERIFY(indicator->width() > indicator->height());
    run("controls.set",{{"id","control.flag"},{"page","runtime"},{"property","checked"},{"value",true}});
    QTRY_VERIFY(app_->nodeState("flag").value("state").toMap().value("value").toBool());
    auto view = findItem("ui.flag_view",runtime); QVERIFY(view); QCOMPARE(view->property("headerHeight").toDouble(),0.);
    for (const auto child : view->findChildren<QObject *>()) QVERIFY(child->property("text").toString() != "冗余标题");
    auto button = findItem("control.button",runtime); QVERIFY(button);
    auto background = button->property("background").value<QQuickItem *>(); QVERIFY(background);
    const auto color = background->property("color");
    const auto position = button->mapToScene(button->boundingRect().center()).toPoint();
    QTest::mousePress(runtime->window(),Qt::LeftButton,Qt::NoModifier,position);
    QVERIFY(button->property("down").toBool()); QVERIFY(background->property("color") != color);
    QTest::mouseRelease(runtime->window(),Qt::LeftButton,Qt::NoModifier,position);
    QTRY_VERIFY(app_->nodeState("button").value("outputs").toMap().value("trigger").toBool());
    QVERIFY(!findItem("execution.pause"));
    run("controls.click",{{"id","execution.run"}}); QTRY_VERIFY(!app_->runtime().value("running").toBool());
    QCOMPARE(findItem("execution.run")->property("symbol").toString(),QString("run"));
    run("controls.click",{{"id","execution.run"}}); QTRY_VERIFY(app_->runtime().value("running").toBool());
}

void WorkbenchUiTest::localPluginDetailsStayVisibleWhenDisabled()
{
    run("project.new",{{"discard",true}});
    const auto before = app_->project();
    run("plugin.settings");
    run("controls.set",{{"id","plugins.search"},{"property","text"},{"value","control.slider"}});
    QTRY_VERIFY(findItem("plugins.tree.package.org.corig.control.slider"));
    run("controls.click",{{"id","plugins.tree.package.org.corig.control.slider"}});
    QTRY_VERIFY(findItem("plugins.node_preview.control.slider"));
    auto preview = run("library.preview",{{"plugin","control.slider"},{"scope","library"}}).toMap();
    QVERIFY(!preview.value("definition").toMap().value("description").toString().isEmpty());
    QVERIFY(!preview.value("geometry").toMap().value("ports").toList().isEmpty());
    QVERIFY(!preview.value("view_urls").toMap().isEmpty());
    QTRY_VERIFY(!mapProperty(findItem("plugins.node_preview.control.slider"),"geometry").contains("error"));
    QTRY_COMPARE(mapProperty(findItem("plugins.ui_preview.control.slider"),"spec").value("id").toString(),QString("control.slider"));
    run("controls.click",{{"id","plugins.enabled.package.org.corig.control.slider"}});
    QTRY_VERIFY(app_->definition("control.slider").isEmpty());
    QTRY_COMPARE(findItem("plugins.enabled.folder.控件")->property("checkState").toInt(),int(Qt::PartiallyChecked));
    QTRY_VERIFY(findItem("plugins.description.control.slider"));
    QTRY_VERIFY(findItem("plugins.node_preview.control.slider") && !mapProperty(findItem("plugins.node_preview.control.slider"),"geometry").contains("error"));
    preview = run("library.preview",{{"plugin","control.slider"},{"scope","library"}}).toMap();
    QCOMPARE(preview.value("definition").toMap().value("id").toString(),QString("control.slider"));
    QCOMPARE(app_->project(),before);
    QVERIFY(!app_->runtime().value("active").toBool());
    // The searched tree only shows one leaf, but its category checkbox still
    // applies to the complete subtree, including filtered/collapsed children.
    run("controls.click",{{"id","plugins.enabled.folder.控件"}});
    QTRY_COMPARE(findItem("plugins.enabled.folder.控件")->property("checkState").toInt(),int(Qt::Checked));
    run("controls.click",{{"id","plugins.enabled.folder.控件"}});
    QTRY_VERIFY(app_->definition("control.number").isEmpty());
    QTRY_VERIFY(app_->definition("control.text").isEmpty());
    QTRY_VERIFY(app_->definition("control.slider").isEmpty());
    QTRY_COMPARE(findItem("plugins.enabled.folder.控件")->property("checkState").toInt(),int(Qt::Unchecked));
    run("controls.click",{{"id","plugins.enabled.folder.控件"}});
    QTRY_VERIFY(!app_->definition("control.number").isEmpty());
    QTRY_VERIFY(!app_->definition("control.text").isEmpty());
    QCOMPARE(app_->project(),before);
    run("controls.click",{{"id","plugins.close"}});
}

void WorkbenchUiTest::propertyDraftsAndPortsAreAtomic()
{
    run("graph.node.add",{{"id","split"},{"plugin","data.fork"},{"config",QVariantMap{{"data_type","number"},{"outputs",QVariantList{"a","b","c"}}}}});
    run("graph.wire.add",{{"id","keep"},{"source",QVariantList{"source","value"}},{"target",QVariantList{"split","in"}}});
    run("graph.wire.add",{{"id","affected"},{"source",QVariantList{"split","c"}},{"target",QVariantList{"target","x"}}});
    run("graph.select",{{"id","split"}});
    run("property.stage",{{"scope","node"},{"target","split"},{"changes",QVariantMap{{"outputs",QVariantList{"a","b"}}}}});
    const auto before = app_->project();
    const auto guard = app_->invoke("graph.select",{{"id","target"}}).toMap();
    QVERIFY(guard.value("requires_draft_resolution").toBool()); QCOMPARE(app_->selected(),QString("split"));
    QTRY_VERIFY(findItem("property.leave.cancel") && findItem("property.leave.cancel")->isVisible() && findItem("property.leave.cancel")->isEnabled());
    run("controls.click",{{"id","property.leave.cancel"}});
    QCOMPARE(app_->selected(),QString("split")); QCOMPARE(app_->project(),before);

    const auto plan = app_->invoke("property.commit",{{"scope","node"},{"target","split"}}).toMap();
    QVERIFY2(!plan.contains("error"),qPrintable(plan.value("error").toString()));
    QCOMPARE(plan.value("affected_wires").toList().size(),1); QVERIFY(plan.value("requires_confirmation").toBool());
    QCOMPARE(app_->project(),before);
    QTRY_VERIFY(findItem("property.ports.cancel") && findItem("property.ports.cancel")->isVisible() && findItem("property.ports.cancel")->isEnabled());
    run("controls.click",{{"id","property.ports.cancel"}});
    QVERIFY(run("property.get",{{"scope","node"},{"target","split"}}).toMap().value("dirty").toBool());
    run("property.commit",{{"scope","node"},{"target","split"},{"confirmed",true},{"revision",plan.value("revision")}});
    QCOMPARE(app_->project().value("wires").toList().size(),1);
    // The confirmed commit changes the project itself. Testing inside this
    // function would record each assertion against the originally entered one,
    // so the final verification uses the same public query directly.
    QVERIFY(!app_->invoke("property.get",{{"scope","node"},{"target","split"}}).toMap().value("dirty").toBool());
    run("history.undo");
    QCOMPARE(app_->project().value("wires"),before.value("wires"));
    QCOMPARE(app_->nodeDesign("split").value("config").toMap().value("outputs").toList(),(QVariantList{"a","b","c"}));

    // A changed candidate or design cannot reuse a prior destructive preview.
    const auto stale = run("node.configure.plan",{{"node","split"},{"changes",QVariantMap{{"outputs",QVariantList{"a","b"}}}}}).toMap();
    run("graph.node.move",{{"id","target"},{"position",QVariantList{600.,80.}}});
    const auto refreshed = app_->command("node.configure",{{"node","split"},{"changes",QVariantMap{{"outputs",QVariantList{"a","b"}}}},{"confirmed",true},{"revision",stale.value("revision")}}).toMap();
    QVERIFY(refreshed.value("requires_confirmation").toBool()); QVERIFY(refreshed.value("revision") != stale.value("revision"));
    QCOMPARE(app_->project().value("wires").toList().size(),2);

    run("graph.select",{{"id","source"}});
    QTRY_VERIFY(findItem("property.base"));
    QVERIFY(!findItem("property.node_title")->isVisible());
    run("property.text",{{"scope","node"},{"target","source"},{"field","value"},{"text","-"}});
    QVERIFY_THROWS_EXCEPTION(corig::Error,app_->command("property.commit",{{"scope","node"},{"target","source"}}));
    // A rejected commit is an expected validation result, not a transport
    // failure: the shared session must retain both text and its reason.
    const auto rejected = app_->invoke("property.get",{{"scope","node"},{"target","source"}}).toMap();
    QVERIFY(rejected.value("dirty").toBool());
    QVERIFY(!rejected.value("error").toString().isEmpty());
    QCOMPARE(rejected.value("text_draft").toMap().value("value").toString(),QString("-"));
    app_->invoke("graph.select",{{"id","target"}});
    QTRY_VERIFY(findItem("property.leave.cancel") && findItem("property.leave.cancel")->isVisible() && findItem("property.leave.cancel")->isEnabled());
    run("controls.click",{{"id","property.leave.cancel"}});
    QCOMPARE(app_->selected(),QString("source"));
    run("property.text",{{"scope","node"},{"target","source"},{"field","value"},{"text","3.5"}});
    app_->invoke("graph.select",{{"id","target"}});
    QTRY_VERIFY(findItem("property.leave.cancel") && findItem("property.leave.cancel")->isVisible() && findItem("property.leave.cancel")->isEnabled());
    run("controls.click",{{"id","property.leave.cancel"}});
    QCOMPARE(app_->selected(),QString("source"));
    // The dialog only decides whether to leave. Applying the accepted text
    // goes through the same public commit entry used by GUI and automation.
    run("property.commit",{{"scope","node"},{"target","source"}});
    QCOMPARE(app_->nodeDesign("source").value("config").toMap().value("value").toDouble(),3.5);
    QVERIFY(!run("property.get",{{"scope","node"},{"target","source"}}).toMap().value("dirty").toBool());
    app_->invoke("graph.select",{{"id","target"}});
    QCOMPARE(app_->selected(),QString("target"));
}

void WorkbenchUiTest::qmlContainersAndWorkspacesTravelWithProject()
{
    // Package the shipped public example exactly as a third-party author does.
    const QString directory = QStringLiteral(CORIG_SOURCE_DIR "/sdk/examples/qml_workspace/");
    QFile manifestFile(directory+"plugin.json"); QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).toVariant().toMap(); QMap<QString,QByteArray> files;
    for (const auto &entry : manifest.take("files").toList()) {
        QFile file(directory+entry.toString()); QVERIFY(file.open(QIODevice::ReadOnly)); files[entry.toString()] = file.readAll();
    }
    const auto package = corig::PluginPackage::create(manifest,files); const auto path = folder_->filePath("ui-tools.crpl"); package.save(path);
    run("plugin.import",{{"path",path}});
    QVERIFY(app_->pythonPath().isEmpty());
    const auto count = app_->project().value("nodes").toList().size();
    run("ui.container.add",{{"id","custom"},{"plugin","user.example.ribbon"},{"properties",QVariantMap{{"x",32.},{"y",32.},{"width",640.},{"height",320.}}}});
    run("ui.view.add",{{"id","first"},{"node","source"},{"view","control"},{"parent","custom"}});
    run("ui.view.add",{{"id","second"},{"node","text"},{"view","control"},{"parent","custom"}});
    QCOMPARE(app_->project().value("nodes").toList().size(),count);
    run("workspace.open",{{"page","layout"}});
    QTRY_VERIFY(findItem("ui.first")&&findItem("ui.second"));
    QTRY_VERIFY(findItem("ui.first")->parentItem()->x()<findItem("ui.second")->parentItem()->x());
    run("property.commit",{{"scope","ui"},{"target","custom"},{"changes",QVariantMap{{"reverse",true}}}});
    QTRY_VERIFY(findItem("ui.first")->parentItem()->x()>findItem("ui.second")->parentItem()->x());
    const auto geometry = run("container.layout",{{"id","custom"},{"width",608.},{"height",288.},{"grid",16}}).toMap().value("rectangles").toList();
    QCOMPARE(geometry.size(),2); QCOMPARE(geometry[0].toMap().value("x").toDouble(),findItem("ui.first")->parentItem()->x());
    run("history.undo"); QTRY_VERIFY(findItem("ui.first")->parentItem()->x()<findItem("ui.second")->parentItem()->x());

    run("workspace.open",{{"page","user.example.overview"}});
    const auto workspace = qobject_cast<corig::Workspaces *>(app_->workspaces()); const auto original = workspace->page("user.example.overview");
    QVERIFY(original); QTRY_VERIFY(findItem("overview.heading"));
    run("workspace.configure",{{"page","user.example.overview"},{"changes",QVariantMap{{"heading","保存的总览"}}}});
    QTRY_COMPARE(findItem("overview.heading")->property("text").toString(),QString("保存的总览"));
    const auto main = run("app.state").toMap().value("workspaces").toMap().value("windows").toList().first().toMap().value("id").toString();
    run("workspace.detach",{{"page","user.example.overview"},{"x",200},{"y",120}});
    QCOMPARE(workspace->page("user.example.overview"),original);
    run("workspace.move",{{"page","user.example.overview"},{"window",main}});
    const auto saved = folder_->filePath("qml-project.crpj"); run("project.save",{{"path",saved},{"include_plugins",true}});
    const auto stored = corig::loadProject(saved);
    QVERIFY(stored.value("workspaces").toMap().contains("user.example.overview"));
    bool included = false; for (const auto &entry : stored.value("plugin_packages").toList()) included |= entry.toMap().value("id") == package.id();
    QVERIFY(included);
    QVERIFY_THROWS_EXCEPTION(corig::Error,app_->command("plugin.enable",{{"plugin","user.example.ribbon"},{"enabled",false}}));
    run("project.new",{{"discard",true}});
    QVERIFY(!workspace->page("user.example.overview"));
    run("plugin.remove",{{"package",package.id()}});
    run("project.open",{{"path",saved}});
    run("workspace.open",{{"page","user.example.overview"}});
    QTRY_VERIFY(findItem("overview.heading"));
    QCOMPARE(findItem("overview.heading")->property("text").toString(),QString("保存的总览"));
    QVERIFY(app_->pythonPath().isEmpty()); QVERIFY(run("project.validate").toMap().value("valid").toBool());
}

void WorkbenchUiTest::nodeViewBridgeContract()
{
    // An unplaced node view still owns a real, checked appearance contract:
    // committing it is a project edit with undo, and runtime decisions come
    // from the same observation the engine enforces.
    auto *bridge = app_->userViewBridge("source","control","");
    QVERIFY(bridge);
    QCOMPARE(bridge->property("runtimeActive").toBool(),false);
    QCOMPARE(bridge->property("running").toBool(),false);
    QCOMPARE(bridge->property("pending").toBool(),false);
    QVERIFY(bridge->property("editable").toBool());
    QCOMPARE(bridge->property("error").toString(),QString());
    const auto info = bridge->property("info").toMap();
    QCOMPARE(info.value("scope").toString(),QString("view"));
    QCOMPARE(info.value("node").toString(),QString("source"));
    QCOMPARE(info.value("view").toString(),QString("control"));
    QCOMPARE(info.value("read_only").toBool(),false);
    const auto defaults = bridge->property("properties").toMap();
    QCOMPARE(defaults.value("color").toString(),QString("#E6EDF5"));
    QVERIFY(!defaults.contains("x"));
    // The appearance is a public command as well, so automation discovers the
    // same schema and reads the same checked values as QML.
    const auto read = run("ui.view.appearance",{{"node","source"},{"view","control"}}).toMap();
    QCOMPARE(read.value("target").toString(),QString("source/control"));
    QCOMPARE(read.value("updated").toBool(),false);
    QCOMPARE(read.value("properties").toMap(),defaults);
    QVERIFY(!read.value("schema").toMap().isEmpty());
    QVariantMap styled{{"color","#FF0000"}};
    QVariant committed;
    QVERIFY(QMetaObject::invokeMethod(bridge,"commit",Q_RETURN_ARG(QVariant,committed),Q_ARG(QVariantMap,styled)));
    QCOMPARE(committed.toMap().value("updated").toBool(),true);
    QCOMPARE(bridge->property("properties").toMap().value("color").toString(),QString("#FF0000"));
    const auto stored = app_->project().value("ui").toMap().value("view_properties").toMap();
    QCOMPARE(stored.value("source/control").toMap().value("color").toString(),QString("#FF0000"));
    QVERIFY(!stored.value("source/control").toMap().contains("x"));
    // A bridge commit and a public patch share one implementation, so their
    // results are identical.
    QCOMPARE(run("ui.view.appearance",{{"node","source"},{"view","control"}}).toMap().value("properties").toMap(),committed.toMap().value("properties").toMap());
    QVERIFY(app_->dirty());
    run("history.undo");
    QCOMPARE(bridge->property("properties").toMap().value("color").toString(),QString("#E6EDF5"));
    // Invalid values and canvas placement are rejected through the checked
    // appearance schema, and the failure stays readable on the bridge.
    QVariantMap badType{{"color",QVariant(5)}};
    QVariant rejected;
    QVERIFY(QMetaObject::invokeMethod(bridge,"commit",Q_RETURN_ARG(QVariant,rejected),Q_ARG(QVariantMap,badType)));
    QVERIFY(!rejected.toMap().value("error").toString().isEmpty());
    QVERIFY(!bridge->property("error").toString().isEmpty());
    QVariantMap placed{{"x",48.}};
    QVERIFY(QMetaObject::invokeMethod(bridge,"commit",Q_RETURN_ARG(QVariant,rejected),Q_ARG(QVariantMap,placed)));
    QCOMPARE(rejected.toMap().value("error").toString(),QString("视图外观不支持布局字段：x"));
    QVERIFY(!app_->project().value("ui").toMap().value("view_properties").toMap().contains("source/control"));
    // A direct patch through the command is the same edit and stays on the same
    // undo stack, so QML and automation never diverge.
    const auto patched = run("ui.view.appearance",{{"node","source"},{"view","control"},{"changes",QVariantMap{{"background","#202020"}}}}).toMap();
    QCOMPARE(patched.value("updated").toBool(),true);
    QCOMPARE(bridge->property("properties").toMap().value("background").toString(),QString("#202020"));
    run("history.undo");
    QVERIFY(!app_->project().value("ui").toMap().value("view_properties").toMap().contains("source/control"));
    QVERIFY(app_->invoke("ui.view.appearance",{{"node","source"},{"view","control"},{"changes",QVariantMap{{"x",16.}}}}).toMap().value("error").toString() == "视图外观不支持布局字段：x");
    // An action runs on the engine like any other job: the bridge becomes
    // pending, then exposes the failure the engine reported.
    QVariantMap actionArgs{{"value",1.}};
    QVariant actionResult;
    QVERIFY(QMetaObject::invokeMethod(bridge,"action",Q_RETURN_ARG(QVariant,actionResult),Q_ARG(QString,QString("set_value")),Q_ARG(QVariantMap,actionArgs)));
    QVERIFY(!actionResult.toMap().value("job").toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!bridge->property("pending").toBool(),10000);
    // The declared action requires a running session, so the engine refuses it
    // and the view keeps its button disabled until runtimeActive turns true.
    QCOMPARE(bridge->property("error").toString(),QString("请先建立运行会话"));
    // A running configuration reports a job; the bridge stays pending and
    // refuses a duplicate submit until the engine settles that job. value is
    // not runtime_editable, so the engine rejects it and the view shows why.
    run("graph.node.add",{{"id","tune"},{"plugin","plot.curves"},{"position",QVariantList{20.,420.}}});
    auto *tunable = app_->userViewBridge("tune","plot","");
    QVERIFY(tunable);
    run("workspace.open",{{"page","runtime"}});
    run("runtime.start",{{"debug",false}});
    QTRY_VERIFY(bridge->property("runtimeActive").toBool());
    QVERIFY(bridge->property("running").toBool());
    // The same action succeeds with a session, and clears the previous error.
    QVERIFY(QMetaObject::invokeMethod(bridge,"action",Q_RETURN_ARG(QVariant,actionResult),Q_ARG(QString,QString("set_value")),Q_ARG(QVariantMap,actionArgs)));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge->property("pending").toBool(),10000);
    QCOMPARE(bridge->property("error").toString(),QString());
    QVariant configured;
    QVariantMap configChanges{{"value",2.}};
    QVERIFY(QMetaObject::invokeMethod(bridge,"configure",Q_RETURN_ARG(QVariant,configured),Q_ARG(QVariantMap,configChanges)));
    QVERIFY(!configured.toMap().value("job").toString().isEmpty());
    QVERIFY(bridge->property("pending").toBool());
    QVariant blocked;
    QVERIFY(QMetaObject::invokeMethod(bridge,"configure",Q_RETURN_ARG(QVariant,blocked),Q_ARG(QVariantMap,configChanges)));
    QCOMPARE(blocked.toMap().value("error").toString(),QString("请等待当前操作完成"));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge->property("pending").toBool(),10000);
    QCOMPARE(bridge->property("error").toString(),QString("该参数需要暂停后修改：value"));
    // A runtime_editable field is accepted while running, and the accepted job
    // carries the new value back into the design.
    QVariantMap tuneChanges{{"window_seconds",20.}};
    QVariant tuned;
    QVERIFY(QMetaObject::invokeMethod(tunable,"configure",Q_RETURN_ARG(QVariant,tuned),Q_ARG(QVariantMap,tuneChanges)));
    QVERIFY(!tuned.toMap().value("job").toString().isEmpty());
    QVERIFY(tunable->property("pending").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!tunable->property("pending").toBool(),10000);
    QCOMPARE(tunable->property("error").toString(),QString());
    QCOMPARE(app_->nodeDesign("tune").value("config").toMap().value("window_seconds").toDouble(),20.);
    run("runtime.stop");
    QTRY_VERIFY(!bridge->property("runtimeActive").toBool());
    // Stopped configuration applies to the design synchronously, so the change
    // the engine refused at runtime now succeeds.
    QVERIFY(QMetaObject::invokeMethod(bridge,"configure",Q_RETURN_ARG(QVariant,configured),Q_ARG(QVariantMap,configChanges)));
    // The stopped path may still hand the new design to the engine as a job;
    // the bridge stays pending until that job adopts the project.
    QTRY_VERIFY_WITH_TIMEOUT(!bridge->property("pending").toBool(),10000);
    QCOMPARE(bridge->property("error").toString(),QString());
    QCOMPARE(app_->nodeDesign("source").value("config").toMap().value("value").toDouble(),2.);
    // Loading and validating use the same appearance rules as editing, so a
    // tampered document cannot introduce layout geometry or unknown views.
    corig::Catalog catalog;
    auto tampered = app_->project();
    auto tamperedUi = tampered.value("ui").toMap();
    tamperedUi["view_properties"] = QVariantMap{{"source/control",QVariantMap{{"x",16.}}}};
    tampered["ui"] = tamperedUi;
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::validateProject(tampered,catalog));
    tamperedUi["view_properties"] = QVariantMap{{"source/control",QVariantMap{{"color",5}}}};
    tampered["ui"] = tamperedUi;
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::validateProject(tampered,catalog));
    tamperedUi["view_properties"] = QVariantMap{{"ghost/control",QVariantMap{{"color","#FFFFFF"}}}};
    tampered["ui"] = tamperedUi;
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::validateProject(tampered,catalog));
    tamperedUi["view_properties"] = QVariantMap{{"source/control",QVariantMap{{"color","#FFFFFF"}}}};
    tampered["ui"] = tamperedUi;
    corig::validateProject(tampered,catalog);
}

void WorkbenchUiTest::sixDofViewActionsMatchCall()
{
    // view.actions and view.call share one table: every advertised view-local
    // action must reach the loaded view instead of falling through to the
    // engine's node.action, which does not declare these actions.
    run("graph.node.add",{{"id","pose"},{"plugin","plot.sixdof"}});
    QQmlComponent component(qml_.get(),QUrl(QStringLiteral("qrc:/qt/qml/CoRig/BuiltinView.qml")));
    QVERIFY2(component.isReady(),qPrintable(component.errorString()));
    std::unique_ptr<QObject> view(component.createWithInitialProperties({{"nodeId","pose"},{"viewKey","sixdof"},{"viewProperties",QVariantMap{}}}));
    QVERIFY(view);
    auto *item = qobject_cast<QQuickItem *>(view.get());QVERIFY(item);
    item->setSize(QSizeF(320,240));item->setParentItem(window_->contentItem());
    QTRY_VERIFY_WITH_TIMEOUT(item->findChild<corig::NativeSixDof *>() != nullptr,5000);
    auto *scene = item->findChild<corig::NativeSixDof *>();
    QTRY_VERIFY_WITH_TIMEOUT(!app_->viewState("pose","sixdof").isEmpty(),5000);

    const auto listed = run("view.actions",{{"node","pose"},{"view","sixdof"}}).toList();
    QMap<QString,QVariantMap> contracts;
    for (const auto &entry : listed) contracts.insert(entry.toMap().value("action").toString(),entry.toMap());
    for (const auto &name : {"camera","reset_camera","restore_defaults","export_image","geometry"})
        QVERIFY2(contracts.contains(name),qPrintable(name));
    QCOMPARE(contracts.value("camera").value("requires_view").toBool(),true);
    QVERIFY(contracts.value("camera").value("args").toMap().contains("azimuth"));
    QVERIFY(contracts.value("export_image").value("args").toMap().contains("path"));

    const auto camera = run("view.call",{{"node","pose"},{"view","sixdof"},{"action","camera"},
        {"args",QVariantMap{{"azimuth",72.},{"elevation",12.},{"distance",1.1},{"pan",QVariantList{0.,0.}}}}}).toMap();
    QCOMPARE(camera.value("azimuth").toDouble(),72.);
    QCOMPARE(scene->camera().value("azimuth").toDouble(),72.);
    const auto reset = run("view.call",{{"node","pose"},{"view","sixdof"},{"action","reset_camera"}}).toMap();
    QCOMPARE(reset.value("azimuth").toDouble(),38.);
    run("view.call",{{"node","pose"},{"view","sixdof"},{"action","camera"},{"args",QVariantMap{{"azimuth",120.}}}});
    QCOMPARE(scene->camera().value("azimuth").toDouble(),120.);
    const auto restored = run("view.call",{{"node","pose"},{"view","sixdof"},{"action","restore_defaults"}}).toMap();
    QCOMPARE(restored.value("azimuth").toDouble(),38.);
    // A style edit through the host binding reaches setSettings but keeps the
    // view the user is looking at.
    const auto framed = scene->camera();
    item->setProperty("viewProperties",QVariantMap{{"background","#000000"}});
    QVERIFY(scene->camera() == framed);
    const auto imagePath = folder_->path() + QStringLiteral("/sixdof.png");
    run("view.call",{{"node","pose"},{"view","sixdof"},{"action","export_image"},
        {"args",QVariantMap{{"path",imagePath},{"width",320},{"height",240}}}});
    QVERIFY(QFile::exists(imagePath));
}

void WorkbenchUiTest::sixDofAppearanceKeepsCamera()
{
    // Appearance edits own rendering fields only; the camera is re-seated only
    // by a changed camera default or by the explicit restore action.
    corig::NativeSixDof scene;
    QVariantMap settings{{"camera_azimuth",38.},{"camera_elevation",24.},{"camera_distance_m",.65},
        {"camera_target",QVariantList{0.,0.,0.}},{"projection","orthographic"},{"background","#14212D"},
        {"model_transparency",.65},{"show_grid",true},{"show_model_axes",true},{"show_world_axes",true}};
    scene.setSettings(settings);
    QCOMPARE(scene.camera().value("azimuth").toDouble(),38.);
    scene.orbit(40,15);scene.dolly(1.3);
    const auto orbit = scene.camera();
    QVERIFY(orbit.value("azimuth").toDouble() != 38.);
    auto appearance = settings;
    appearance["background"]="#000000";appearance["show_grid"]=false;
    appearance["model_transparency"]=.4;appearance["projection"]="perspective";
    scene.setSettings(appearance);
    QVERIFY(scene.camera() == orbit);
    QCOMPARE(scene.settings().value("show_grid").toBool(),false);
    auto cameraDefault = appearance;
    cameraDefault["camera_azimuth"]=10.;cameraDefault["camera_elevation"]=-5.;cameraDefault["camera_distance_m"]=2.;
    scene.setSettings(cameraDefault);
    QCOMPARE(scene.camera().value("azimuth").toDouble(),10.);
    QCOMPARE(scene.camera().value("elevation").toDouble(),-5.);
    QCOMPARE(scene.camera().value("distance").toDouble(),2.);
}

void WorkbenchUiTest::placedViewBridgeUsesPropertySession()
{
    // A view placed on the layout resolves to its UI object's property
    // session, so the documented members keep the per-instance appearance.
    run("ui.view.add",{{"id","source_view"},{"node","source"},{"view","control"}});
    auto *panel = app_->userViewBridge("source","control","source_view");
    QVERIFY(panel);
    QVERIFY(panel == app_->propertyBridge("ui","source_view"));
    QCOMPARE(panel->property("scope").toString(),QString("ui"));
    QVERIFY(!panel->property("properties").toMap().isEmpty());
    QCOMPARE(panel->property("runtimeActive").toBool(),false);
    QCOMPARE(panel->property("pending").toBool(),false);
    QVERIFY(panel->property("editable").toBool());
    QVariantMap styled{{"color","#123456"}};
    QVariant committed;
    QVERIFY(QMetaObject::invokeMethod(panel,"commit",Q_RETURN_ARG(QVariant,committed),Q_ARG(QVariantMap,styled)));
    QCOMPARE(committed.toMap().value("updated").toBool(),true);
    QCOMPARE(app_->uiItem("source_view").value("properties").toMap().value("color").toString(),QString("#123456"));
    QCOMPARE(panel->property("properties").toMap().value("color").toString(),QString("#123456"));
}

void WorkbenchUiTest::viewBridgePreviewStaysReadOnly()
{
    // Design preview loads real view QML with the documented members, but the
    // preview bridge has no authority to act or commit.
    QQmlComponent component(qml_.get(),QUrl(QStringLiteral("qrc:/qt/qml/CoRig/BuiltinView.qml")));
    QVERIFY2(component.isReady(),qPrintable(component.errorString()));
    const auto url = QUrl::fromLocalFile(QStringLiteral(CORIG_SOURCE_DIR "/tests/native/probe/view_bridge_probe.qml"));
    // A third-party node view declares its own QML; the official views use the
    // Host interface instead, so the preview must be driven by a user plugin
    // declaration with the documented bridge contract.
    const QVariantMap design{{"plugin","user.bridge.preview"},{"title","桥预览"},{"config",QVariantMap{{"gain",2.}}}};
    const QVariantMap definition{{"id","user.bridge.preview"},{"name","桥预览"},{"kind","node"},{"views",QVariantMap{}},{"properties",QVariantMap{}}};
    std::unique_ptr<QObject> view(component.createWithInitialProperties({{"nodeId",""},{"viewKey","control"},{"designMode",true},
        {"designOverride",design},{"definitionOverride",definition},{"viewUrlOverride",url.toString()},
        {"viewProperties",QVariantMap{{"label","预览外观"}}}}));
    QVERIFY(view);
    auto *item = qobject_cast<QQuickItem *>(view.get());
    QVERIFY(item);
    item->setParentItem(window_->contentItem());
    QTRY_VERIFY(findItem("view.bridge.probe",item));
    auto *probe = findItem("view.bridge.probe",item);
    auto *bridge = probe->property("bridgeRef").value<QObject *>();
    QVERIFY(bridge);
    QCOMPARE(bridge->property("editable").toBool(),false);
    QCOMPARE(bridge->property("runtimeActive").toBool(),false);
    QCOMPARE(bridge->property("running").toBool(),false);
    QCOMPARE(bridge->property("pending").toBool(),false);
    QCOMPARE(bridge->property("info").toMap().value("scope").toString(),QString("preview"));
    QCOMPARE(bridge->property("schema").toMap().isEmpty(),true);
    QCOMPARE(bridge->property("properties").toMap().value("label").toString(),QString("预览外观"));
    QVERIFY(!probe->property("actionError").toString().isEmpty());
    QVERIFY(!probe->property("commitError").toString().isEmpty());
    QVERIFY(!probe->property("configureError").toString().isEmpty());
}

QTEST_MAIN(WorkbenchUiTest)
#include "workbench_ui_test.moc"
