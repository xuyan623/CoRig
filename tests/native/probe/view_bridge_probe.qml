import QtQuick

// A third-party view loaded by the design preview. It declares the documented
// bridge contract, so previewing proves the read-only bridge keeps the same
// members while refusing every write.
Item {
    id:probe
    required property var bridge
    objectName:"view.bridge.probe"
    readonly property var bridgeRef:bridge
    readonly property string actionError:bridge.action("bridge_probe",{}).error||""
    readonly property string commitError:bridge.commit({color:"#101010"}).error||""
    readonly property string configureError:bridge.configure({value:1}).error||""
}