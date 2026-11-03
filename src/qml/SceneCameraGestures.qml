import QtQuick

// Orbit, pan and zoom are identical for every 3D view; the camera object owns
// all clamping so both views keep the same feel and limits.
MouseArea {
    id:root
    required property var camera
    anchors.fill:parent
    acceptedButtons:Qt.LeftButton|Qt.RightButton|Qt.MiddleButton
    // Keep orbit/pan gestures inside this view when nested in a ScrollView.
    preventStealing:true
    cursorShape:pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
    property point previous
    onPressed:mouse=>previous=Qt.point(mouse.x,mouse.y)
    onPositionChanged:mouse=>{if(!pressed)return;const dx=mouse.x-previous.x,dy=mouse.y-previous.y;if(pressedButtons===Qt.LeftButton)camera.orbit(dx,dy);else camera.pan(dx,dy);previous=Qt.point(mouse.x,mouse.y)}
    onWheel:wheel=>{camera.dolly(Math.pow(1.0015,-wheel.angleDelta.y));wheel.accepted=true}
    onDoubleClicked:camera.resetCamera()
}
