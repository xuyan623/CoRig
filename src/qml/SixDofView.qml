import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CoRig.Native 1.0

Item {
    id:root
    required property var host
    readonly property var settings:root.host.viewProperties||({})
    readonly property var state:(root.host.observation||{}).state||({})
    readonly property var config:(root.host.observation||{}).config||({})
    function call(action,args){
        if(action==="camera"){scene.camera=args;return scene.camera}
        if(action==="reset_camera"){scene.resetCamera();return scene.camera}
        if(action==="restore_defaults"){scene.restoreDefaults();return scene.camera}
        if(action==="export_image")return App.invoke("capture",{target:"view."+host.nodeId+"."+host.viewKey,path:args.path||"",width:args.width||1600,height:args.height||1000})
        return({ok:false,error:"未知空间视图操作"})
    }
    Rectangle{anchors.fill:parent;color:root.settings.background||"#14212D"}
    NativeSixDof{id:scene;anchors.fill:parent;observation:root.host.observation;settings:root.settings}
    SceneCameraGestures{camera:scene;enabled:!root.host.designMode}
    RowLayout{anchors.left:parent.left;anchors.top:parent.top;anchors.margins:6;spacing:4
        IconButton{symbol:"reset";text:"复位相机";onClicked:scene.resetCamera()}
        IconButton{symbol:"refresh";text:"恢复默认外观";onClicked:scene.restoreDefaults()}
        IconButton{symbol:"export";text:"导出…";onClicked:exportFile.open()}
    }
    Label{anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;text:"X 红　Y 绿　Z 蓝";color:"#AFBFCE";font.pixelSize:10}
    Label{anchors.left:parent.left;anchors.bottom:parent.bottom;anchors.margins:10;font.pixelSize:11
        text:root.state.frozen?"定位失效，显示最后有效位姿":root.state.valid===false?(root.state.error||"等待位姿输入"):"左键旋转　右键平移　滚轮缩放"
        color:root.state.frozen?"#E0B36A":root.state.valid===false?Theme.error:"#A5BACB"}
    FileDialog{id:exportFile;title:"导出空间视图";fileMode:FileDialog.SaveFile;defaultSuffix:"png";nameFilters:["PNG (*.png)"];onAccepted:App.invoke("view.call",{node:root.host.nodeId,view:root.host.viewKey,action:"export_image",args:{path:decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//,""))}})}
}
