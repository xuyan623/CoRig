import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    objectName: "settings.python.page"
    property bool active: false
    property string error: ""
    function activate() { pythonInput.text = App.pythonPath || ""; error = "" }
    onActiveChanged: if (active) activate()
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "选择本机或虚拟环境中的 Python 3.10 及以上版本。官方功能无需 Python；软件不会修改所选环境。"
            color: Theme.muted
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: pythonInput; objectName: "settings.python"; Layout.fillWidth: true; placeholderText: "选择 python.exe" }
            ActionButton { text: "浏览…"; onClicked: pythonFile.open() }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "settings.python.apply"
                text: "应用"
                onClicked: {
                    const reply = App.invoke("preferences.set", { python: pythonInput.text }) || {}
                    root.error = reply.error || ""
                }
            }
            ActionButton {
                text: "重置"
                onClicked: { pythonInput.text = App.pythonPath || ""; root.error = "" }
            }
            ActionButton {
                text: "检查环境"
                onClicked: { pythonProbe.jobId = (App.invoke("python.probe", {}) || {}).job || "" }
            }
        }
        Label { Layout.fillWidth: true; visible: !!root.error; text: root.error; color: Theme.error; wrapMode: Text.WordWrap }
        TextArea {
            id: pythonProbe
            property string jobId: ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            wrapMode: Text.WrapAnywhere
            text: App.pythonPath ? "已选择：" + App.pythonPath : "尚未选择本地解释器"
        }
        Connections {
            target: App
            function onJobFinished(id) { if (id === pythonProbe.jobId) pythonProbe.text = JSON.stringify(App.job(id), null, 2) }
        }
    }
    FileDialog {
        id: pythonFile
        title: "选择本地 Python"
        nameFilters: ["Python (python.exe python3.exe)", "所有文件 (*)"]
        onAccepted: pythonInput.text = decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//, ""))
    }
}
