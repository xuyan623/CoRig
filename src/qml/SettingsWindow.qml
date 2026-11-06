import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "settings.window"
    title: "设置"
    modal: true
    anchors.centerIn: parent
    width: 560
    height: 390
    padding: 12
    // The editor and the published runtime share this window, but not every
    // page is meaningful in both modes. A page key (not a tab ordinal) is the
    // public selector so hidden pages cannot shift the meaning of an index.
    readonly property var pages: App.appMode === "publish"
        ? [
            { key: "general", title: "通用", component: generalPage },
            { key: "tasks", title: "后台任务", component: backgroundPage }
        ]
        : [
            { key: "general", title: "通用", component: generalPage },
            { key: "project", title: "工程", component: projectPage },
            { key: "plugins", title: "插件", component: pluginPage },
            { key: "python", title: "Python", component: pythonPage },
            { key: "resources", title: "资源额度", component: resourcePage },
            { key: "tasks", title: "后台任务", component: backgroundPage }
        ]
    property string requestedPage: ""
    property int pageIndex: 0
    function selectPage(key) {
        let index = 0
        for (let i = 0; i < root.pages.length; ++i) if (root.pages[i].key === key) { index = i; break }
        root.pageIndex = index
    }
    function present(pageKey) {
        requestedPage = pageKey || ""
        const storedWidth = Number(App.settingsValue("settings/width", 560))
        const storedHeight = Number(App.settingsValue("settings/height", 390))
        width = Number.isFinite(storedWidth) ? Math.min(560, Math.max(520, storedWidth)) : 560
        height = Number.isFinite(storedHeight) ? Math.min(390, Math.max(340, storedHeight)) : 390
        // Select before opening so the first controls query after
        // settings.open already addresses the requested page. The previous
        // callLater left a one-turn race for AI clients and GUI scripts.
        root.selectPage(root.requestedPage)
        open()
    }
    onOpened: root.selectPage(root.requestedPage)
    onPageIndexChanged: if (tabs && tabs.currentIndex !== root.pageIndex) tabs.currentIndex = root.pageIndex
    onClosed: {
        App.setSettingsValue("settings/width", width)
        App.setSettingsValue("settings/height", height)
    }
    contentItem: ColumnLayout {
        width: root.availableWidth
        spacing: 10
        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.maximumWidth: root.availableWidth
            clip: true
            font.pixelSize: 12
            onCurrentIndexChanged: if (root.pageIndex !== currentIndex) root.pageIndex = currentIndex
            Repeater {
                model: root.pages
                TabButton {
                    id: tabButton
                    required property var modelData
                    required property int index
                    text: modelData.title
                    checked: index === root.pageIndex
                    focusPolicy: Qt.NoFocus
                    width: root.availableWidth / Math.max(1, root.pages.length)
                    padding: 4
                    // Some platform styles dim unchecked tabs; keep every label at
                    // full opacity so the explicit theme text stays readable.
                    opacity: 1
                    // Keep every tab label readable on the dark panel; the selected
                    // tab is distinguished by its raised background and blue border.
                    contentItem: Text {
                        text: tabButton.text
                        font: tabButton.font
                        color: Theme.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: 3
                        color: parent.checked ? Theme.raised : "transparent"
                        border.width: parent.checked ? 1 : 0
                        border.color: Theme.blue
                    }
                    onClicked: root.pageIndex = index
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: root.availableWidth
            clip: true
            currentIndex: root.pageIndex
            Repeater {
                model: root.pages
                Loader {
                    required property var modelData
                    required property int index
                    sourceComponent: modelData.component
                    active: root.opened && root.pageIndex === index
                    onLoaded: if (item) item.active = active
                    onActiveChanged: if (item) item.active = active
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "settings.close"; text: "关闭"; onClicked: root.close() }
        }
    }
    Component { id: generalPage; GeneralSettingsPage {} }
    Component { id: projectPage; ProjectSettingsPage {} }
    Component { id: pluginPage; PluginManager {} }
    Component { id: pythonPage; PythonSettingsPage {} }
    Component { id: resourcePage; ResourceSettings {} }
    Component { id: backgroundPage; BackgroundTasks {} }
}
