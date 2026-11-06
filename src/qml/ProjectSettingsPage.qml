import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "settings.project.page"
    property bool active: false
    property bool includePlugins: true
    property string error: ""
    function activate() {
        includePlugins = (App.project.save_options || {}).include_plugins !== false
        error = ""
    }
    onActiveChanged: if (active) activate()
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "当前工程的保存选项。普通保存和另存为都会使用这里的设置。"
            color: Theme.muted
        }
        CheckBox {
            id: include
            objectName: "project.options.include_plugins"
            text: "包含工程使用的全部插件"
            checked: root.includePlugins
            onToggled: root.includePlugins = checked
        }
        Label {
            Layout.fillWidth: true
            text: root.includePlugins ? "保存所用官方、用户插件及随包资源，换电脑后可从工程内加载。" : "只记录所需插件和版本，打开时需要本机具有这些插件。"
            color: Theme.muted
            wrapMode: Text.WordWrap
        }
        Label { Layout.fillWidth: true; visible: !!root.error; text: root.error; color: Theme.error; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "project.options.apply"
                text: "应用"
                onClicked: {
                    const reply = App.invoke("project.options", { include_plugins: root.includePlugins }) || {}
                    root.error = reply.error || ""
                    if (!root.error) root.activate()
                }
            }
            ActionButton { text: "重置"; onClicked: root.activate() }
        }
        Item { Layout.fillHeight: true }
    }
}
