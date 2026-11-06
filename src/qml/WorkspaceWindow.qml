import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    required property string hostId
    property bool childWindow:false
    property bool pinned:false
    property var tabs: []
    property string activePage: ""
    property string pendingAction: ""
    property string savingJob: ""
    property string pendingProjectOp: ""
    property var pendingProjectArgs: ({})
    property string runtimeStopJob: ""
    property string projectJob: ""
    // Presentation state only: a published application hides editor chrome here,
    // while CommandRegistry stays the single command-level authority.
    property bool publishMode: App.appMode === "publish"
    readonly property var publishPages: ["runtime", "logs"]
    property string localNotice: ""
    property string localNoticeLevel: "info"
    objectName: "window." + hostId
    width:childWindow?1000:1420;height:childWindow?700:900;minimumWidth:childWindow?540:800;minimumHeight:childWindow?320:540
    visible: false
    color: Theme.panel
    title: (App.dirty ? "● " : "") + (App.project.name || "未命名工程") + " — 联调 · CoRig"
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13
    palette.window: Theme.panel
    palette.windowText: Theme.text
    palette.base: Theme.dark ? "#111923" : "#FFFFFF"
    palette.alternateBase: Theme.raised
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.highlight: Theme.blue
    palette.highlightedText: Theme.dark ? "#111923" : "#FFFFFF"
    palette.mid: Theme.border
    palette.dark: Theme.border
    palette.light: Theme.raised

    function requestAction(action) {
        propertyDialogs.guard(()=>requestResolvedAction(action))
    }
    function requestResolvedAction(action) {
        pendingAction = action
        if (App.dirty) unsaved.open()
        else continueAction()
    }
    function continueAction() {
        let action = pendingAction; pendingAction = ""
        if (action === "open") openProject.open()
        else if (action === "new") win.newProject()
        else if (action === "close") App.workspaces.closeWindow(hostId, true)
    }
    // MenuBar always creates one button per bare Menu and ignores Menu.visible, so a
    // published window hides the editor menus on the created bar items instead. The
    // publish menu is hidden the same way while editing.
    function applyMenuBarMode() {
        const bar = win.menuBar
        const row = bar ? bar.contentItem : null
        if (!row) return
        for (let i = 0; i < row.children.length; ++i) {
            const item = row.children[i]
            if (!item.menu) continue
            if (item.menu === editorFileMenu || item.menu === editorEditMenu) item.visible = !win.publishMode
            else if (item.menu === publishMenu) item.visible = win.publishMode
        }
    }
    // Local feedback shares the status notice; no command produces these messages,
    // so the window shows them itself and falls back to App.notice afterwards.
    function notify(message, level) { win.localNotice = message; win.localNoticeLevel = level || "info"; localNoticeTimer.restart() }
    // app.connection returns the copyable AI access document. Qt Quick has no
    // clipboard API, so the window keeps one hidden text sink for that write.
    function copyConnectionInfo() {
        const reply = App.invoke("app.connection", {}) || {}
        if (reply.error) { win.notify(String(reply.error), "error"); return }
        clipboardSink.text = String(reply.text || "")
        clipboardSink.selectAll(); clipboardSink.copy()
        win.notify("AI 接入信息已复制", "info")
    }
    function exportOriginal(path) {
        const reply = App.invoke("project.export_original", {path: path, overwrite: true}) || {}
        if (reply.error) { win.notify("导出失败：" + reply.error, "error"); return }
        win.notify("工程已导出：" + (reply.path || path), "info")
    }
    Component.onCompleted: {
        win.applyMenuBarMode()
        // The shell opens the design graph for editors; a published window replaces
        // it with the runtime surface after the workspace finishes its first open.
        if (win.publishMode && !win.childWindow) Qt.callLater(() => App.workspaces.open("runtime", win.hostId))
    }
    onPublishModeChanged: Qt.callLater(win.applyMenuBarMode)
    // Project switches take two steps while a session is live: the command asks
    // for a decision, the window asks the user, then stops the run and retries.
    // Both failures land in a dialog because the notice alone is easy to miss.
    function openProjectAt(path) { startProjectCommand("project.open", {path: path, discard: true}) }
    function newProject() { startProjectCommand("project.new", {discard: true}) }
    function startProjectCommand(op, args) {
        const reply = App.invoke(op, args) || {}
        if (reply.error) { showProjectError(reply.error); return }
        if (reply.requires_runtime_stop) {
            pendingProjectOp = op; pendingProjectArgs = args
            runtimeStopDialog.projectName = App.project.name || ""
            runtimeStopDialog.open(); return
        }
        projectJob = reply.job || ""
    }
    function confirmRuntimeStop() {
        runtimeStopDialog.close()
        const reply = App.invoke("runtime.stop", {}) || {}
        if (reply.error) { showProjectError(reply.error); return }
        runtimeStopJob = reply.job || ""
        if (!runtimeStopJob) showProjectError("无法停止运行")
    }
    function showProjectError(message) { projectErrorDialog.message = message; projectErrorDialog.open() }
    function save() {
        if (App.projectPath.length) savingJob = App.invoke("project.save", {}).job || ""
        else saveProject.open()
    }
    function saveAs() { saveProject.open() }
    property string softwareSaveJob: ""
    property string softwareStopJob: ""
    property bool softwarePendingSave: false
    property bool softwareStarted: false
    function exportSoftware() {
        if (win.softwareStarted) return
        win.softwareStarted = true
        if (!App.projectPath.length) { win.softwarePendingSave = true; saveProject.open(); return }
        if (App.dirty) { softwareSavePrompt.open(); return }
        win.continueSoftwareAfterSave()
    }
    function saveSoftwareAndContinue() {
        softwareSavePrompt.close()
        const reply = App.invoke("project.save", {}) || {}
        if (reply.error) { win.notify("保存失败：" + reply.error, "error"); win.softwareStarted = false; return }
        win.softwareSaveJob = reply.job || ""
        if (!win.softwareSaveJob) win.continueSoftwareAfterSave()
    }
    function continueSoftwareAfterSave() {
        if (App.runtime.running) { softwareRuntimeStop.open(); return }
        softwareExport.present()
        win.softwareStarted = false
    }
    function stopRuntimeForSoftware() {
        softwareRuntimeStop.close()
        const reply = App.invoke("runtime.stop", {}) || {}
        if (reply.error) { win.notify("停止运行失败：" + reply.error, "error"); win.softwareStarted = false; return }
        win.softwareStopJob = reply.job || ""
        if (!win.softwareStopJob) { softwareExport.present(); win.softwareStarted = false }
    }
    onClosing: close => { close.accepted = !!App.invoke("window.close",{window:hostId}).closed }
    PropertyDialogs { id:propertyDialogs; anchors.fill:parent; hostId:win.hostId }
    Connections {
        target: App
        function onExitConfirmation(window) { if (window === hostId) { pendingAction = "close"; unsaved.open() } }
        function onJobFinished(id) {
            if (id === savingJob) { savingJob = ""; if (App.job(id).state === "complete" && pendingAction.length) continueAction() }
            // A failed switch must surface even though the retry path works.
            if (id === projectJob) {
                projectJob = ""
                if (App.job(id).state !== "complete") showProjectError(App.job(id).error || "打开工程失败")
            }
            // The confirmed switch continues only after the session is gone.
            if (id === runtimeStopJob) {
                runtimeStopJob = ""
                if (App.job(id).state !== "complete") { showProjectError(App.job(id).error || "停止运行失败"); return }
                startProjectCommand(pendingProjectOp, pendingProjectArgs)
            }
            if (id === softwareSaveJob) {
                softwareSaveJob = ""
                if (App.job(id).state !== "complete") { win.notify("保存失败：" + (App.job(id).error || "未知错误"), "error"); win.softwareStarted = false; return }
                win.continueSoftwareAfterSave()
            }
            if (id === softwareStopJob) {
                softwareStopJob = ""
                if (App.job(id).state !== "complete") { win.notify("停止运行失败：" + (App.job(id).error || "未知错误"), "error"); win.softwareStarted = false; return }
                softwareExport.present(); win.softwareStarted = false
            }
        }
        function onPluginSettingsRequested(){if(!win.childWindow)settingsWindow.present(win.publishMode?"general":"plugins")}
        function onSettingsRequested(page){if(!win.childWindow)settingsWindow.present(String(page||""))}
        function onPluginOpenRequested(){if(!win.childWindow)pluginVersions.present()}
        function onPluginExportRequested(scope,plugin){if(!win.childWindow)pluginExporter.present(scope,plugin)}
    }
    // Cycling reaches every tab the workspace owns, including hidden editor pages,
    // so it stays an editor gesture.
    Shortcut { sequence: "Ctrl+Tab"; enabled:!win.publishMode; onActivated: App.workspaces.cycle(hostId, 1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; enabled:!win.publishMode; onActivated: App.workspaces.cycle(hostId, -1) }
    Shortcut { sequence: "F5"; onActivated: App.invoke("runtime.start",{debug:true}) }
    Shortcut { sequence: "Ctrl+F5"; onActivated: App.invoke("runtime.start",{debug:false}) }
    Shortcut { sequence: "F6"; onActivated: App.invoke("runtime.pause",{}) }
    Shortcut { sequence: "F10"; enabled:!!App.runtime.debug&&!App.runtime.running;onActivated:App.invoke("runtime.step",{mode:"batch"}) }
    Shortcut { sequence: "F11"; enabled:!!App.runtime.debug&&!App.runtime.running;onActivated:App.invoke("runtime.step",{mode:"node"}) }
    Shortcut { sequence: "Ctrl+Shift+F5";onActivated:App.invoke("runtime.reset",{}) }
    Shortcut { sequence: "Shift+F5"; onActivated: App.invoke("runtime.stop", {}) }
    menuBar: MenuBar {
        visible:!win.childWindow
        height:visible?implicitHeight:0
        enabled:visible
        // Publish entry point. applyMenuBarMode() hides it while editing.
        Menu {
            id:publishMenu; objectName:"menu.publish"; title:"应用"
            Action { objectName:"menu.connection"; text:"复制 AI 接入信息"; onTriggered:win.copyConnectionInfo() }
            MenuItem { objectName:"menu.exportOriginal"; text:"导出工程…"; visible:App.retainOriginal; onTriggered:exportOriginalFile.open() }
            MenuSeparator {}
            Action { text:"关闭窗口"; onTriggered:win.requestAction("close") }
        }
        Menu {
            id:editorFileMenu; objectName:"menu.file"
            title: "文件"
            Action { text: "新建工程"; shortcut: StandardKey.New; enabled: !win.publishMode; onTriggered: win.requestAction("new") }
            Action { text: "打开工程…"; shortcut: StandardKey.Open; enabled: !win.publishMode; onTriggered: win.requestAction("open") }
            Action { text: "保存工程"; shortcut: StandardKey.Save; enabled: !win.publishMode; onTriggered: win.save() }
            Action { text: "另存为…"; enabled: !win.publishMode; onTriggered: win.saveAs() }
            Action { text: "工程信息…"; enabled: !win.publishMode; onTriggered: projectInfo.present() }
            Action { objectName:"menu.exportSoftware"; text: "导出为软件…"; enabled: !win.publishMode; onTriggered: win.exportSoftware() }
            Action { text:"导出工程插件…";enabled: !win.publishMode;onTriggered:pluginExporter.present("project","") }
            MenuSeparator {}
            Action { text: "关闭窗口"; onTriggered: win.requestAction("close") }
        }
        Menu {
            id:editorEditMenu; objectName:"menu.edit"
            title: "编辑"
            Action { text: "撤销"; shortcut: StandardKey.Undo; enabled: !App.readOnly; onTriggered: App.invoke("history.undo", {}) }
            Action { text: "重做"; shortcut: StandardKey.Redo; enabled: !App.readOnly; onTriggered: App.invoke("history.redo", {}) }
            MenuSeparator {}
            Action { text: App.readOnly ? "进入编辑模式" : "设为只读"; enabled: !win.publishMode; onTriggered: App.invoke("preferences.set", {read_only: !App.readOnly}) }
        }
        Menu {
            title: "工作区"
            Repeater {
                // A published window switches between runtime surfaces only.
                model:win.publishMode?App.workspaces.available.filter(p=>win.publishPages.indexOf(p.key)>=0):App.workspaces.available
                MenuItem { required property var modelData; text: modelData.title; onTriggered: App.workspaces.open(modelData.key, hostId) }
            }
        }
        Menu {
            objectName:"menu.settings";title: "设置"
            Action { text: "设置…"; shortcut: "Ctrl+,"; onTriggered: settingsWindow.present() }
        }
        Menu {
            title: "帮助"
            Action{text:"检查更新…";onTriggered:updateDialog.open()}
            Action { text: "关于与本地接口"; onTriggered: about.open() }
        }
    }
    // Workspace navigation stays above the page; shared engine controls stay
    // below it. The library has a compact window header and no workspace tab.
    ColumnLayout {
        anchors.fill:parent;spacing:0
        Rectangle {
            objectName:"workspace.tabs."+win.hostId
            Layout.fillWidth:true;Layout.preferredHeight:38;color:Theme.dark?"#101924":"#DCE5ED"
            Row {
                id:tabStrip;spacing:1;visible:win.activePage!=="library"
                Repeater {
                    // A published window keeps runtime tabs; editor pages stay hidden
                    // even when the shell opened one at startup.
                    model:win.publishMode?win.tabs.filter(t=>win.publishPages.indexOf(t.key)>=0):win.tabs
                    Rectangle {
                        id:tab;required property var modelData;required property int index
                        objectName:"workspace.tab."+modelData.key;width:140;height:38
                        color:modelData.key===win.activePage?Theme.panel:hover.containsMouse?Theme.raised:"transparent"
                        Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:2;color:Theme.blue;visible:tab.modelData.key===win.activePage}
                        Image{x:10;anchors.verticalCenter:parent.verticalCenter;width:18;height:18;source:"image://icons/"+(tab.modelData.icon||tab.modelData.key)+"/"+encodeURIComponent((tab.modelData.key===win.activePage?Theme.text:Theme.muted).toString());sourceSize.width:36;sourceSize.height:36}
                        Label{x:35;width:72;anchors.verticalCenter:parent.verticalCenter;text:tab.modelData.title;elide:Text.ElideRight;color:tab.modelData.key===win.activePage?Theme.text:Theme.muted}
                        MouseArea {
                            id:hover;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.LeftButton|Qt.RightButton
                            property point start;property bool dragging:false
                            onPressed:m=>{start=Qt.point(m.x,m.y);dragging=false;if(m.button===Qt.RightButton&&!win.publishMode){tabMenu.key=tab.modelData.key;tabMenu.popup()}}
                            onClicked:m=>{if(m.button===Qt.LeftButton&&!dragging)App.workspaces.open(tab.modelData.key,win.hostId)}
                            onPositionChanged:m=>{if(pressed&&!dragging&&Math.hypot(m.x-start.x,m.y-start.y)>10){dragging=true;App.workspaces.dragPage(tab.modelData.key)}}
                        }
                        IconButton{anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;symbol:"close";text:"关闭标签";width:28;height:28;visible:!win.publishMode;onClicked:App.workspaces.closePage(tab.modelData.key)}
                        DropArea{anchors.fill:parent;keys:["application/x-corig-workspace"];onEntered:mark.visible=true;onExited:mark.visible=false;onDropped:d=>{mark.visible=false;App.workspaces.acceptDrop(d.getDataAsString("application/x-corig-workspace"),win.hostId,tab.index);d.acceptProposedAction()}}
                        Rectangle{id:mark;visible:false;width:3;height:parent.height;color:Theme.blue}
                    }
                }
                IconButton{objectName:"workspace.add";symbol:"plus";text:"打开工作区";visible:!win.childWindow&&!win.publishMode;onClicked:pagesMenu.popup()}
            }
            Label{visible:win.activePage==="library";anchors.left:parent.left;anchors.leftMargin:12;anchors.verticalCenter:parent.verticalCenter;text:"插件库 · "+(App.libraryTarget==="layout"?"界面设计":"功能设计");color:Theme.text}
            Row {
                id:childTools;visible:win.childWindow;anchors.right:parent.right;height:parent.height
                IconButton{objectName:"window.pin."+win.hostId;symbol:"pin";ink:win.pinned?Theme.blue:Theme.text;text:win.pinned?"取消置顶":"固定在顶部";height:36;checkable:true;checked:win.pinned;onClicked:App.workspaces.pinWindow(win.hostId,checked)}
                IconButton{symbol:"dock";text:"定位主窗口";height:36;onClicked:App.workspaces.focusMain()}
            }
            DropArea{visible:win.activePage!=="library";x:tabStrip.width;width:Math.max(0,(win.childWindow?childTools.x:parent.width)-x);height:parent.height;keys:["application/x-corig-workspace"];onDropped:d=>{App.workspaces.acceptDrop(d.getDataAsString("application/x-corig-workspace"),win.hostId,-1);d.acceptProposedAction()}}
        }
        Item {
            objectName:"workspace.content";Layout.fillWidth:true;Layout.fillHeight:true;clip:true
            Label{anchors.centerIn:parent;visible:!win.activePage.length;text:"工作区已移至子窗口。\n从“工作区”菜单打开或定位页面。";color:Theme.muted;horizontalAlignment:Text.AlignHCenter;lineHeight:1.5}
        }
        Rectangle{visible:!win.childWindow;Layout.fillWidth:true;height:visible?1:0;color:Theme.border}
        // Share one compact row: controls keep their width, notices use the
        // remaining space, and the single runtime summary stays right aligned.
        Rectangle {
            visible:!win.childWindow;Layout.fillWidth:true;Layout.preferredHeight:visible?40:0;color:Theme.panel
            RowLayout {
                objectName:"execution.toolbar"
                anchors.fill:parent;anchors.leftMargin:8;anchors.rightMargin:12;spacing:12
                DebugControls{Layout.alignment:Qt.AlignVCenter}
                Rectangle{implicitWidth:1;implicitHeight:20;color:Theme.border;Layout.alignment:Qt.AlignVCenter}
                Label {
                    objectName:"execution.notice";Layout.fillWidth:true;Layout.minimumWidth:0
                    text:win.localNotice.length?win.localNotice:App.notice;elide:Text.ElideRight;color:(win.localNotice.length?win.localNoticeLevel:App.noticeLevel)==="error"?Theme.error:Theme.muted;font.pixelSize:12
                    ToolTip.visible:noticeHover.hovered&&truncated;ToolTip.delay:400;ToolTip.text:text
                    HoverHandler{id:noticeHover}
                }
                BusyIndicator{running:App.busy;visible:running;implicitWidth:20;implicitHeight:20}
                RowLayout {
                    objectName:"execution.summary";spacing:12
                    Label{objectName:"execution.mode";text:App.runtime.debug?"调试":"普通";color:Theme.muted;font.pixelSize:12}
                    Label{objectName:"execution.access";text:App.readOnly?"只读":"编辑";color:Theme.muted;font.pixelSize:12}
                    Label {
                        objectName:"execution.status"
                        text:App.executionPending?"执行操作中":App.runtime.running?"运行中":App.runtime.active?"已暂停":"已停止"
                        color:App.runtime.running?Theme.green:Theme.muted;font.pixelSize:12
                        ToolTip.visible:statusHover.hovered
                        ToolTip.text:(App.runtime.cursor||{}).next_node?"批次 "+App.runtime.cursor.batch+"，下一节点 "+App.runtime.cursor.next_node:"当前没有未完成批次"
                        HoverHandler{id:statusHover}
                    }
                    Label{objectName:"execution.elapsed";text:Number(App.runtime.elapsed||0).toFixed(2)+" s";color:Theme.muted;font.family:"Consolas";font.pixelSize:12}
                }
            }
        }
    }
    Menu {
        id: pagesMenu
        Repeater { model:win.publishMode?App.workspaces.available.filter(p=>win.publishPages.indexOf(p.key)>=0):App.workspaces.available; MenuItem { required property var modelData; text:modelData.title; onTriggered:App.workspaces.open(modelData.key,win.hostId) } }
    }
    Menu {
        id: tabMenu; property string key
        MenuItem { text: "移至新窗口"; onTriggered: App.workspaces.detach(tabMenu.key) }
        MenuItem { text: "关闭标签"; onTriggered: App.workspaces.closePage(tabMenu.key) }
        MenuItem { text:"从工程移除工作区"; visible:App.workspaces.available.some(p=>p.key===tabMenu.key&&!p.builtin); enabled:!App.readOnly&&!App.busy; onTriggered:App.invoke("workspace.remove",{page:tabMenu.key}) }
    }
    FileDialog { id: openProject; title: "打开工程"; nameFilters: ["CoRig 工程 (*.crpj *.corig)"]; onAccepted: win.openProjectAt(selectedFile.toString()) }
    FileDialog {
        id:saveProject;title:"另存为工程";fileMode:FileDialog.SaveFile;defaultSuffix:"crpj";nameFilters:["CoRig 工程 (*.crpj)"]
        onAccepted:{
            const reply=App.invoke("project.save",{path:selectedFile.toString()})||{}
            if(reply.error){win.notify("保存失败："+reply.error,"error");if(win.softwarePendingSave){win.softwarePendingSave=false;win.softwareStarted=false};return}
            win.savingJob=reply.job||""
            if(win.softwarePendingSave){win.softwarePendingSave=false;win.softwareSaveJob=win.savingJob;if(!win.softwareSaveJob)win.continueSoftwareAfterSave()}
        }
        onRejected: if(win.softwarePendingSave){win.softwarePendingSave=false;win.softwareStarted=false}
    }
    // Published applications export the retained original without touching the
    // runtime snapshot; the native dialog already asked about overwriting.
    FileDialog { id: exportOriginalFile; title: "导出工程"; fileMode: FileDialog.SaveFile; defaultSuffix: "crpj"; nameFilters: ["CoRig 工程 (*.crpj)"]; onAccepted: win.exportOriginal(selectedFile.toString()) }
    // Hidden sink for the clipboard write: Qt Quick exposes no clipboard API, and the
    // publish window must not add a command for it.
    TextEdit { id: clipboardSink; visible: false; width: 1; height: 1 }
    Timer { id: localNoticeTimer; interval: 2600; onTriggered: { win.localNotice = ""; win.localNoticeLevel = "info" } }
    ProjectInfoDialog{id:projectInfo}
    SoftwareExportDialog{id:softwareExport}
    SettingsWindow{id:settingsWindow}
    PluginExportDialog{id:pluginExporter}
    PluginVersionDialog{id:pluginVersions}
    UpdateDialog{id:updateDialog}
    Dialog {
        id: softwareSavePrompt; title: "先保存工程"; modal: true; anchors.centerIn: parent; width: 430
        contentItem: Label { text: "导出为软件需要先保存当前工程。保存后继续导出？"; color: Theme.text; padding: 12; wrapMode: Text.WordWrap }
        footer: DialogButtonBox {
            ActionButton { text: "保存并继续"; onClicked: win.saveSoftwareAndContinue() }
            ActionButton { text: "取消"; onClicked: { softwareSavePrompt.close(); win.softwareStarted = false } }
        }
    }
    Dialog {
        id: softwareRuntimeStop; title: "工程正在运行"; modal: true; anchors.centerIn: parent; width: 450
        contentItem: Label { text: "导出为软件前需要停止当前运行，避免把运行中的临时状态误当成工程内容。停止运行并继续？"; color: Theme.text; padding: 12; wrapMode: Text.WordWrap }
        footer: DialogButtonBox {
            ActionButton { text: "停止并继续"; onClicked: win.stopRuntimeForSoftware() }
            ActionButton { text: "取消"; onClicked: { softwareRuntimeStop.close(); win.softwareStarted = false } }
        }
    }
    Dialog {
        id: unsaved; title: "工程有未保存修改"; modal: true; anchors.centerIn: parent; width: 400
        contentItem: Label { text: "保存当前工程后再继续？"; color: Theme.text; padding: 12 }
        footer: DialogButtonBox {
            ActionButton { text: "保存"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: { unsaved.close(); win.save() } }
            ActionButton { text: "丢弃修改"; DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole; onClicked: { unsaved.close(); win.continueAction() } }
            ActionButton { text: "取消"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { win.pendingAction = ""; unsaved.close() } }
        }
    }
    Dialog {
        id: runtimeStopDialog; objectName: "dialog.project.runtime"; modal: true; anchors.centerIn: parent; width: 430
        property string projectName: ""
        title: "工程正在运行"
        contentItem: Label {
            text: "当前工程正在运行，切换工程会结束本次运行。停止运行并打开「" + runtimeStopDialog.projectName + "」？"
            color: Theme.text; padding: 12; wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            ActionButton { objectName: "dialog.project.runtime.open"; text: "停止并打开"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: win.confirmRuntimeStop() }
            ActionButton { objectName: "dialog.project.runtime.cancel"; text: "取消"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { win.pendingProjectOp = ""; runtimeStopDialog.close() } }
        }
    }
    Dialog {
        id: projectErrorDialog; objectName: "dialog.project.error"; modal: true; anchors.centerIn: parent; width: 430
        property string message: ""
        title: "无法打开工程"
        contentItem: Label { objectName: "dialog.project.error.text"; text: projectErrorDialog.message; color: Theme.text; padding: 12; wrapMode: Text.WordWrap }
        footer: DialogButtonBox { ActionButton { text: "关闭"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: projectErrorDialog.close() } }
    }
    Dialog {
        id: about; title: "联调 · CoRig 0.4"; width: 510; anchors.centerIn: parent; standardButtons: Dialog.Close
        Label { width: parent.width; wrapMode: Text.WrapAnywhere; color: Theme.text; text: "原生 C++ / Qt Quick 功能设计工作台\n\n本地接口：" + App.dataDirectory + "/.sessions/api.json\n\n五个工作区共享同一个工程与运行会话。可将标签拖出为独立窗口，也可拖回其他窗口。" }
    }
}
