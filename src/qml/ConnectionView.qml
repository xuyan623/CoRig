import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id:root;required property var host
    property var connectionState:host.snapshot.state||({})
    property var ports:[]
    property string portsJob:""
    property bool portsReady:false
    property string portsError:""
    property string loopbackPort:""
    property bool pendingConnect:false
    property string runtimeJob:""
    property string configureJob:""
    property string targetPort:""
    property string actionJob:""
    property string connectError:""
    readonly property bool serial:root.host.design.plugin==="io.serial"
    readonly property bool connecting:root.pendingConnect||root.runtimeJob.length>0||root.configureJob.length>0||root.actionJob.length>0
    readonly property string savedPort:String(root.host.config.port||"").trim()
    readonly property string portWarning:root.serial&&root.portsReady&&!root.connectionState.connected&&root.savedPort.length>0&&!root.portKnown(root.savedPort)?"已保存的端口 "+root.savedPort+" 当前不可用，请刷新后重新选择":""
    readonly property string feedback:root.connectError.length>0?root.connectError:(root.serial?(root.portsError.length>0?root.portsError:root.portWarning):"")
    // io.ports advertises the software loopback as a capability entry. Keep it
    // usable in empty enumerations without matching a transport name here.
    function portKnown(port){return root.ports.indexOf(port)>=0||(root.loopbackPort.length>0&&port===root.loopbackPort)}
    function configure(key,value){let changes={};changes[key]=value;return App.invoke("node.configure",{node:host.nodeId,changes:changes,persist:!App.readOnly})}
    // Port enumeration and runtime startup are asynchronous jobs. Track their
    // ids so only the latest result is applied and a rapid second click cannot
    // start a second runtime session or a concurrent connect.
    function portsFailed(message){
        root.portsError=message;root.portsReady=false
        if(root.pendingConnect){root.pendingConnect=false;root.connectError=message}
    }
    function refreshPorts(){
        if(root.portsJob.length)return
        root.portsError=""
        const reply=App.invoke("io.ports",{})
        if(reply.error){root.portsFailed(reply.error);return}
        root.portsJob=reply.job||""
        if(!root.portsJob)root.portsFailed("串口列表获取失败")
    }
    // A serial port must exist in the latest enumeration before it is opened.
    // A saved port that disappeared stays visible as a warning instead of
    // being silently reused.
    function connectTarget(){
        if(root.connecting)return
        if(root.connectionState.connected){root.disconnectTarget();return}
        if(App.busy){root.connectError="工程操作尚未完成，请稍候重试";return}
        if(root.serial){
            const port=String(portCombo.editText||"").trim()
            if(!port){root.connectError="请选择或输入串口端口";return}
            if(root.portsJob.length||!root.portsReady){root.pendingConnect=true;root.connectError="";root.refreshPorts();return}
            if(!root.portKnown(port)){root.connectError="串口端口不存在或已失效："+port+"；请刷新端口列表后重试";return}
            root.targetPort=port
        }
        root.connectError=""
        root.startAndConnect()
    }
    // The port is applied only after a runtime session exists: io.serial marks
    // it runtime-editable, so this works in read-only projects too, and the
    // connect action never runs against a session that is still starting.
    function startAndConnect(){
        if(App.runtime.active){root.applyPort();return}
        if(App.executionPending){root.connectError="运行会话正在启动，请稍候";return}
        const reply=App.invoke("runtime.start",{})
        if(reply.error){root.connectError=reply.error;return}
        root.runtimeJob=reply.job||""
        if(!root.runtimeJob)root.connectError="运行会话启动失败"
    }
    function applyPort(){
        if(root.serial&&root.targetPort.length&&root.targetPort!==root.savedPort){
            const reply=root.configure("port",root.targetPort)
            if(reply&&reply.error){root.connectError=reply.error;return}
            if(reply&&reply.requires_confirmation){root.connectError="修改串口端口会影响现有接线，请先在属性面板确认";return}
            if(reply&&reply.job){root.connectError="";root.configureJob=reply.job;return}
        }
        root.openDevice()
    }
    function openDevice(){
        const reply=root.host.action("connect",{})
        if(reply&&reply.error){root.connectError=reply.error;return}
        root.actionJob=reply&&reply.job||""
        if(!root.actionJob)root.connectError="连接请求未被接受"
    }
    function disconnectTarget(){
        root.connectError=""
        const reply=root.host.action("disconnect",{})
        if(reply&&reply.error){root.connectError=reply.error;return}
        root.actionJob=reply&&reply.job||""
        if(!root.actionJob)root.connectError="断开请求未被接受"
    }
    function handleJob(id){
        if(id===root.portsJob){
            const job=App.job(id);root.portsJob=""
            if(job.state!=="complete"){root.portsFailed(job.error||"串口列表获取失败");return}
            const entries=job.result.ports||[]
            const loopback=entries.find(entry=>entry&&entry.loopback===true)
            // Replacing an editable ComboBox model moves currentIndex and
            // rewrites editText, so preserve the text the user is looking at.
            const shown=String(portCombo.editText||"")
            root.loopbackPort=loopback?String(loopback.port||""):""
            root.ports=entries.filter(entry=>!(entry&&entry.loopback===true)).map(entry=>typeof entry==="string"?entry:String(entry.port||"")).filter(name=>name.length>0)
            portCombo.currentIndex=root.ports.indexOf(shown)
            portCombo.editText=shown.length?shown:root.savedPort
            root.portsReady=true;root.portsError=""
            if(root.pendingConnect){root.pendingConnect=false;root.connectTarget()}
            return
        }
        if(id===root.runtimeJob){
            const job=App.job(id);root.runtimeJob=""
            if(job.state!=="complete"){root.connectError=job.error||"运行会话启动失败";return}
            if(!App.runtime.active){root.connectError="运行会话尚未就绪，无法连接";return}
            root.applyPort();return
        }
        if(id===root.configureJob){
            const job=App.job(id);root.configureJob=""
            if(job.state!=="complete"){root.connectError=job.error||"串口端口保存失败";return}
            root.connectError="";root.openDevice();return
        }
        if(id===root.actionJob){
            const job=App.job(id);root.actionJob=""
            if(job.state!=="complete")root.connectError=job.error||"连接操作失败"
        }
    }
    // External config changes (undo, project switch) must reach the editor when
    // the user is not actively typing; model refreshes bypass the editText binding.
    onSavedPortChanged:if(root.serial&&portCombo&&!portCombo.activeFocus&&!root.connecting)portCombo.editText=root.savedPort
    Component.onCompleted:if(root.serial)root.refreshPorts()
    Connections{target:App;function onJobFinished(id){root.handleJob(id)}}
    ColumnLayout{
        anchors.fill:parent;spacing:8
        RowLayout{
            Rectangle{width:8;height:8;radius:4;color:root.connectionState.connected?Theme.green:Theme.muted}
            Label{Layout.fillWidth:true;text:root.connectionState.connected?"已连接":"未连接";color:Theme.text}
            Label{text:"收 "+(root.connectionState.rx_bytes||0)+"　发 "+(root.connectionState.tx_bytes||0);color:Theme.muted;font.pixelSize:11}
            IconButton{symbol:"sliders";text:"配置";visible:App.appMode!=="publish";onClicked:{App.workspaces.open("graph");App.invoke("graph.select",{id:root.host.nodeId})}}
        }
        RowLayout{
            Layout.fillWidth:true
            ComboBox{
                id:portCombo;visible:root.serial;objectName:"connection."+root.host.nodeId+".port";Layout.fillWidth:true;editable:true;model:root.ports;editText:root.host.config.port||"";enabled:!root.connectionState.connected&&!root.host.designMode&&!root.connecting
                onAccepted:{root.connectError="";root.configure("port",String(editText).trim())}
                onActivated:{root.connectError="";root.configure("port",String(currentText).trim())}
                onDownChanged:if(down)root.refreshPorts()
            }
            IconButton{visible:root.serial;symbol:"refresh";objectName:"connection."+root.host.nodeId+".ports_refresh";text:root.portsJob.length?"正在刷新端口列表":"刷新端口列表";enabled:root.serial&&!root.portsJob&&!root.connecting;ink:root.portsJob.length?Theme.muted:Theme.text;onClicked:root.refreshPorts()}
            TextField{visible:root.host.design.plugin!=="io.serial";Layout.fillWidth:true;text:root.host.config.host||"";placeholderText:"主机";enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:if(text!==root.host.config.host)root.configure("host",text)}
            TextField{Layout.preferredWidth:100;text:String(root.host.design.plugin==="io.serial"?root.host.config.baudrate:root.host.config.port);placeholderText:root.host.design.plugin==="io.serial"?"波特率":"端口";enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:root.configure(root.host.design.plugin==="io.serial"?"baudrate":"port",Number(text))}
            ActionButton{objectName:"connection."+root.host.nodeId+".connect";text:root.connectionState.connected?"断开":root.connecting?"连接中…":"连接";enabled:!root.host.designMode&&!App.busy&&!root.connecting;onClicked:root.connectTarget()}
        }
        Label{objectName:"connection."+root.host.nodeId+".feedback";Layout.fillWidth:true;visible:!!root.feedback;text:root.feedback;color:Theme.error;wrapMode:Text.WrapAnywhere}
        RowLayout{
            visible:root.host.design.plugin==="io.network"
            ComboBox{model:["tcp","udp"];currentIndex:model.indexOf(root.host.config.protocol);enabled:!root.connectionState.connected&&!root.host.designMode;onActivated:root.configure("protocol",currentText)}
            ComboBox{model:["client","server"];currentIndex:model.indexOf(root.host.config.mode);displayText:currentIndex?"服务端":"客户端";enabled:!root.connectionState.connected&&!root.host.designMode;onActivated:root.configure("mode",currentValue)}
            Label{Layout.fillWidth:true;text:root.connectionState.clients&&root.connectionState.clients.length?root.connectionState.clients.length+" 个客户端":"";color:Theme.muted}
        }
        RowLayout{
            visible:root.host.design.plugin==="io.ssh"
            TextField{Layout.fillWidth:true;text:root.host.config.user||"";placeholderText:"用户";enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:root.configure("user",text)}
            TextField{Layout.fillWidth:true;text:root.host.config.password||"";placeholderText:"密码";echoMode:TextInput.Password;enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:root.configure("password",text)}
        }
        Label{Layout.fillWidth:true;visible:!!root.connectionState.last_error;text:root.connectionState.last_error||"";color:Theme.error;wrapMode:Text.WrapAnywhere}
        Label{Layout.fillWidth:true;visible:!!root.connectionState.host_fingerprint;text:root.connectionState.host_fingerprint||"";color:Theme.muted;font.pixelSize:10;elide:Text.ElideMiddle}
        Item{Layout.fillHeight:true}
    }
}