import QtQuick
import QtQuick.Controls

// Observation is display-only: bounds, shape and colors are per-view
// properties, so one node can serve a round dial and a rectangular panel.
Item {
    id:root;required property var host
    readonly property var state:(root.host.observation||{}).state||({})
    readonly property var settings:root.host.viewProperties||({})
    readonly property bool valid:state.valid===true
    readonly property var xy:state.xy||[0,0]
    readonly property bool circle:(settings.shape||"circle")==="circle"
    readonly property real xMin:Number(settings.x_min===undefined?-1:settings.x_min)
    readonly property real xMax:Number(settings.x_max===undefined?1:settings.x_max)
    readonly property real yMin:Number(settings.y_min===undefined?-1:settings.y_min)
    readonly property real yMax:Number(settings.y_max===undefined?1:settings.y_max)
    // A degenerate bound would divide by zero, so the view falls back to zero.
    readonly property bool degenerate:Math.abs(xMax-xMin)<1e-9||Math.abs(yMax-yMin)<1e-9
    readonly property real nx:valid&&!degenerate?2*(Number(xy[0])-xMin)/(xMax-xMin)-1:0
    readonly property real ny:valid&&!degenerate?2*(Number(xy[1])-yMin)/(yMax-yMin)-1:0
    readonly property real divisor:circle?Math.max(1,Math.hypot(nx,ny)):1
    readonly property real px:Math.max(-1,Math.min(1,nx/divisor))
    readonly property real py:Math.max(-1,Math.min(1,ny/divisor))
    readonly property bool overflow:valid&&(circle?Math.hypot(nx,ny)>1.00001:Math.abs(nx)>1||Math.abs(ny)>1)
    implicitWidth:192;implicitHeight:352
    Rectangle{anchors.fill:parent;radius:8;border.color:"#324858";color:root.settings.background||"#172632"}
    Label{anchors.top:parent.top;anchors.topMargin:16;anchors.horizontalCenter:parent.horizontalCenter;text:root.settings.label||"二维坐标";color:"#D6E6EF";font.pixelSize:14}
    Rectangle{
        id:area;anchors.centerIn:parent;radius:root.circle?width/2:4;border.color:"#496374";color:"#0E1C27"
        width:Math.max(20,Math.min(root.width-32,root.height-112));height:width
        Rectangle{anchors.centerIn:parent;width:parent.width-16;height:1;color:"#2F4656"}
        Rectangle{anchors.centerIn:parent;width:1;height:parent.height-16;color:"#2F4656"}
        Rectangle{
            width:14;height:14;radius:7;border.color:"#F0F8FC";border.width:1
            visible:root.valid&&root.state.active===true
            x:(area.width-width)/2+root.px*(area.width-width)/2
            y:(area.height-height)/2-root.py*(area.height-height)/2
            color:root.state.pressed?(root.settings.pressed_color||"#F1B56D"):(root.settings.point_color||"#62CCE0")
        }
    }
    Label{
        anchors.bottom:parent.bottom;anchors.bottomMargin:18;anchors.horizontalCenter:parent.horizontalCenter
        horizontalAlignment:Text.AlignHCenter;color:root.overflow?"#EDBA82":"#B7CBD6";font.pixelSize:12
        text:!root.valid?"等待 XY 输入":"X "+Number(root.xy[0]).toFixed(3)+"\nY "+Number(root.xy[1]).toFixed(3)+(root.overflow?"\n超出显示范围":root.state.pressed?"\n按压":root.state.active?"\n激活":"\n未激活")
    }
}
