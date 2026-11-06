import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CoRig.Native 1.0

Item {
    id:root;required property var host;objectName:"plot.view"
    property bool follow:true
    property bool fft:false
    property var range:({})
    property string exportKind:"png"
    // The data panel width is part of the view appearance, so a placed view
    // reopens with the split the user chose. It is written once when a drag
    // ends rather than while the handle moves.
    property real panelWidth:148
    property bool panelDragging:false
    readonly property bool panelLocked:!!(root.host.designMode||App.readOnly)
    readonly property real maxPanelWidth:Math.max(100,Math.min(1200,width*0.6))
    readonly property real persistedPanelWidth:root.clampPanelWidth(root.host.viewProperties&&root.host.viewProperties.panel_width!==undefined?root.host.viewProperties.panel_width:148)
    function clampPanelWidth(value){const n=Number(value);if(!isFinite(n)||n<=0)return 148;return Math.max(100,Math.min(root.maxPanelWidth,Math.round(n)))}
    function syncPanelWidth(){if(!root.panelDragging)root.panelWidth=root.persistedPanelWidth}
    function applyPanelWidth(value){
        if(root.panelLocked||!root.host.uiItem)return
        const next=root.clampPanelWidth(value)
        root.panelWidth=next
        if(next===root.persistedPanelWidth)return
        const reply=App.invoke("ui.item.patch",{id:root.host.uiItem,properties:{panel_width:next}})||{}
        if(reply.ok===false||reply.error)root.panelWidth=root.persistedPanelWidth
    }
    function finishPanelDrag(){root.panelDragging=false;root.applyPanelWidth(panel.width)}
    // The channel list must survive the view poll, which replaces the whole
    // observation every refresh. Rows are keyed by channel name and show the
    // node's own channel state: the view keeps a click while its patch is in
    // flight and never re-derives why the node hides a channel.
    property var visibilityJobs:({})
    property var colorJobs:({})
    property var jobChannels:({})
    property var visibilityHold:({})
    property var colorHold:({})
    function observe(){if(host.designMode||!host.nodeId)return;let args=Object.assign({width:Math.round(plot.width),follow:follow,fft:fft},range);App.observeView(host.nodeId,host.viewKey,args)}
    function patch(changes){return App.invoke("node.configure",{node:host.nodeId,changes:changes,persist:!App.readOnly})}
    function rowOf(name){for(let i=0;i<channels.count;++i)if(channels.get(i).channel===name)return i;return -1}
    // A visibility patch carries the user's own values only, so replaying a
    // pending click never rewrites channels the node filtered out.
    function visibilityMap(){const map=Object.assign({},root.host.config.visibility||{});for(const name in root.visibilityHold)map[name]=root.visibilityHold[name];return map}
    function colorMap(name,color){const map=Object.assign({},root.host.config.colors||{});for(const other in root.colorHold)map[other]=root.colorHold[other];map[name]=color;return map}
    function track(kind,name,job){const table=kind==="visibility"?root.visibilityJobs:root.colorJobs;table[name]=job;root.jobChannels[job]={channel:name,kind:kind}}
    function sync(){
        const observed=root.host.observation.channels||[],seen={}
        for(let i=0;i<observed.length;++i){
            const item=observed[i],name=item.name;if(!name)continue
            seen[name]=true
            // The node's channel state is the only source, and a held value is
            // released as soon as the node reports that value back, so a poll
            // requested before the patch cannot revert the user's click.
            const tint=item.color===undefined?"":String(item.color)
            if(name in root.visibilityHold&&root.visibilityHold[name]===item.visible)delete root.visibilityHold[name]
            if(name in root.colorHold&&root.colorHold[name]===tint)delete root.colorHold[name]
            let row=root.rowOf(name)
            if(row<0){channels.append({channel:name,ticked:false,tint:"",filtered:false});row=channels.count-1}
            // Writes that change nothing would still wake every delegate for
            // each poll, so the row is only touched when a value differs.
            const ticked=(name in root.visibilityHold)?root.visibilityHold[name]:item.visible===true
            const shown=(name in root.colorHold)?root.colorHold[name]:tint
            const filtered=item.filtered===true
            if(channels.get(row).ticked!==ticked)channels.setProperty(row,"ticked",ticked)
            if(channels.get(row).tint!==shown)channels.setProperty(row,"tint",shown)
            if(channels.get(row).filtered!==filtered)channels.setProperty(row,"filtered",filtered)
        }
        for(let i=channels.count-1;i>=0;--i){
            const name=channels.get(i).channel;if(seen[name])continue
            delete root.visibilityHold[name];delete root.colorHold[name];channels.remove(i)
        }
    }
    function setVisible(name,ticked){
        const row=root.rowOf(name);if(row<0)return
        const previous=channels.get(row).ticked
        channels.setProperty(row,"ticked",ticked);root.visibilityHold[name]=ticked
        let changes={visibility:root.visibilityMap()}
        // The node reports this channel as filtered out, so asking for it on
        // also opens the filter instead of leaving the click without effect.
        if(ticked&&channels.get(row).filtered){const wanted=root.host.config.channels||[];if(wanted.indexOf(name)<0)changes.channels=wanted.concat([name])}
        const reply=root.patch(changes)
        if(reply.job){root.track("visibility",name,reply.job);return}
        // A rejected or merely planned answer never reached the node, so the row
        // falls back instead of waiting for a document that will not change.
        if(reply.ok===false||reply.requires_confirmation===true){delete root.visibilityHold[name];channels.setProperty(row,"ticked",previous);return}
        root.sync()
    }
    function setColor(name,color){
        const row=root.rowOf(name);if(row<0)return
        const previous=channels.get(row).tint
        channels.setProperty(row,"tint",color);root.colorHold[name]=color
        const reply=root.patch({colors:root.colorMap(name,color)})
        if(reply.job){root.track("colors",name,reply.job);return}
        if(reply.ok===false||reply.requires_confirmation===true){delete root.colorHold[name];channels.setProperty(row,"tint",previous);return}
        root.sync()
    }
    // Settles one patch. A stale job never overrides the row a newer patch owns.
    // A failed job drops the hold at once, so the row falls back to the node; a
    // completed one keeps it until the node reports the same value back, which
    // keeps the click visible for the one poll that may still be in flight.
    function settle(id){
        const owner=root.jobChannels[id];if(!owner)return
        delete root.jobChannels[id]
        const name=owner.channel,table=owner.kind==="visibility"?root.visibilityJobs:root.colorJobs
        if(table[name]===id)delete table[name]
        if(table[name]!==undefined)return
        if(App.job(id).state!=="complete"){const holds=owner.kind==="visibility"?root.visibilityHold:root.colorHold;delete holds[name]}
        root.sync()
    }
    function call(action,args){
        if(action==="range"||action==="fft"){
            if(action==="fft")fft=args.enabled===undefined?true:args.enabled
            if(args.follow!==undefined)follow=args.follow
            if(args.fft!==undefined)fft=args.fft
            let next=Object.assign({},range)
            for(const key of ["x_min","x_max","y_min","y_max"])if(args[key]!==undefined)next[key]=args[key]
            if(args.follow===true)next=({})
            range=next;observe();return {follow:follow,fft:fft,range:range}
        }
        if(action==="export_image")return plot.exportImage(args.path,args.width||1600,args.height||900)
        return({ok:false,error:"未知曲线视图操作"})
    }
    ListModel{id:channels}
    onPanelWidthChanged:panel.SplitView.preferredWidth=root.panelWidth
    // A narrower window clamps the panel, and the stored width returns once
    // there is room again.
    onWidthChanged:root.syncPanelWidth()
    Component.onCompleted:{observe();sync();root.syncPanelWidth()}
    onFollowChanged:observe()
    onFftChanged:observe()
    onRangeChanged:observe()
    Connections{target:root.host;function onObservationChanged(){root.sync()}function onConfigChanged(){root.sync()}function onViewPropertiesChanged(){root.syncPanelWidth()}}
    Connections{target:App;function onJobFinished(id){root.settle(id)}}
    ColumnLayout{
        anchors.fill:parent;spacing:0
        RowLayout{
            Layout.fillWidth:true;Layout.preferredHeight:32;spacing:2
            IconButton{symbol:"follow";text:root.follow?"跟随":"自由浏览";onClicked:{root.follow=!root.follow;if(root.follow)root.range=({})}}
            IconButton{symbol:"spectrum";text:root.fft?"时域":"FFT";onClicked:{root.fft=!root.fft;root.range=({})}}
            IconButton{symbol:root.host.observation.paused?"run":"pause";text:root.host.observation.paused?"恢复绘制":"暂停绘制";onClicked:root.host.action("pause_display",{value:!root.host.observation.paused})}
            IconButton{symbol:"fit";text:"复位范围";onClicked:{root.follow=true;root.range=({})}}
            Item{Layout.fillWidth:true}
            IconButton{symbol:"export";text:"导出";onClicked:exportMenu.popup()}
        }
        SplitView{
            id:split
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:0
            // Only an editable project persists a new split. Locking the two
            // bounds instead of disabling the view keeps the channel list
            // scrollable in read-only projects.
            onResizingChanged:{if(resizing)root.panelDragging=true;else if(root.panelDragging)root.finishPanelDrag()}
            Item {
                SplitView.fillWidth:true;SplitView.minimumWidth:120
                NativePlot{id:plot;anchors.fill:parent;observation:root.host.observation;onWidthChanged:root.observe()}
                Repeater{model:6;Label{required property int index;x:2;y:16+(plot.height-56)*index/5-7;width:48;horizontalAlignment:Text.AlignRight;color:Theme.muted;font.pixelSize:10;font.family:"Consolas";text:(Number(root.host.observation.y_max||1)-(Number(root.host.observation.y_max||1)-Number(root.host.observation.y_min||-1))*index/5).toPrecision(3)}}
                Repeater{model:6;Label{required property int index;x:54+(plot.width-72)*index/5-25;y:plot.height-33;width:50;horizontalAlignment:Text.AlignHCenter;color:Theme.muted;font.pixelSize:10;font.family:"Consolas";text:(Number(root.host.observation.x_min||0)+(Number(root.host.observation.x_max||1)-Number(root.host.observation.x_min||0))*index/5).toPrecision(3)}}
                Label{anchors.bottom:parent.bottom;anchors.horizontalCenter:parent.horizontalCenter;color:Theme.muted;font.pixelSize:10;text:root.host.observation.x_label||"时间 / s"}
                MouseArea{
                    id:pointer;anchors.fill:parent;hoverEnabled:true;enabled:!root.host.designMode;acceptedButtons:Qt.LeftButton|Qt.RightButton
                    // Once a plot drag starts, its outer ScrollView must not
                    // steal the gesture and turn a vertical pan into scrolling.
                    preventStealing:true
                    cursorShape:pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
                    property point start
                    property var original:({})
                    onPressed:mouse=>{start=Qt.point(mouse.x,mouse.y);original=Object.assign({},root.host.observation);root.range={x_min:original.x_min,x_max:original.x_max,y_min:original.y_min,y_max:original.y_max};root.follow=false}
                    onPositionChanged:mouse=>{if(pressed){const dx=(mouse.x-start.x)/Math.max(1,width-72)*(original.x_max-original.x_min),dy=(mouse.y-start.y)/Math.max(1,height-56)*(original.y_max-original.y_min);root.range={x_min:original.x_min-dx,x_max:original.x_max-dx,y_min:original.y_min+dy,y_max:original.y_max+dy}}}
                    onWheel:wheel=>{const d=root.host.observation,f=Math.pow(1.0015,-wheel.angleDelta.y),cx=(d.x_min+d.x_max)/2,cy=(d.y_min+d.y_max)/2;root.follow=false;root.range={x_min:cx+(d.x_min-cx)*f,x_max:cx+(d.x_max-cx)*f,y_min:cy+(d.y_min-cy)*f,y_max:cy+(d.y_max-cy)*f};wheel.accepted=true}
                }
                Rectangle{x:pointer.mouseX;y:16;width:1;height:plot.height-56;color:"#648CA7";visible:pointer.containsMouse&&pointer.mouseX>=54&&pointer.mouseX<=plot.width-18}
                Label{visible:pointer.containsMouse;x:Math.min(plot.width-width-8,Math.max(54,pointer.mouseX+8));y:20;color:Theme.text;font.pixelSize:10;font.family:"Consolas";text:{const d=root.host.observation;return"x "+(Number(d.x_min||0)+(pointer.mouseX-54)/Math.max(1,plot.width-72)*(Number(d.x_max||1)-Number(d.x_min||0))).toPrecision(5)+"\ny "+(Number(d.y_max||1)-(pointer.mouseY-16)/Math.max(1,plot.height-56)*(Number(d.y_max||1)-Number(d.y_min||-1))).toPrecision(5)}}
            }
            ScrollView{
                id:panel
                SplitView.preferredWidth:148
                SplitView.minimumWidth:root.panelLocked?root.panelWidth:100
                SplitView.maximumWidth:root.panelLocked?root.panelWidth:root.maxPanelWidth
                clip:true
                Column{
                    width:Math.max(0,panel.width-10)
                    Repeater{
                        model:channels
                        Row{
                            required property string channel
                            required property bool ticked
                            required property string tint
                            // A wrapped name grows the row instead of being cut
                            // off, and every row keeps the old minimum height.
                            width:parent.width;height:Math.max(32,nameLabel.implicitHeight+8);spacing:4
                            CheckBox{
                                id:toggle;width:28;height:Math.min(parent.height,30)
                                anchors.verticalCenter:parent.verticalCenter
                                // The click owns the button until the patch settles;
                                // Binding re-applies a rollback without breaking it.
                                Binding{target:toggle;property:"checked";value:ticked}
                                onClicked:root.setVisible(channel,toggle.checked)
                            }
                            Rectangle{width:9;height:9;radius:2;color:tint;anchors.verticalCenter:parent.verticalCenter;TapHandler{onTapped:{colorPicker.channel=channel;colorPicker.selectedColor=tint;colorPicker.open()}}}
                            Label{
                                id:nameLabel
                                width:Math.max(40,parent.width-45);text:channel;color:Theme.text
                                // A wrapped name must never be cut, so the mode is
                                // stated explicitly instead of relying on the default.
                                elide:Text.ElideNone
                                wrapMode:Text.WrapAtWordBoundaryOrAnywhere
                                anchors.verticalCenter:parent.verticalCenter
                                ToolTip.visible:hover.hovered;ToolTip.text:channel;HoverHandler{id:hover}
                                objectName:"plot.channel.name"
                            }
                        }
                    }
                }
            }
        }
    }
    Menu{id:exportMenu;MenuItem{text:"原始数据 CSV…";onTriggered:{root.exportKind="csv";exportFile.nameFilters=["CSV (*.csv)"];exportFile.defaultSuffix="csv";exportFile.open()}}MenuItem{text:"图片 PNG…";onTriggered:{root.exportKind="png";exportFile.nameFilters=["PNG (*.png)"];exportFile.defaultSuffix="png";exportFile.open()}}MenuItem{text:"矢量图 SVG…";onTriggered:{root.exportKind="svg";exportFile.nameFilters=["SVG (*.svg)"];exportFile.defaultSuffix="svg";exportFile.open()}}MenuSeparator{}MenuItem{text:"清空曲线";onTriggered:root.host.action("clear",{})}}
    FileDialog{id:exportFile;title:"导出曲线";fileMode:FileDialog.SaveFile;onAccepted:{const path=decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//,""));if(root.exportKind==="csv")root.host.action("export_csv",{path:path});else App.invoke("view.call",{node:root.host.nodeId,view:root.host.viewKey,action:"export_image",args:{path:path}})}}
    ColorDialog{id:colorPicker;property string channel:"";title:"选择曲线颜色";onAccepted:root.setColor(channel,selectedColor.toString())}
}
