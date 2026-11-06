import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "project.info.dialog"
    title: "工程信息"
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 48 : 560)
    height: 350
    property string error: ""

    function present() {
        nameInput.text = App.project.name || ""
        descriptionInput.text = App.project.description || ""
        authorInput.text = App.project.author || ""
        versionInput.text = App.project.version || "1.0.0"
        error = ""
        open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            Label { text: "工程名称"; color: Theme.text }
            TextField { id: nameInput; objectName: "project.info.name"; Layout.fillWidth: true; selectByMouse: true }
            Label { text: "工程版本"; color: Theme.text }
            TextField { id: versionInput; objectName: "project.info.version"; Layout.fillWidth: true; selectByMouse: true; placeholderText: "1.0.0" }
            Label { text: "作者"; color: Theme.text }
            TextField { id: authorInput; objectName: "project.info.author"; Layout.fillWidth: true; selectByMouse: true }
            Label { text: "描述"; color: Theme.text; Layout.alignment: Qt.AlignTop }
            TextArea {
                id: descriptionInput
                objectName: "project.info.description"
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                wrapMode: Text.Wrap
                selectByMouse: true
                placeholderText: "可选"
                padding: 6
                background: Rectangle { color: Theme.canvas; border.color: Theme.border; radius: 4 }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !!root.error
            text: root.error
            color: Theme.error
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            text: App.projectPath || "尚未保存"
            color: Theme.muted
            elide: Text.ElideMiddle
        }
    }
    footer: DialogButtonBox {
        ActionButton {
            objectName: "project.info.apply"
            text: "确定"
            onClicked: {
                const reply = App.invoke("project.info", {
                    name: nameInput.text,
                    description: descriptionInput.text,
                    author: authorInput.text,
                    version: versionInput.text
                }) || {}
                if (reply.error) { root.error = String(reply.error); return }
                root.close()
            }
        }
        ActionButton { text: "取消"; onClicked: root.close() }
    }
}
