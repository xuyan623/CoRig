import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CoRig.Native 1.0

Item {
    id:root;required property var host
    function call(action,args){if(action==="camera"){scene.camera=args;return scene.camera}if(action==="reset_camera"){scene.resetCamera();return scene.camera}if(action==="export_image")return App.invoke("capture",{target:"view."+host.nodeId+"."+host.viewKey,path:args.path||"",width:args.width||1600,height:args.height||1000});return({ok:false,error:"未知姿态视图操作"})}
    Rectangle{anchors.fill:parent;color:root.host.config.background_color||"#17212C";opacity:root.host.config.background_opacity===undefined?1:root.host.config.background_opacity}
    NativeAttitude{id:scene;anchors.fill:parent;observation:root.host.observation}
    SceneCameraGestures{camera:scene;enabled:!root.host.designMode}
    RowLayout{anchors.left:parent.left;anchors.top:parent.top;anchors.margins:6;IconButton{symbol:"reset";text:"复位相机";onClicked:scene.resetCamera()}IconButton{symbol:"export";text:"导出…";onClicked:exportFile.open()}}
    Label{anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;text:"X 红　Y 绿　Z 蓝";color:"#AFBFCE";font.pixelSize:10}
    Label{anchors.left:parent.left;anchors.bottom:parent.bottom;anchors.margins:10;text:(root.host.observation.state||{}).error||"左键旋转　右键平移　滚轮缩放";color:(root.host.observation.state||{}).valid===false?Theme.error:"#A5BACB";font.pixelSize:11}
    FileDialog{id:exportFile;title:"导出姿态视图";fileMode:FileDialog.SaveFile;defaultSuffix:"png";nameFilters:["PNG (*.png)"];onAccepted:App.invoke("view.call",{node:root.host.nodeId,view:root.host.viewKey,action:"export_image",args:{path:decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//,""))}})}
}

