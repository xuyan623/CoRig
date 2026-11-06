import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    objectName: "software.export.dialog"
    title: "导出为软件"
    modal: true
    anchors.centerIn: parent
    width: Math.min(620, parent ? parent.width - 48 : 620)
    closePolicy: (page === 1 && (stateText() === "running" || stateText() === "cancelling"))
                 ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    property int page: 0
    property bool initialDirectory: false
    property string directory: ""
    property string pendingTarget: ""
    property string error: ""
    property bool showAdvanced: false
    property var exportState: App.softwareExport || ({})
    readonly property var state: App.softwareExport || ({})
    function stateText() { return String(root.state.state || "idle") }
    function stageText(stage) {
        return ({prepare:"正在校验工程",collect:"正在收集运行依赖",verify:"正在校验依赖",
                 assemble:"正在打包应用",container:"正在封装单文件",commit:"正在写入目标",
                 done:"已完成"})[stage] || "正在保存"
    }
    function localPath(url) {
        let text = String(url || "")
        if (text.indexOf("file:///") === 0) text = text.substring(8)
        else if (text.indexOf("file://") === 0) text = text.substring(7)
        return decodeURIComponent(text)
    }
    function present() {
        error = ""
        page = 0
        showAdvanced = false
        nameInput.text = App.project.name || "未命名工程"
        versionInput.text = App.project.version || "1.0.0"
        appIdInput.text = ""
        retainInput.checked = false
        directory = App.softwareExportDirectory || (App.dataDirectory + "/project")
        initialDirectory = true
        open()
        directoryDialog.currentFolder = "file:///" + directory.replace(/\\/g, "/")
        directoryDialog.open()
    }
    function begin(overwrite) {
        error = ""
        const reply = App.startSoftwareExport({
            project: App.projectPath,
            directory: root.directory,
            name: nameInput.text,
            version: versionInput.text,
            app_id: appIdInput.text,
            retain_original: retainInput.checked,
            overwrite: !!overwrite,
            python: App.pythonPath
        }) || {}
        if (reply.exists) {
            pendingTarget = reply.target || ""
            overwritePrompt.open()
            return
        }
        if (reply.error) { error = String(reply.error); return }
        page = 1
    }
    function openFolder(path) {
        const target = path.indexOf("/") >= 0 && path.indexOf("\\") < 0 ? path : path.replace(/\\/g, "/")
        Qt.openUrlExternally("file:///" + target)
    }
    function closeResult() {
        root.close()
    }

    contentItem: ColumnLayout {
        spacing: 12
        StackLayout {
            currentIndex: root.page
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 10
                Label { Layout.fillWidth: true; text: "导出目录"; color: Theme.muted }
                RowLayout {
                    Layout.fillWidth: true
                    Label { objectName: "software.export.directory"; Layout.fillWidth: true; text: root.directory; color: Theme.text; elide: Text.ElideMiddle }
                    ActionButton { text: "更改…"; onClicked: directoryDialog.open() }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10
                    Label { text: "应用名称"; color: Theme.text }
                    TextField { id: nameInput; objectName: "software.export.name"; Layout.fillWidth: true; selectByMouse: true }
                    Label { text: "应用版本"; color: Theme.text }
                    TextField { id: versionInput; objectName: "software.export.version"; Layout.fillWidth: true; selectByMouse: true; placeholderText: "1.0.0" }
                }
                CheckBox {
                    id: retainInput
                    objectName: "software.export.retain"
                    text: "在应用中保留可导出的原始工程"
                }
                ActionButton {
                    text: root.showAdvanced ? "收起高级选项" : "高级选项…"
                    onClicked: root.showAdvanced = !root.showAdvanced
                }
                ColumnLayout {
                    visible: root.showAdvanced
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: "应用标识（app id）"; color: Theme.text }
                    TextField {
                        id: appIdInput
                        objectName: "software.export.app_id"
                        Layout.fillWidth: true
                        selectByMouse: true
                        placeholderText: "留空自动生成"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "修改标识会让新应用使用独立的数据目录，旧应用数据不会自动迁移。"
                        color: Theme.muted
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: !!root.error
                    text: root.error
                    color: Theme.error
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        objectName: "software.export.start"
                        text: "开始导出"
                        enabled: nameInput.text.trim().length > 0 && versionInput.text.trim().length > 0
                        onClicked: root.begin(false)
                    }
                    ActionButton { text: "取消"; onClicked: root.close() }
                }
            }
            ColumnLayout {
                spacing: 10
                Label {
                    objectName: "software.export.status"
                    Layout.fillWidth: true
                    text: root.stateText() === "success" ? "导出完成"
                          : root.stateText() === "failed" ? "导出失败"
                          : root.stateText() === "cancelled" ? "已取消导出"
                          : "正在保存…"
                    color: root.stateText() === "failed" ? Theme.error : Theme.text
                    font.pixelSize: 18
                    font.bold: true
                }
                Label {
                    objectName: "software.export.stage"
                    Layout.fillWidth: true
                    visible: root.stateText() === "running" || root.stateText() === "cancelling"
                    text: root.stageText(root.state.stage) + " · " + Number(root.state.progress || 0) + "%"
                    color: Theme.muted
                }
                ProgressBar {
                    objectName: "software.export.progress"
                    Layout.fillWidth: true
                    from: 0; to: 100
                    value: Number(root.state.progress || 0)
                    visible: root.stateText() === "running" || root.stateText() === "cancelling"
                }
                Label {
                    objectName: "software.export.artifact"
                    Layout.fillWidth: true
                    visible: root.stateText() === "success"
                    text: root.state.artifact || root.state.path || ""
                    color: Theme.text
                    wrapMode: Text.WrapAnywhere
                }
                Label {
                    objectName: "software.export.error"
                    Layout.fillWidth: true
                    visible: root.stateText() === "failed"
                    text: root.state.error || "发布器返回失败"
                    color: Theme.error
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.stateText() === "success"
                    ActionButton { text: "打开所在文件夹"; onClicked: root.openFolder(root.state.artifact || root.state.path || "") }
                    Item { Layout.fillWidth: true }
                    ActionButton { objectName: "software.export.close"; text: "关闭"; onClicked: root.closeResult() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.stateText() === "failed" || root.stateText() === "cancelled"
                    ActionButton { visible: root.stateText() === "failed"; text: "打开日志"; onClicked: App.openSoftwareExportLog() }
                    Item { Layout.fillWidth: true }
                    ActionButton { visible: root.stateText() === "failed"; text: "返回"; onClicked: root.page = 0 }
                    ActionButton { text: "关闭"; onClicked: root.closeResult() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.stateText() === "running" || root.stateText() === "cancelling"
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        objectName: "software.export.cancel"
                        text: root.stateText() === "cancelling" ? "正在取消…" : "取消导出"
                        enabled: root.stateText() === "running"
                        onClicked: cancelPrompt.open()
                    }
                }
            }
        }
    }

    FolderDialog {
        id: directoryDialog
        title: "选择导出目录"
        onAccepted: {
            root.directory = root.localPath(selectedFolder)
            App.setSoftwareExportDirectory(root.directory)
            root.initialDirectory = false
            directoryDialog.close()
            root.page = 0
        }
        onRejected: {
            if (root.initialDirectory) { root.initialDirectory = false; root.close() }
        }
    }
    Dialog {
        id: overwritePrompt
        title: "目标已存在"
        modal: true
        anchors.centerIn: parent
        width: 460
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.text
            text: "目标文件已存在：\n" + root.pendingTarget + "\n\n是否覆盖？"
        }
        footer: DialogButtonBox {
            ActionButton { objectName: "software.export.overwrite"; text: "覆盖"; onClicked: { overwritePrompt.close(); root.begin(true) } }
            ActionButton { objectName: "software.export.reselect"; text: "重新选择目录"; onClicked: { overwritePrompt.close(); directoryDialog.open() } }
            ActionButton { objectName: "software.export.overwriteCancel"; text: "取消"; onClicked: overwritePrompt.close() }
        }
    }
    Dialog {
        id: cancelPrompt
        title: "取消导出"
        modal: true
        anchors.centerIn: parent
        width: 420
        Label { width: parent.width; wrapMode: Text.WordWrap; color: Theme.text; text: "确定取消导出吗？未完成的文件会被清理。" }
        footer: DialogButtonBox {
            ActionButton { text: "取消导出"; onClicked: { cancelPrompt.close(); App.cancelSoftwareExport() } }
            ActionButton { text: "继续导出"; onClicked: cancelPrompt.close() }
        }
    }
}
