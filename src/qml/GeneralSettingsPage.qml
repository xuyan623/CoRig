import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "settings.general.page"
    property bool active: false
    ColumnLayout {
        anchors.fill: parent
        spacing: 14
        Label { text: "外观"; color: Theme.text; font.bold: true }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: "主题"; color: Theme.text }
            ActionButton { text: "深色"; enabled: !Theme.dark; onClicked: App.invoke("preferences.set", { theme: "dark" }) }
            ActionButton { text: "浅色"; enabled: Theme.dark; onClicked: App.invoke("preferences.set", { theme: "light" }) }
        }
        Label { Layout.fillWidth: true; text: "切换即时生效。"; color: Theme.muted }
        Item { Layout.fillHeight: true }
    }
}
