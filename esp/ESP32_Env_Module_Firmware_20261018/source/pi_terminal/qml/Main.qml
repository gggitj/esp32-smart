import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import QtWebEngine
import Terminal.Native 1.0

ApplicationWindow {
    id: win
    visible: true; width: 1280; height: 800
    minimumWidth:320;minimumHeight:320
    title: "模块化终端"; color: Theme.background
    property real textScale: systemTools.interfaceScale
    // Fit the complete desktop layout to the available logical screen size.
    // The previous lower clamp of 1 prevented any down-scaling, so compact
    // panels such as the Pi's 1024x600 display clipped the content while the
    // UI still reported that it was adapting automatically.
    readonly property real autoScale: Math.max(0.65, Math.min(1.4, Math.min(width / 1280, height / 800)))
    readonly property real uiScale: autoScale * textScale
    readonly property bool narrow: width < 760 * uiScale
    readonly property bool shortScreen: height < 760 * uiScale
    readonly property real edge: width < 600 ? Theme.gap : Theme.margin
    font.pointSize: Theme.body
    Material.theme: Material.Dark; Material.accent: Theme.accent; Material.foreground: Theme.foreground
    Binding { target: Theme; property: "scaleFactor"; value: win.uiScale }
    property int section: 0
    property string phase:"home"
    property real openProgress:0
    property real originX:width/2
    property real originY:height/2
    property string appTitle:""
    property string appIcon:"apps"
    property string appEntryId:""
    property bool appMinimized:false
    property bool appMaximized:false
    readonly property bool appSurfaceVisible:appShell.visible
    readonly property real appSurfaceWidth:appShell.width
    property var openTaskIds: []
    ListModel {id:openTasks}
    property var pendingLaunch:null
    property var releaseQueue:[]
    property int homeRevision:0
    property int unreadNotifications:0
    property string activeSession: ""
    property string moduleMessage: ""
    property var activeActions: []
    property var trustQueue: []
    property var trustItem: ({instance:"", title:"", permissions:[]})
    property string folder: ""
    function textBackground(label) {
        // Read geometry and source bindings so resize, carousel movement and a
        // newly selected wallpaper recalculate the local contrast.
        const revision=systemTools.wallpaper
        const scroll=launcher.contentX
        const point=label.mapToItem(homeWallpaper,label.width/2,label.height/2)
        let base=systemTools.wallpaperColor(point.x,point.y,homeWallpaper.width,homeWallpaper.height)
        let onHome=false,card=false,localBackground=null
        for(let node=label;node;node=node.parent){
            if(localBackground===null){
                if(node.background && node.background.color!==undefined && node.background.color.a>0)localBackground=node.background.color
                else if(node!==label && node.color!==undefined && node.color.a>0)localBackground=node.color
            }
            if(node===homePane)onHome=true
            if(node.objectName==="appTile")card=true
        }
        if(onHome)return localBackground!==null?localBackground:card?Theme.blend(Theme.surface,base,systemTools.launcherOpacity):base
        for(let node=label;node;node=node.parent){
            if(node===appShell)return Theme.blend(Theme.background,base,systemTools.moduleOpacity)
            if(node.background && node.background.color!==undefined && node.background.color.a>0)return node.background.color
            if(node!==label && node.color!==undefined && node.color.a>0)return node.color
        }
        return Theme.background
    }
    property var fileRows: []
    property string filePath: ""
    property string fileFilter: ""
    property bool fileGridView: true
    property string fileSelection: ""
    property bool filesRecent:false
    property bool fileSearchVisible:false
    property string fileOperation:""
    property string fileOperationError:""
    readonly property var filteredFileRows:fileRows.filter(function(row){return !fileFilter || row.name.toLowerCase().indexOf(fileFilter.toLowerCase())>=0})
    property var fileHistory: [""]
    property int fileHistoryIndex: 0
    property bool fileEditable: true
    property var marks: []
    property string clockText: Qt.formatTime(new Date(), "hh:mm")
    property bool browserTestRunning: false
    property bool browserTestPassed: false
    property bool browserTouchLinkPassed:false
    property string browserStatus: ""
    property string noteFile: "pi-terminal-note.txt"
    property string noteText: ""
    property string calcDisplay: "0"
    property real calcStored: 0
    property string calcOperator: ""
    property bool calcFresh: true
    property string paintColor: "#13283d"
    property real paintSize: 6
    property bool paintEraser: false
    property var paintStrokes: []
    property var paintRedo: []
    property bool exitPending:false
    readonly property bool permissionPending:trustDialog.visible
    property var activeBrowser: browserRepeater.itemAt(browserTabs.currentIndex)
    readonly property string defaultBrowserUrl: "https://baidu.com"
    Timer { interval: 1000; running: true; repeat: true; onTriggered: clockText = Qt.formatTime(new Date(), "hh:mm") }
    function builtin(page) {
        return ({1:{entryId:"connections",title:"连接管理",iconName:"lan",accentColor:"#c9baff",route:1,instance:"",ready:true},
                 2:{entryId:"browser",title:"浏览器",iconName:"language",accentColor:"#a5e6c4",route:2,instance:"",ready:true},
                 3:{entryId:"files",title:"文件",iconName:"folder",accentColor:"#ffdaa0",route:3,instance:"",ready:true},
                 4:{entryId:"settings",title:"设置",iconName:"settings",accentColor:"#85deff",route:4,instance:"",ready:true},
                 6:{entryId:"calculator",title:"计算器",iconName:"apps",accentColor:"#ffcf8a",route:6,instance:"",ready:true},
                 7:{entryId:"paint",title:"画图",iconName:"image",accentColor:"#ff9fb5",route:7,instance:"",ready:true},
                 8:{entryId:"notes",title:"文本便笺",iconName:"text_snippet",accentColor:"#9bd8ff",route:8,instance:"",ready:true}})[page]
    }
    function rememberApp(entry) {
        for(let i=recentApps.count-1;i>=0;--i)if(recentApps.get(i).entryId===entry.entryId)recentApps.remove(i)
        recentApps.insert(0,{entryId:entry.entryId,title:entry.title,iconName:entry.iconName,accentColor:entry.accentColor,route:entry.route,instance:entry.instance})
        while(recentApps.count>3)recentApps.remove(recentApps.count-1)
    }
    function removeRecent(entryId) {
        for(let i=recentApps.count-1;i>=0;--i) if(recentApps.get(i).entryId===entryId) recentApps.remove(i)
    }
    function clearForegroundModulePage() {
        if (activeSession !== "") {
            closeModulePopups()
            moduleLoader.source=""; activeSession=""; activeActions=[]; moduleMessage=""
        }
    }
    function closeModulePopups() {
        const seen=[]
        function visit(object) {
            if (!object || seen.indexOf(object)>=0) return
            seen.push(object)
            for(let name of ["children","data","resources"]) {
                const list=object[name]
                if (list && typeof list.length==="number")
                    for(let n=0;n<list.length;++n) visit(list[n])
            }
            if (object!==moduleLoader.item && object.opened!==undefined && typeof object.close==="function") object.close()
        }
        visit(moduleLoader.item)
    }
    function announce(text) { if(systemTools.speechEnabled) systemTools.speak(text) }
    function refreshFiles() {
        if(filesRecent){fileRows=systemTools.recentFiles();return}
        fileRows=systemTools.files(folder).sort(function(a,b) {
            if (a.directory!==b.directory) return a.directory ? -1 : 1
            return a.name.localeCompare(b.name)
        })
    }
    function navigateFiles(next,record) {
        next=next || ""
        if (next===folder && !filesRecent) { refreshFiles(); return }
        folder=next; filesRecent=false; fileSelection=""; fileFilter=""; refreshFiles()
        if (record!==false) { fileHistory=fileHistory.slice(0,fileHistoryIndex+1); fileHistory.push(folder); fileHistoryIndex=fileHistory.length-1 }
    }
    function fileParent() {
        if (!folder) return ""
        return folder.substring(0,Math.max(0,folder.lastIndexOf('/')))
    }
    function openFileRow(row) {
        fileSelection=row.relative || row.name
        let p=row.relative || ((folder?folder+"/":"")+row.name)
        if(row.directory) { navigateFiles(p) }
        else { systemTools.rememberFile(p);filePath=p; fileEditable=systemTools.editable(p); viewer.text=systemTools.read(p); fileDialog.open() }
    }
    function requestFileOperation(operation) {
        fileOperation=operation;fileOperationError=""
        const selected=fileRows.find(function(row){return (row.relative || row.name)===fileSelection})
        operationName.text=operation==="rename" && selected?selected.name:""
        fileOperationDialog.open()
    }
    function completeFileOperation() {
        const name=operationName.text.trim()
        if(fileOperation!=="delete" && (!name || name==="." || name===".." || name.indexOf("/")>=0)) {fileOperationError="请输入有效名称";return}
        const selected=fileRows.find(function(row){return (row.relative || row.name)===fileSelection})
        if((fileOperation==="rename" || fileOperation==="delete") && !selected){fileOperationError="请先选择文件或文件夹";return}
        const source=selected && selected.relative ? selected.relative : ((folder?folder+"/":"")+fileSelection)
        const target=(folder?folder+"/":"")+name
        let ok=false
        if(fileOperation==="newFile")ok=systemTools.createFile(target,"")
        else if(fileOperation==="newFolder")ok=systemTools.makeDirectory(target)
        else if(fileOperation==="rename")ok=systemTools.rename(source,target)
        else if(fileOperation==="delete")ok=systemTools.remove(source)
        if(!ok){fileOperationError="操作失败：请检查名称、权限或目录是否为空";return}
        fileSelection="";refreshFiles();fileOperationDialog.close()
    }
    function fileIcon(row) {
        if (row.directory) return "folder"
        const name=row.name.toLowerCase()
        if (/\.(png|jpe?g|gif|bmp|webp|svg)$/.test(name)) return "image"
        if (/\.(c|cc|cpp|h|hpp|qml|js|py|sh|rs|java|json|xml|html|css)$/.test(name)) return "code"
        if (/\.(zip|tar|gz|bz2|xz|7z|rar|zst)$/.test(name)) return "archive"
        if (/\.(txt|md|csv|log|ini|conf|yaml|yml)$/.test(name)) return "text_snippet"
        return "description"
    }
    function loadNote() {
        const text=systemTools.read(noteFile)
        noteText=text==="无法读取"?"":text
    }
    function saveNote() {
        if(systemTools.write(noteFile,noteText)) announce("便笺已保存")
    }
    function paintUndo() {
        if(!paintStrokes.length)return
        paintRedo=paintRedo.concat([paintStrokes[paintStrokes.length-1]])
        paintStrokes=paintStrokes.slice(0,paintStrokes.length-1);paintCanvas.redraw()
    }
    function paintRedoStroke() {
        if(!paintRedo.length)return
        paintStrokes=paintStrokes.concat([paintRedo[paintRedo.length-1]])
        paintRedo=paintRedo.slice(0,paintRedo.length-1);paintCanvas.redraw()
    }
    function paintClear() {paintStrokes=[];paintRedo=[];paintCanvas.redraw()}
    function calcReset() {calcDisplay="0";calcStored=0;calcOperator="";calcFresh=true}
    function calcInput(value) {
        if(value==="C"){calcReset();return}
        if(value===".") {if(calcFresh){calcDisplay="0.";calcFresh=false}else if(calcDisplay.indexOf(".")<0)calcDisplay+=".";return}
        if(value>="0" && value<="9") {if(calcFresh||calcDisplay==="0"){calcDisplay=value;calcFresh=false}else calcDisplay+=value;return}
        if(value==="=") {calcEquals();return}
        if(value==="+"||value==="-"||value==="×"||value==="÷") {calcStored=parseFloat(calcDisplay)||0;calcOperator=value;calcFresh=true}
    }
    function calcEquals() {
        if(!calcOperator)return
        const rhs=parseFloat(calcDisplay)||0
        let result=calcStored
        if(calcOperator==="+")result+=rhs
        else if(calcOperator==="-")result-=rhs
        else if(calcOperator==="×")result*=rhs
        else if(calcOperator==="÷")result=rhs===0?0:result/rhs
        calcDisplay=String(Math.round(result*100000000)/100000000);calcOperator="";calcFresh=true
    }
    function taskRow(entryId) {
        for(let i=0;i<openTasks.count;++i) if(openTasks.get(i).entryId===entryId) return openTasks.get(i)
        return null
    }
    function updateTaskState() {
        for(let i=0;i<openTasks.count;++i) if(openTasks.get(i).entryId===appEntryId) {
            openTasks.setProperty(i,"minimized",appMinimized)
            openTasks.setProperty(i,"maximized",appMaximized)
            return
        }
    }
    function registerTask(entry) {
        let previous=taskRow(entry.entryId)
        let row={entryId:entry.entryId,title:entry.title,iconName:entry.iconName||"extension",accentColor:entry.accentColor||Theme.accent,route:entry.route,instance:entry.instance||"",ready:true,minimized:previous?previous.minimized:false,maximized:previous?previous.maximized:false}
        for(let i=0;i<openTasks.count;++i)if(openTasks.get(i).entryId===entry.entryId){openTasks.set(i,row);return}
        openTasks.append(row)
    }
    function launch(entry,x,y) {
        if(!entry)return false
        if (appMinimized && appEntryId===entry.entryId) {
            restoreApp();return true
        }
        if(phase!=="home") {
            if(appEntryId===entry.entryId){appMinimized=false;phase="open";updateTaskState();return true}
            pendingLaunch={entry:entry,x:x,y:y};switchTask(entry);return true
        }
        if(!entry.instance) clearForegroundModulePage()
        if(entry.instance) {
            let session=null
            for(let card of connectionManager.cards)if(card.instance===entry.instance)session=card
            if(!session || !session.ready){showDevicePanel(false);return false}
            activeSession=entry.instance;activeActions=session.actions;moduleMessage=session.message
            moduleLoader.setSource(connectionManager.pageProvider.page(entry.instance),{sessionModel:connectionManager.pageProvider.model(entry.instance)})
        }
        section=entry.route;appTitle=entry.title;appIcon=entry.iconName;appEntryId=entry.entryId
        if(section===3)refreshFiles()
        if(section===8)loadNote()
        if(section===6)calcReset()
        registerTask(entry)
        let saved=taskRow(entry.entryId);appMinimized=false;appMaximized=saved?saved.maximized:false
        updateTaskState()
        originX=Math.max(0,Math.min(width,x===undefined?width/2:x));originY=Math.max(0,Math.min(contentItem.height,y===undefined?contentItem.height/2:y))
        rememberApp(entry);openProgress=0;phase="opening";zoomAnimation.to=1;zoomAnimation.duration=260;zoomAnimation.easing.type=Easing.OutCubic;zoomAnimation.restart()
        if(openTaskIds.indexOf(entry.entryId)<0)openTaskIds.push(entry.entryId)
        return true
    }
    function go(page) {if(page===0)closeApp();else launch(builtin(page),width/2,contentItem.height/2)}
    function closeApp() {
        if(phase==="home" || phase==="closing")return
        zoomAnimation.stop();phase="closing";zoomAnimation.to=0;zoomAnimation.duration=200;zoomAnimation.easing.type=Easing.InCubic;zoomAnimation.restart()
    }
    function minimizeApp() {
        if(phase!=="open") return
        closeModulePopups()
        appMinimized=true;phase="home";moduleLoader.source="";activeSession="";activeActions=[];moduleMessage="";updateTaskState();announce("最小化应用")
        let releases=releaseQueue;releaseQueue=[];Qt.callLater(function(){for(let instance of releases)connectionManager.releasePage(instance)})
    }
    function restoreApp() { appMinimized=false;phase="open";updateTaskState() }
    function toggleMaximize() { if(phase!=="open") return;appMaximized=!appMaximized;updateTaskState() }
    function switchTask(entry) {
        const existing=taskRow(entry.entryId)
        if (existing && appEntryId===entry.entryId) {
            if (appMinimized || phase!=="open") { existing.minimized=false; restoreApp() }
            else { minimizeApp() }
            return
        }
        for(let i=0;i<openTasks.count;++i)if(openTasks.get(i).entryId===entry.entryId){
            // Keep task records alive while changing the foreground view; do not
            // send the current module through the close/unload path.
            pendingLaunch=null;clearForegroundModulePage();announce("切换到"+entry.title);phase="home";
            openTasks.setProperty(i,"minimized",false)
            Qt.callLater(function(){launch(entry,width/2,contentItem.height/2)});return
        }
        if(openTaskIds.indexOf(entry.entryId)<0)openTaskIds.push(entry.entryId)
        pendingLaunch={entry:entry,x:width/2,y:contentItem.height/2};
        clearForegroundModulePage();phase="home";Qt.callLater(function(){let next=pendingLaunch;pendingLaunch=null;launch(next.entry,next.x,next.y)})
    }
    function arrangeWindows() { }
    function exitApplication() {
        if(phase==="home"){ Qt.quit(); return }
        exitPending=true; announce("正在退出"); closeApp()
    }
    function finishTransition() {
        if(phase==="opening"){phase="open";return}
        if(phase!=="closing")return
        moduleLoader.source="";activeSession="";activeActions=[];moduleMessage="";section=0;phase="home";appMinimized=false;appMaximized=false
        const closingId=appEntryId;openTaskIds=openTaskIds.filter(function(id){return id!==closingId})
        for(let i=openTasks.count-1;i>=0;--i)if(openTasks.get(i).entryId===closingId)openTasks.remove(i)
        removeRecent(appEntryId);appEntryId=""
        let releases=releaseQueue;releaseQueue=[]
        Qt.callLater(function(){for(let instance of releases)connectionManager.releasePage(instance)})
        if(pendingLaunch){let next=pendingLaunch;pendingLaunch=null;Qt.callLater(function(){launch(next.entry,next.x,next.y)})}
        if(exitPending) Qt.quit()
    }
    function showDevicePanel(notifications) {systemTools.refreshStatus();devicePanel.jumpToNotifications=notifications;devicePanel.open();if(notifications)unreadNotifications=0}
    NumberAnimation {id:zoomAnimation;target:win;property:"openProgress";onFinished:win.finishTransition()}
    Shortcut {sequence:"Escape";enabled:!trustDialog.opened && !fileDialog.opened && !fileOperationDialog.opened && !bookmarkDialog.opened && !devicePanel.opened;onActivated:closeApp()}
    function stateText(state) {
        return ({attached:"检测到模块", connecting:"正在连接", downloading_package:"下载功能包", verifying_package:"验证签名", awaiting_trust:"等待权限确认", loading_plugin:"加载界面", ready:"已连接", contact_detached:"无线连接中", data_lost:"正在重连", error:"连接失败", disconnecting:"正在断开", unloading:"正在卸载", closed:"已断开"})[state] || state
    }
    function syncCards() {
        const entries=[]
        for(let card of connectionManager.cards) entries.push({entryId:card.instance,title:card.title,
            description:stateText(card.state),category:"模块",accentColor:"#85deff",iconName:card.icon||"extension",route:5,instance:card.instance,ready:card.ready})
        for(let route of [2,3,1,4,6,7,8]) {let app=builtin(route);app.description="";app.category="应用";entries.push(app)}
        for (let i=homeCards.count-1; i>=0; --i)
            if (!entries.some(function(e) { return e.entryId === homeCards.get(i).entryId })) homeCards.remove(i)
        for (let desired=0;desired<entries.length;++desired) {
            let entry=entries[desired]
            let found = -1
            for (let i=0; i<homeCards.count; ++i) if (homeCards.get(i).entryId === entry.entryId) { found=i; break }
            if(found<0)homeCards.insert(desired,entry)
            else {if(found!==desired)homeCards.move(found,desired,1);homeCards.set(desired,entry)}
        }
        for(let i=recentApps.count-1;i>=0;--i)if(recentApps.get(i).instance && !entries.some(function(e){return e.entryId===recentApps.get(i).entryId}))recentApps.remove(i)
        homeRevision++
    }
    function showNextTrust() {
        if (!trustDialog.visible && trustQueue.length) { trustItem=trustQueue.shift(); trustDialog.open() }
    }
    function approveTrustOnce() {
        if (!trustDialog.opened) return
        connectionManager.trust(trustItem.instance,false); trustDialog.close()
    }
    function openModule(instance) {
        for (let row of connectionManager.cards) if (row.instance === instance && row.ready) {
            return launch({entryId:instance,title:row.title,iconName:row.icon||"extension",accentColor:"#85deff",route:5,instance:instance,ready:true},width/2,contentItem.height/2)
        }
        showDevicePanel(false);return false
    }
    function navigateBrowser(text) {
        let address=text.trim()
        if (address === "") return
        if(/^\/\//.test(address)) address="https:"+address
        else if(!/^(https?|about|file):/i.test(address))address="https://"+address
        if(browserTabs.currentIndex>=0 && browserTabs.currentIndex<tabs.count)tabs.setProperty(browserTabs.currentIndex,"address",address)
        location.text=address
        if(activeBrowser){browserStatus="正在加载";activeBrowser.forceActiveFocus();activeBrowser.url=address}
    }
    function beginBrowserTest() {
        go(2); tabs.append({address:"about:blank"}); browserTabs.currentIndex=tabs.count-1
        Qt.callLater(function() { browserTestRunning=true; activeBrowser.zoomFactor=1.6
            activeBrowser.loadHtml("<html><head><title>Local terminal test</title></head><body><h1>Browser ready</h1></body></html>") })
    }
    function auditBrowserTouchPage() {
        tabs.append({address:"about:blank"});browserTabs.currentIndex=tabs.count-1
        Qt.callLater(function(){activeBrowser.loadHtml("<html><body style='font-size:32px'><a id='go' target='_blank' href='#touch-link'>TOUCH LINK</a><div style='height:1800px'>TOUCH SCROLL TARGET</div><div id='touch-link'>NAVIGATED</div></body></html>")})
    }
    function auditBrowserClickLink() { if(activeBrowser) activeBrowser.runJavaScript("document.getElementById('go').click()") }
    function auditBrowserTabAddress() {
        if (tabs.count<2) tabs.append({address:"about:blank"})
        tabs.setProperty(0,"address","https://first.example")
        tabs.setProperty(1,"address","https://second.example")
        browserTabs.currentIndex=0; browserTabs.currentIndex=1
        return location.text==="https://second.example"
    }
    function auditOpenWindow(route) {go(route);zoomAnimation.complete()}
    function auditTaskCount(){return openTasks.count}
    function auditMinimizeMain(){if(phase==="opening")zoomAnimation.complete();minimizeApp()}
    function auditRestoreMain(){appMinimized=false;phase="open";updateTaskState()}
    function auditTaskbarRestore(){
        const row=taskRow(appEntryId)
        if(row) switchTask({entryId:row.entryId,title:row.title,iconName:row.iconName,accentColor:row.accentColor,route:row.route,instance:row.instance,ready:true})
    }
    function auditHomeRequest(){if(phase==="open")minimizeApp()}
    function auditHomeReopen(){const row=taskRow(appEntryId);if(row)launch(row,width/2,contentItem.height/2)}
    function auditToggleAppMaximize(){appMaximized=!appMaximized;updateTaskState()}
    function auditSnapshot() {
        return JSON.stringify({width:width,height:height,scale:uiScale,body:Theme.body,title:Theme.title,gap:Theme.gap,hit:Theme.hit,
            section:section,phase:phase,openProgress:openProgress,carouselX:launcher.contentX,cardCount:homeCards.count,
            homeContentWidth:homePane.width,homeViewportWidth:homePane.width})
    }
    function auditPage(page) {pendingLaunch=null;if(phase!=="home"){closeApp();zoomAnimation.complete()}if(page>0){go(page);zoomAnimation.complete()}}
    function auditScale(value) {textScale=value}
    function auditDialog(kind) {
        if (kind === "trust") { trustItem={instance:"audit-only",title:"测试模块",permissions:["transport.ble","transport.hotspot_lan"]}; trustDialog.open() }
        else if (kind === "file") { filePath="audit.txt"; viewer.text="响应式文件编辑器"; fileDialog.open() }
        else if(kind==="file-operation")requestFileOperation("newFile")
        else if(kind==="opacity")opacityDialog.open()
        else if (kind === "bookmarks") { marks=["https://example.invalid/书签示例"];bookmarkDialog.open() }
        else if(kind==="status")showDevicePanel(false)
        else { trustDialog.close(); fileDialog.close();fileOperationDialog.close();bookmarkDialog.close();devicePanel.close();opacityDialog.close() }
    }
    ListModel { id: homeCards }
    ListModel {id:recentApps}
    ListModel {id:notices}
    Connections {
        target: connectionManager
        function onCardsChanged() {
            syncCards()
            for (let row of connectionManager.cards) if (row.instance===activeSession) moduleMessage=row.message
        }
        function onTrustRequired(instance,title,permissions) { trustQueue.push({instance:instance,title:title,permissions:permissions}); showNextTrust() }
        function onPageMustClose(instance) {
            if(activeSession===instance){
                if(releaseQueue.indexOf(instance)<0)releaseQueue.push(instance)
                if(phase==="home") { releaseQueue=releaseQueue.filter(function(id){return id!==instance}); Qt.callLater(function(){connectionManager.releasePage(instance)}) }
                else closeApp()
            }
            else Qt.callLater(function(){connectionManager.releasePage(instance)})
            trustQueue=trustQueue.filter(function(item) { return item.instance!==instance })
            if (trustItem.instance===instance) trustDialog.close()
        }
        function onDeviceAttached(instance,moduleId) {if(phase==="home")Qt.callLater(function(){launcher.reveal(instance)})}
    }
    Connections { target: systemTools; function onChanged() { marks=systemTools.bookmarks() }
        function onNotification(message){notices.insert(0,{message:message,time:Qt.formatTime(new Date(),"hh:mm")});while(notices.count>100)notices.remove(notices.count-1);if(!devicePanel.opened)unreadNotifications++} }

    // Capture only the background, excluding UI and blur outputs (no feedback).
    Item {
        id:desktopBackground;objectName:"desktopBackground";anchors.fill:parent;layer.enabled:true
    Rectangle {
        id: sceneSource; anchors.fill: parent; layer.enabled: true; color: "#071827"
        Rectangle { width: parent.width*0.65; height: parent.height*0.7; x:-width*0.12; y:parent.height*0.13
            radius: Math.min(width,height)*0.5; rotation:-22; color:"#14546c" }
        Rectangle { width: parent.width*0.5; height: parent.height*0.65; x:parent.width*0.64; y:-height*0.1
            radius: Math.min(width,height)*0.5; color:"#413764" }
        Repeater { model: 6; Rectangle {
            required property int index
            x:sceneSource.width*(0.12+index*0.14); y:sceneSource.height*0.45; width:sceneSource.width*0.018
            height:sceneSource.height*0.9; rotation:48; color:index%2 ? "#24617b" : "#162b49"
        } }
    }
     // Two levels preserve the soft background effect while avoiding four
     // extra FBO passes on low-end/software-rendered boards.
     BlurItem { objectName:"backgroundGpuBlur"; anchors.fill:parent; source:sceneSource; levels:2; strength:2.0 }
    Rectangle { anchors.fill:parent; color:"#08121f"; opacity:0.72 }
    Image {
        id:homeWallpaper;objectName:"homeWallpaper";anchors.fill:parent;clip:true
        visible:status===Image.Ready
        source:systemTools.wallpaper;fillMode:Image.PreserveAspectCrop;autoTransform:true
        sourceSize.width:Math.ceil(win.width*Screen.devicePixelRatio)
        asynchronous:true
    }
    }
    BlurItem {
        objectName:"moduleBackgroundBlur";anchors.fill:parent;source:desktopBackground
         levels:2;strength:2.0;mixAmount:1.0
        visible:phase!=="home" && !appMinimized
        opacity:openProgress
    }

    footer:StatusDock {
        status:systemTools.deviceStatus;recentModel:openTasks;unread:unreadNotifications;clock:clockText;appOpen:phase!=="home";activeEntryId:appEntryId
        onHomeRequested:{if(phase==="open")minimizeApp()}
        onStatusRequested:showDevicePanel(false)
        onNotificationsRequested:showDevicePanel(true)
        onRecentRequested:function(entry){switchTask(entry)}
    }
    Item {
        id:homePane;objectName:"homePane";anchors.fill:parent;anchors.margins:win.edge;visible:phase!=="open";enabled:phase==="home"
        opacity:phase==="home"?1:1-openProgress*0.3
            RowLayout {id:homeHeader;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;height:Theme.hit;spacing:Theme.gap
                CarText {text:"应用";heading:true;Layout.fillWidth:true}
                CarText {visible:!narrow;text:connectionManager.cards.length?connectionManager.cards.length+" 个模块已接入":launcher.pageCount>1?"左右滑动浏览应用":"点按图标打开应用";subdued:true}
                IconButton {iconName:"tune";label:"设备快捷设置";onClicked:showDevicePanel(false)}
                IconButton {iconName:"minimize";label:"返回桌面";onClicked:desktopShell.showDesktop()}
            }
            AppLauncher {id:launcher;backgroundSource:desktopBackground;anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;anchors.top:homeHeader.bottom;anchors.topMargin:Theme.gap;model:homeCards;revision:homeRevision
                onActivated:function(entry,x,y){launch(entry,x,y)}}
    }
    Item {
        id:appShell;objectName:"animatedAppSurface"
        x:appMaximized || narrow?0:win.width*0.08; y:appMaximized || narrow?0:win.contentItem.height*0.06
        width:appMaximized || narrow?win.width:win.width*0.84; height:appMaximized || narrow?win.contentItem.height:win.contentItem.height*0.84
        visible:section!==0 && !appMinimized;enabled:phase==="open";opacity:openProgress*systemTools.moduleOpacity
        transform:Scale {origin.x:win.originX;origin.y:win.originY;xScale:0.12+0.88*openProgress;yScale:xScale}
        Rectangle {anchors.fill:parent;color:Theme.background;radius:24*uiScale*(1-openProgress)}
        Item {
            anchors.fill:parent;anchors.margins:win.edge
            RowLayout {id:windowTitleBar;objectName:"windowTitleBar";anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;height:Theme.hit;spacing:Theme.gap
                MaterialIcon {visible:!narrow;name:appIcon;Layout.preferredWidth:32*uiScale;Layout.preferredHeight:32*uiScale}
                CarText {text:appTitle;heading:true;Layout.fillWidth:true;maximumLineCount:1;elide:Text.ElideRight}
                IconButton {visible:!narrow;iconName:"minimize";label:"最小化应用窗口";onClicked:minimizeApp()}
                IconButton {visible:!narrow;iconName:appMaximized?"close_fullscreen":"fullscreen";label:appMaximized?"恢复应用窗口":"最大化应用窗口";onClicked:toggleMaximize()}
                IconButton {objectName:"closeAppButton";iconName:"close";label:"关闭当前应用";onClicked:{announce("关闭当前应用");closeApp()}}
            }
            StackLayout {
        id:pages;anchors.left:parent.left;anchors.right:parent.right;anchors.top:windowTitleBar.bottom;anchors.bottom:parent.bottom;anchors.topMargin:Theme.gap;currentIndex:Math.max(0,win.section-1)
        // MODULES: wrap toolbar controls, never compress a single fixed row.
        Flickable {
            id:moduleFlick; objectName:"connectionPage"; clip:true; contentWidth:width; contentHeight:moduleColumn.implicitHeight
            boundsBehavior:Flickable.StopAtBounds; flickableDirection:Flickable.VerticalFlick
            ColumnLayout {
                id:moduleColumn; width:moduleFlick.width; spacing:Theme.gap
                CarText {text:"这里只管理连接。模块功能请从主页图标打开。";Layout.fillWidth:true;subdued:true}
                Flow {
                    visible:desktopDebug; Layout.fillWidth:true; spacing:Theme.gap
                    CarButton { text:"插入示例"; onClicked:connectionManager.attach({event:"attach",module_id:80,module_instance:"desktop-demo"}) }
                    CarButton { text:"拔出触点"; onClicked:connectionManager.attach({event:"detach",module_id:80,module_instance:"desktop-demo"}) }
                    CarButton { text:"结束会话"; onClicked:connectionManager.disconnectModule("desktop-demo") }
                }
                CarText { text:connectionManager.status; Layout.fillWidth:true; padding:Theme.gap
                    background:Rectangle { color:Theme.surface; radius:12*uiScale } }
                Repeater {
                    model:connectionManager.cardModel
                    Rectangle {
                        required property var cardData
                        Layout.fillWidth:true; implicitHeight:sessionContent.implicitHeight+Theme.gap*2
                        color:Theme.surface; radius:16*uiScale; border.color:Theme.outline
                        ColumnLayout {
                            id:sessionContent; anchors.left:parent.left; anchors.right:parent.right; anchors.top:parent.top
                            anchors.margins:Theme.gap; spacing:Theme.gap
                            CarText { text:cardData.title; heading:true; Layout.fillWidth:true }
                            CarText { text:stateText(cardData.state)+(cardData.contact ? " · 触点已接入" : " · 无线会话"); subdued:true; Layout.fillWidth:true }
                            ProgressBar { value:cardData.progress/100; Layout.fillWidth:true
                                contentItem:Rectangle { implicitHeight:8*uiScale; color:Theme.raised
                                    Rectangle { width:parent.width*parent.parent.position; height:parent.height; color:Theme.accent } } }
                            Flow { Layout.fillWidth:true; spacing:Theme.gap
                    CarButton { text:"打开";enabled:cardData.ready;onClicked:openModule(cardData.instance) }
                    CarButton { text:"断开"; onClicked:{announce("请求断开模块");connectionManager.disconnectModule(cardData.instance)} }
                                CarButton { text:"重试"; visible:cardData.state==="error"; onClicked:connectionManager.retry(cardData.instance) }
                            }
                        }
                    }
                }
            }
        }
        // BROWSER: short or portrait screens use a scrollable toolbar over a sizeable viewport.
        Flickable {
            id:browserFlick; objectName:"browserPage"; clip:true; contentWidth:width; contentHeight:height
            Layout.fillWidth:true;Layout.fillHeight:true
            interactive:false
            ColumnLayout {
                id:browserColumn; anchors.fill:parent; spacing:Theme.gap
                CarField { id:location; objectName:"browserAddressField";Layout.fillWidth:true; text:activeBrowser?activeBrowser.url.toString():defaultBrowserUrl; placeholderText:"输入网址或域名"
                    onAccepted:navigateBrowser(text) }
                Flow { Layout.fillWidth:true; spacing:Theme.gap
                    CarButton { text:"前往"; accent:true; onClicked:navigateBrowser(location.text) }
                    CarButton { text:"后退";enabled:activeBrowser && activeBrowser.canGoBack;onClicked:activeBrowser.goBack() }
                    CarButton { text:"前进";enabled:activeBrowser && activeBrowser.canGoForward;onClicked:activeBrowser.goForward() }
                    CarButton { text:activeBrowser && activeBrowser.loading?"停止加载":"刷新";enabled:!!activeBrowser;onClicked:{if(activeBrowser.loading)activeBrowser.stop();else activeBrowser.reload()} }
                    CarButton { text:"收藏"; onClicked:if(activeBrowser) systemTools.remember(activeBrowser.url.toString()) }
                    CarButton { text:"放大"; onClicked:if(activeBrowser) activeBrowser.zoomFactor=Math.min(3,activeBrowser.zoomFactor+0.2) }
                    CarButton { text:"缩小"; onClicked:if(activeBrowser) activeBrowser.zoomFactor=Math.max(1,activeBrowser.zoomFactor-0.2) }
                }
                CarText { visible:browserStatus!==""; text:browserStatus; subdued:true; Layout.fillWidth:true; maximumLineCount:2; elide:Text.ElideRight }
                Flow { Layout.fillWidth:true; spacing:Theme.gap
                    Repeater { model:tabs; CarButton {
                        required property int index;required property var model;text:model.pageTitle || ("网页 "+(index+1));width:Math.min(240*uiScale,browserColumn.width);checked:browserTabs.currentIndex===index
                        onClicked:browserTabs.currentIndex=index
                    } }
                    CarButton { text:"新标签"; enabled:tabs.count<4; onClicked:{tabs.append({address:"about:blank"});browserTabs.currentIndex=tabs.count-1} }
                    CarButton { text:"关闭页"; enabled:tabs.count>1; onClicked:{let i=browserTabs.currentIndex;browserTabs.currentIndex=0;tabs.remove(i)} }
                }
                QtObject {
                    id:browserTabs
                    property int currentIndex:0
                    onCurrentIndexChanged: {
                        if (currentIndex>=0 && currentIndex<tabs.count) {
                            location.text=tabs.get(currentIndex).address
                            browserStatus=""
                        }
                    }
                }
                ListModel { id:tabs; ListElement { address:"https://baidu.com";pageTitle:"百度" } }
                CarButton { visible:marks.length>0;Layout.fillWidth:true;text:"打开书签（"+marks.length+"）";onClicked:bookmarkDialog.open() }
                StackLayout {
                    Layout.fillWidth:true; Layout.fillHeight:true;Layout.minimumHeight:0
                    currentIndex:browserTabs.currentIndex
                    Repeater { id:browserRepeater; model:tabs
                        WebEngineView {
                            objectName:"browserWebView"
                            Layout.fillWidth:true; Layout.fillHeight:true
                            required property string address;required property int index;url:address;zoomFactor:1.3
                            onTitleChanged:if(index>=0 && index<tabs.count)tabs.setProperty(index,"pageTitle",title)
                            onNavigationRequested:function(request) {
                                if (index>=0 && index<tabs.count && request.url.toString()!==tabs.get(index).address)
                                    tabs.setProperty(index,"address",request.url.toString())
                                if (index===browserTabs.currentIndex) { location.text=request.url.toString(); browserStatus="正在加载" }
                                request.accept()
                            }
                            onNewWindowRequested:function(request) {
                                const target=request.requestedUrl.toString()
                                if (tabs.count>=4) {
                                    if (index>=0 && index<tabs.count) {
                                        tabs.setProperty(index,"address",target || "about:blank")
                                        if (index===browserTabs.currentIndex) location.text=target
                                    }
                                    return
                                }
                                tabs.append({address:target || "about:blank"})
                                browserTabs.currentIndex=tabs.count-1
                            }
                            onUrlChanged: {
                                if(url.toString().indexOf("touch-link")>=0)browserTouchLinkPassed=true
                                if(index>=0 && index<tabs.count && tabs.get(index).address!==url.toString())tabs.setProperty(index,"address",url.toString())
                                if(index===browserTabs.currentIndex)location.text=url.toString()
                            }
                            onLoadingChanged:function(request) {
                                if (index===browserTabs.currentIndex) {
                                    if (request.status===WebEngineView.LoadStartedStatus) browserStatus="正在加载"
                                    else if (request.status===WebEngineView.LoadSucceededStatus) browserStatus=""
                                    else if (request.status===WebEngineView.LoadFailedStatus) browserStatus="加载失败："+(request.errorString || "无法打开此网页")
                                }
                                if(browserTestRunning && request.status===WebEngineView.LoadSucceededStatus && title==="Local terminal test")
                                    runJavaScript("21 * 2",function(value){browserTestPassed=value===42 && tabs.count===2;browserTestRunning=false})
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            id:filesFlick; objectName:"filesPage"; clip:true; contentWidth:width; contentHeight:height;interactive:false
            readonly property bool compact:width<500*uiScale || height<400*uiScale
            Item { id:filesColumn; width:filesFlick.width; height:filesFlick.height
                Rectangle { id:filesSidebar;visible:filesFlick.width>500*uiScale;anchors.left:parent.left;anchors.top:parent.top;anchors.bottom:parent.bottom;width:Math.min(220*uiScale,filesFlick.width*0.28);color:Theme.surface
                    ColumnLayout { anchors.fill:parent; anchors.margins:Theme.gap; spacing:Theme.gap
                        CarText { text:"位置"; subdued:true; Layout.fillWidth:true }
                        Repeater {model:[{label:"最近使用",icon:"refresh",location:"recent"},{label:"主目录",icon:"folder",location:"home"},{label:"文档",icon:"description",location:"documents"},{label:"下载",icon:"storage",location:"downloads"}]
                            Button {id:placeButton;required property var modelData;property bool auditControl:true
                                Layout.fillWidth:true;Layout.minimumHeight:Theme.hit;font.pointSize:Theme.body;padding:Theme.gap/2
                                Accessible.name:modelData.label
                                checked:modelData.location==="recent"?filesRecent:!filesRecent && folder===(modelData.location==="home"?"":systemTools.fileLocation(modelData.location))
                                background:Rectangle {radius:4;color:placeButton.down?Theme.pressed:placeButton.checked?Theme.raised:"transparent";border.width:placeButton.activeFocus?2:0;border.color:Theme.accent}
                                contentItem:RowLayout {spacing:Theme.gap/2
                                    MaterialIcon {name:placeButton.modelData.icon;Layout.preferredWidth:24*uiScale;Layout.preferredHeight:24*uiScale}
                                    Text {text:placeButton.modelData.label;font:placeButton.font;color:Theme.foreground;Layout.fillWidth:true;elide:Text.ElideRight}
                                }
                                onClicked:if(modelData.location==="recent"){filesRecent=true;fileSelection="";fileFilter="";refreshFiles()}else navigateFiles(modelData.location==="home"?"":systemTools.fileLocation(modelData.location))
                            }
                        }
                        Item { Layout.fillHeight:true }
                        CarText { text:systemTools.filesRoot; subdued:true; wrapMode:Text.Wrap; Layout.fillWidth:true }
                    }
                }
                ColumnLayout { anchors.fill:parent;anchors.margins:filesFlick.compact?Theme.gap/2:Theme.gap;anchors.leftMargin:filesSidebar.visible?filesSidebar.width+Theme.gap:filesFlick.compact?Theme.gap/2:Theme.gap;spacing:filesFlick.compact?Theme.gap/2:Theme.gap
                    Flow { Layout.fillWidth:true; spacing:Theme.gap/2
                        IconButton { visible:!filesFlick.compact;iconName:"arrow_back"; label:"后退"; enabled:fileHistoryIndex>0; onClicked:{fileHistoryIndex--;navigateFiles(fileHistory[fileHistoryIndex],false)} }
                        IconButton { visible:!filesFlick.compact;iconName:"arrow_forward"; label:"前进"; enabled:fileHistoryIndex<fileHistory.length-1; onClicked:{fileHistoryIndex++;navigateFiles(fileHistory[fileHistoryIndex],false)} }
                        IconButton { visible:!filesFlick.compact;iconName:"refresh"; label:"刷新"; onClicked:refreshFiles() }
                        IconButton { iconName:fileGridView?"view_list":"apps"; label:fileGridView?"列表视图":"网格视图"; onClicked:fileGridView=!fileGridView }
                        IconButton { iconName:"tune";label:"文件操作";onClicked:fileActions.open()
                            Menu {id:fileActions
                                MenuItem {text:"后退";enabled:fileHistoryIndex>0;onTriggered:{fileHistoryIndex--;navigateFiles(fileHistory[fileHistoryIndex],false)}}
                                MenuItem {text:"前进";enabled:fileHistoryIndex<fileHistory.length-1;onTriggered:{fileHistoryIndex++;navigateFiles(fileHistory[fileHistoryIndex],false)}}
                                MenuItem {text:"刷新";onTriggered:refreshFiles()}
                                MenuItem {text:"搜索";checkable:true;checked:fileSearchVisible;onTriggered:fileSearchVisible=!fileSearchVisible}
                                MenuItem {text:"主目录";onTriggered:navigateFiles("")}
                                MenuItem {text:"上一级";enabled:!!folder && !filesRecent;onTriggered:navigateFiles(fileParent())}
                                MenuItem {text:"最近使用";onTriggered:{filesRecent=true;fileSelection="";refreshFiles()}}
                                MenuSeparator {}
                                MenuItem {text:"新建文件";enabled:!filesRecent;onTriggered:requestFileOperation("newFile")}
                                MenuItem {text:"新建文件夹";enabled:!filesRecent;onTriggered:requestFileOperation("newFolder")}
                                MenuItem {text:"打开";enabled:!!fileSelection;onTriggered:{const row=fileRows.find(function(r){return (r.relative||r.name)===fileSelection});if(row)openFileRow(row)}}
                                MenuItem {text:"重命名";enabled:!!fileSelection && !filesRecent;onTriggered:requestFileOperation("rename")}
                                MenuItem {text:"删除";enabled:!!fileSelection;onTriggered:requestFileOperation("delete")}
                                MenuItem {text:"设为壁纸";enabled:!!fileSelection;onTriggered:{const row=fileRows.find(function(r){return (r.relative||r.name)===fileSelection});if(row && !row.directory)systemTools.setFileWallpaper(row.relative || ((folder?folder+"/":"")+row.name))}}
                            }
                        }
                    }
                    CarText {Layout.fillWidth:true;text:filesRecent?"最近使用":systemTools.filesRoot+(folder?"/"+folder:"");elide:Text.ElideMiddle;maximumLineCount:1;subdued:true}
                    CarField { id:fileSearch;visible:!filesFlick.compact || fileSearchVisible;Layout.fillWidth:true; Layout.minimumHeight:Theme.hit; placeholderText:"搜索文件"; text:fileFilter; onTextChanged:fileFilter=text }
                    CarText { visible:filteredFileRows.length===0; text:fileFilter?"没有匹配的文件":filesRecent?"暂无最近使用的文件":"当前文件夹为空"; subdued:true; Layout.fillWidth:true }
                    ScrollView {id:fileScroll;objectName:"fileScroll";Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumWidth:0;Layout.minimumHeight:48;Layout.preferredHeight:0;implicitWidth:0;contentWidth:availableWidth;contentHeight:fileContent.implicitHeight;clip:true
                        ScrollBar.vertical: ScrollBar {policy:ScrollBar.AlwaysOn}
                        ScrollBar.horizontal: ScrollBar {policy:ScrollBar.AlwaysOff}
                    Column {id:fileContent;width:fileScroll.availableWidth
                    GridLayout { visible:fileGridView; width:parent.width; columnSpacing:Theme.gap; rowSpacing:Theme.gap; columns:Math.max(1,Math.floor((width+Theme.gap)/(128*uiScale)))
                        Repeater { model:filteredFileRows
                            Item { required property var modelData; Layout.fillWidth:true; Layout.preferredWidth:112*uiScale; Layout.preferredHeight:136*uiScale
                                Column { anchors.fill:parent; spacing:8*uiScale
                                MaterialIcon { anchors.horizontalCenter:parent.horizontalCenter; name:fileIcon(modelData); light:modelData.directory; width:52*uiScale; height:52*uiScale }
                                    Text { width:parent.width; text:modelData.name; color:Theme.foreground; font.pointSize:Theme.body; horizontalAlignment:Text.AlignHCenter; wrapMode:Text.Wrap;elide:Text.ElideMiddle; maximumLineCount:2 }
                                }
                                Rectangle { anchors.fill:parent; z:-1; radius:8*uiScale; color:fileSelection===(modelData.relative||modelData.name)?Theme.raised:"transparent" }
                                activeFocusOnTab:true
                                Keys.onReturnPressed:openFileRow(modelData)
                                Accessible.role:Accessible.Button;Accessible.name:modelData.name;Accessible.onPressAction:openFileRow(modelData)
                                MouseArea { anchors.fill:parent; onClicked:fileSelection=modelData.relative||modelData.name; onDoubleClicked:openFileRow(modelData) }
                            }
                        }
                    }
                    ColumnLayout { visible:!fileGridView; width:parent.width
                        Repeater { model:filteredFileRows
                            Rectangle { required property var modelData; color:fileSelection===(modelData.relative||modelData.name)?Theme.raised:"transparent"; Layout.fillWidth:true; Layout.preferredHeight:Theme.hit
                                RowLayout { anchors.fill:parent; anchors.margins:Theme.gap/2; spacing:Theme.gap
                                    MaterialIcon { name:fileIcon(modelData); light:modelData.directory; Layout.preferredWidth:36*uiScale; Layout.preferredHeight:36*uiScale }
                                    Text { text:modelData.name; color:Theme.foreground; font.pointSize:Theme.body; Layout.fillWidth:true; elide:Text.ElideMiddle }
                                    Text { visible:filesFlick.width>600*uiScale;text:modelData.directory?"文件夹":(modelData.bytes+" B"); color:Theme.secondary; font.pointSize:Theme.body; }
                                }
                                activeFocusOnTab:true;Keys.onReturnPressed:openFileRow(modelData)
                                MouseArea { anchors.fill:parent; onClicked:fileSelection=modelData.relative||modelData.name; onDoubleClicked:openFileRow(modelData) }
                            }
                        }
                    }
                    }
                    }
                    CarText {visible:!filesFlick.compact;text:filteredFileRows.length+" 项"+(fileSelection?" · 已选择 "+fileSelection:"");subdued:true;Layout.fillWidth:true;elide:Text.ElideMiddle;maximumLineCount:1}
                }
            }
        }
        Flickable {
            id:settingsFlick; objectName:"settingsPage"; clip:true; contentWidth:width; contentHeight:settingsColumn.implicitHeight
            boundsBehavior:Flickable.StopAtBounds; flickableDirection:Flickable.VerticalFlick
            ColumnLayout {
                id:settingsColumn; width:settingsFlick.width; spacing:Theme.gap
                CarText { text:"显示与操作"; heading:true; Layout.fillWidth:true; padding:Theme.gap
                    background:Rectangle { color:Theme.surface; radius:12*uiScale } }
                CarText {text:"桌面壁纸";Layout.fillWidth:true}
                CarButton {text:"界面不透明度";Layout.fillWidth:true;onClicked:opacityDialog.open()}
                CarText {text:systemTools.wallpaper.toString() || "默认背景";subdued:true;Layout.fillWidth:true;maximumLineCount:1;elide:Text.ElideMiddle}
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarButton {text:"选择图片";onClicked:wallpaperPicker.open()}
                    CarButton {text:"恢复默认";enabled:systemTools.wallpaper.toString()!=="";onClicked:systemTools.setWallpaper("")}
                }
                CarText { text:"界面比例 "+Math.round(textScale*100)+"% · 布局自动适应屏幕"; Layout.fillWidth:true; padding:Theme.gap
                    background:Rectangle { color:Theme.surface; radius:12*uiScale } }
                Flow { Layout.fillWidth:true; spacing:Theme.gap
                    CarButton { text:"标准"; checked:textScale===1; onClicked:{textScale=1;systemTools.setInterfaceScale(1)} }
                    CarButton { text:"大字"; checked:textScale===1.25; onClicked:{textScale=1.25;systemTools.setInterfaceScale(1.25)} }
                    CarButton { text:"更大"; checked:textScale===1.5; onClicked:{textScale=1.5;systemTools.setInterfaceScale(1.5)} }
                }
                Switch { Layout.fillWidth:true; text:"语音播报";font.pointSize:Theme.body;implicitHeight:Theme.hit;checked:systemTools.speechEnabled;onToggled:systemTools.setSpeechEnabled(checked) }
                Flow { Layout.fillWidth:true; spacing:Theme.gap
                    CarButton { text:systemTools.hotspotBusy?"处理中":"开启热点"; enabled:!systemTools.hotspotBusy;onClicked:{announce("开启热点");systemTools.hotspot(true)} }
                    CarButton { text:"关闭热点"; enabled:!systemTools.hotspotBusy;onClicked:{announce("关闭热点");systemTools.hotspot(false)} }
                }
                CarText { text:"网络状态"; heading:true; Layout.fillWidth:true; padding:Theme.gap
                    background:Rectangle { color:Theme.surface; radius:12*uiScale } }
                CarText { text:systemTools.network; Layout.fillWidth:true; padding:Theme.gap
                    background:Rectangle { color:Theme.surface; radius:12*uiScale } }
                CarText { text:"操作日志"; heading:true; Layout.fillWidth:true; padding:Theme.gap
                    background:Rectangle { color:Theme.surface; radius:12*uiScale } }
                Repeater { model:systemTools.logs; CarText {
                    required property string modelData; text:modelData; Layout.fillWidth:true; padding:Theme.gap
                    background:Rectangle { color:Theme.surface; radius:12*uiScale }
                } }
            }
        }
        Flickable {
            id:functionFlick;objectName:"functionPage";clip:true;contentWidth:width;contentHeight:functionColumn.implicitHeight
            boundsBehavior:Flickable.StopAtBounds;flickableDirection:Flickable.VerticalFlick
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id:functionColumn;width:functionFlick.width;spacing:Theme.gap
                Flow {
                    id:moduleActions;Layout.fillWidth:true;spacing:Theme.gap
                    Repeater {model:activeActions;CarButton {
                        required property string modelData
                        text:({start:"开始",stop:"停止",frame:"采集单帧",reset:"复位",increment:"加一",decrement:"减一",drop_link:"模拟断线",request_disconnect:"模块断开",stale_data:"旧会话测试"})[modelData] || modelData
                        visible:modelData!=="open";onClicked:connectionManager.action(activeSession,modelData)
                    }}
                }
                Loader {
                    id:moduleLoader;objectName:"moduleFunctionLoader"
                    Layout.fillWidth:true
                    Layout.preferredHeight:item ? Math.max(item.implicitHeight, 1) : 0
                    asynchronous:false
                    onLoaded: {
                        if (item && item.hostUiScale !== undefined)
                            item.hostUiScale = Qt.binding(function() { return win.uiScale })
                    }
                }
            }
        }
        Flickable {
            id:calculatorFlick;objectName:"calculatorPage";clip:true;contentWidth:width;contentHeight:calculatorColumn.implicitHeight
            boundsBehavior:Flickable.StopAtBounds;flickableDirection:Flickable.VerticalFlick
            ColumnLayout { id:calculatorColumn; width:calculatorFlick.width; spacing:Theme.gap
                CarText {text:"计算器";heading:true;Layout.fillWidth:true}
                Rectangle {Layout.fillWidth:true;Layout.preferredHeight:88*uiScale;color:Theme.surface;radius:12*uiScale
                    Text {anchors.fill:parent;anchors.margins:Theme.gap;text:calcDisplay;color:Theme.foreground;font.pointSize:32*uiScale;horizontalAlignment:Text.AlignRight;verticalAlignment:Text.AlignVCenter;elide:Text.ElideLeft}
                }
                GridLayout {Layout.fillWidth:true;columns:4;rowSpacing:Theme.gap;columnSpacing:Theme.gap
                    Repeater {model:["C","÷","×","-","7","8","9","+","4","5","6","=","1","2","3",".","0"]
                        CarButton {required property string modelData;text:modelData;Layout.fillWidth:true;Layout.minimumHeight:Theme.hit;accent:modelData==="=";onClicked:calcInput(modelData)}
                    }
                }
                CarText {text:"支持加、减、乘、除和小数；结果保留 8 位小数。";subdued:true;Layout.fillWidth:true;wrapMode:Text.Wrap}
            }
        }
        Flickable {
            id:paintFlick;objectName:"paintPage";clip:true;contentWidth:width;contentHeight:paintColumn.implicitHeight;interactive:false
            ColumnLayout {id:paintColumn;width:paintFlick.width;spacing:Theme.gap
                CarText {text:"画图";heading:true;Layout.fillWidth:true}
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarButton {text:"撤销";enabled:paintStrokes.length>0;onClicked:paintUndo()}
                    CarButton {text:"重做";enabled:paintRedo.length>0;onClicked:paintRedoStroke()}
                    CarButton {text:"清空画布";onClicked:paintClear()}
                    CarButton {text:"保存 PNG";accent:true;onClicked:{const result=systemTools.saveCameraFrame(paintCanvas.toDataURL("image/png"),systemTools.cameraPhotoPath());paintStatus.text=result===""?"已保存到图片目录":result}}
                }
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarText {text:"颜色";verticalAlignment:Text.AlignVCenter;height:Theme.hit}
                    Repeater {model:["#13283d","#d83d58","#e7a72f","#269c77","#3478c6","#8d55b6","#ffffff"]
                        Rectangle {required property string modelData;width:Theme.hit*0.7;height:Theme.hit*0.7;radius:width/2;color:modelData;border.width:paintColor===modelData&&!paintEraser?3:1;border.color:paintColor===modelData&&!paintEraser?Theme.accent:Theme.outline
                            MouseArea {anchors.fill:parent;onClicked:{paintColor=parent.modelData;paintEraser=false}}
                        }
                    }
                    CarButton {text:"橡皮擦";checked:paintEraser;onClicked:paintEraser=!paintEraser}
                }
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarText {text:"笔刷";verticalAlignment:Text.AlignVCenter;height:Theme.hit}
                    Repeater {model:[2,5,10,18]
                        CarButton {required property int modelData;text:modelData+" px";checked:paintSize===modelData;onClicked:paintSize=modelData}
                    }
                }
                CarText {id:paintStatus;text:"用手指或鼠标在画布上绘制";subdued:true;Layout.fillWidth:true}
                Rectangle {Layout.fillWidth:true;Layout.preferredHeight:Math.max(260*uiScale,paintFlick.height-180*uiScale);color:"white";border.color:Theme.outline;border.width:2;radius:8*uiScale
                    Canvas {id:paintCanvas;anchors.fill:parent;property var currentPoints:[];property real cursorX:0;property real cursorY:0;property bool cursorVisible:false
                        function redraw(){requestPaint()}
                        function clearCanvas(){paintStrokes=[];paintRedo=[];requestPaint()}
                        function drawStroke(c,stroke){
                            const points=stroke.points||[];if(!points.length)return
                            c.strokeStyle=stroke.color;c.lineWidth=stroke.size;c.lineCap="round";c.lineJoin="round";c.beginPath();c.moveTo(points[0].x,points[0].y)
                            for(let i=1;i<points.length;++i)c.lineTo(points[i].x,points[i].y)
                            if(points.length===1)c.lineTo(points[0].x+0.1,points[0].y+0.1);c.stroke()
                        }
                        onPaint:{const c=getContext("2d");c.reset();c.fillStyle="white";c.fillRect(0,0,width,height);for(let stroke of paintStrokes)drawStroke(c,stroke);if(currentPoints.length)drawStroke(c,{color:paintEraser?"white":paintColor,size:paintSize*uiScale,points:currentPoints})}
                        MouseArea {anchors.fill:parent;preventStealing:true;propagateComposedEvents:false;hoverEnabled:true
                            onEntered:paintCanvas.cursorVisible=true
                            onExited:if(!pressed)paintCanvas.cursorVisible=false
                            onPressed:{paintCanvas.cursorVisible=true;paintCanvas.cursorX=mouse.x;paintCanvas.cursorY=mouse.y;paintCanvas.currentPoints=[{x:mouse.x,y:mouse.y}]}
                            onPositionChanged:{paintCanvas.cursorVisible=true;paintCanvas.cursorX=mouse.x;paintCanvas.cursorY=mouse.y;if(pressed){paintCanvas.currentPoints=paintCanvas.currentPoints.concat([{x:mouse.x,y:mouse.y}]);paintCanvas.requestPaint()}}
                            onReleased:{if(paintCanvas.currentPoints.length){paintStrokes=paintStrokes.concat([{color:paintEraser?"white":paintColor,size:paintSize*uiScale,points:paintCanvas.currentPoints}]);paintRedo=[];paintCanvas.currentPoints=[];paintCanvas.requestPaint()}}
                            onCanceled:{paintCanvas.currentPoints=[];paintCanvas.cursorVisible=false}
                        }
                    }
                    Rectangle {id:paintPreview;enabled:false;visible:paintCanvas.cursorVisible;width:Math.max(8,paintSize*uiScale);height:width;x:paintCanvas.x+paintCanvas.cursorX-width/2;y:paintCanvas.y+paintCanvas.cursorY-height/2;radius:width/2;color:paintEraser?"transparent":paintColor;opacity:paintEraser?1:0.45;border.width:Math.max(1,uiScale);border.color:paintEraser?"#111111":paintColor}
                }
            }
        }
        Flickable {
            id:notesFlick;objectName:"notesPage";clip:true;contentWidth:width;contentHeight:notesColumn.implicitHeight
            boundsBehavior:Flickable.StopAtBounds;flickableDirection:Flickable.VerticalFlick
            ColumnLayout {id:notesColumn;width:notesFlick.width;spacing:Theme.gap
                RowLayout {Layout.fillWidth:true;spacing:Theme.gap
                    CarText {text:"文本便笺";heading:true;Layout.fillWidth:true}
                    CarButton {text:"保存";accent:true;onClicked:saveNote()}
                    CarButton {text:"清空";onClicked:noteEditor.text=""}
                }
                CarText {text:"保存位置："+systemTools.filesRoot+"/"+noteFile;subdued:true;Layout.fillWidth:true;elide:Text.ElideMiddle}
                ScrollView {Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:260*uiScale
                    TextArea {id:noteEditor;text:noteText;wrapMode:Text.Wrap;font.pointSize:Theme.body;color:Theme.foreground;selectionColor:Theme.accent;selectedTextColor:Theme.accentInk
                        onTextChanged:noteText=text
                        background:Rectangle {color:Theme.surface;border.color:Theme.outline;border.width:1}
                    }
                }
                CarText {text:"便笺支持触摸输入、编辑和保存为 UTF-8 文本。";subdued:true;Layout.fillWidth:true;wrapMode:Text.Wrap}
            }
        }
            }
        }
    }
    Drawer {
        id:devicePanel;objectName:"deviceControlPanel";edge:Qt.BottomEdge;width:win.width;height:Math.min(760*uiScale,win.height-Theme.gap,controlColumn.implicitHeight+Theme.gap*2)
        padding:Theme.gap;modal:true;dragMargin:0
        property bool jumpToNotifications:false
        background:Rectangle {color:Theme.surface;radius:24*uiScale}
        Overlay.modal:Rectangle {color:"#99040b14"}
        onOpened:{if(jumpToNotifications)unreadNotifications=0;Qt.callLater(function(){controlFlick.contentY=jumpToNotifications?Math.min(notificationsTitle.y,Math.max(0,controlFlick.contentHeight-controlFlick.height)):0})}
        contentItem:Flickable {
            id:controlFlick;clip:true;contentWidth:width;contentHeight:controlColumn.implicitHeight;boundsBehavior:Flickable.StopAtBounds
            ColumnLayout {
                id:controlColumn;width:controlFlick.width;spacing:Theme.gap
                RowLayout {Layout.fillWidth:true;spacing:Theme.gap
                    CarText {text:"设备状态";heading:true;Layout.fillWidth:true}
                    IconButton {iconName:"close";label:"关闭设备面板";onClicked:devicePanel.close()}
                }
                GridLayout {
                    Layout.fillWidth:true;columns:width>680*uiScale?2:1;columnSpacing:Theme.gap;rowSpacing:Theme.gap
                    CarText {text:"网络 · "+(systemTools.deviceStatus.networkLabel||"正在读取");Layout.fillWidth:true}
                    CarText {text:"蓝牙 · "+(!systemTools.deviceStatus.bluetoothAvailable?"未检测到适配器":systemTools.deviceStatus.bluetoothOn?"已开启":"已关闭");Layout.fillWidth:true}
                    CarText {text:systemTools.deviceStatus.batteryPresent?"电量 · "+systemTools.deviceStatus.batteryPercent+"%":"电源 · 未检测到系统电池";Layout.fillWidth:true}
                    CarText {text:"模块 · "+connectionManager.cards.length+" 个会话";Layout.fillWidth:true}
                    CarText {text:"内存 · "+(systemTools.deviceStatus.memoryLabel||"正在读取");Layout.fillWidth:true}
                    CarText {text:"存储 · "+(systemTools.deviceStatus.storageLabel||"正在读取");Layout.fillWidth:true}
                }
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarButton {text:"连接管理";onClicked:{devicePanel.close();go(1)}}
                    CarButton {text:"系统设置";onClicked:{devicePanel.close();go(4)}}
                    CarButton {text:"界面不透明度";onClicked:{devicePanel.close();opacityDialog.open()}}
                    CarButton {text:systemTools.speechEnabled?"关闭播报":"开启播报";onClicked:systemTools.setSpeechEnabled(!systemTools.speechEnabled)}
                    CarButton {text:systemTools.deviceStatus.bluetoothOn?"关闭蓝牙":"开启蓝牙";enabled:systemTools.deviceStatus.bluetoothAvailable;onClicked:systemTools.setBluetoothEnabled(!systemTools.deviceStatus.bluetoothOn)}
                    CarButton {text:systemTools.hotspotBusy?"处理中":"开启热点";enabled:!systemTools.hotspotBusy;onClicked:systemTools.hotspot(true)}
                    CarButton {text:"关闭热点";enabled:!systemTools.hotspotBusy;onClicked:systemTools.hotspot(false)}
                }
                CarText {id:notificationsTitle;text:"通知";heading:true;Layout.fillWidth:true}
                CarButton {text:"清除通知";enabled:notices.count>0;Layout.fillWidth:true;onClicked:{notices.clear();unreadNotifications=0}}
                CarText {visible:notices.count===0;text:"暂无通知";subdued:true;Layout.fillWidth:true}
                Repeater {model:notices
                    CarText {required property string message;required property string time;text:time+"  "+message;Layout.fillWidth:true;padding:Theme.gap
                        background:Rectangle {color:Theme.background;radius:16*uiScale}}
                }
            }
        }
    }
    NativeDialogs.FileDialog {
        id:wallpaperPicker;title:"选择桌面壁纸";fileMode:NativeDialogs.FileDialog.OpenFile
        nameFilters:["图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif)","所有文件 (*)"]
        onAccepted:systemTools.setWallpaper(selectedFile)
    }
    Dialog {
        id:opacityDialog;objectName:"opacityDialog";parent:Overlay.overlay;modal:true;opacity:1
        x:(win.width-width)/2;y:(win.height-height)/2
        width:Math.min(640*uiScale,win.width-Theme.gap*2);height:Math.min(560*uiScale,win.height-Theme.gap*2)
        padding:Theme.gap;title:"界面不透明度"
        background:Rectangle {color:Theme.surface;radius:16*uiScale}
        contentItem:Flickable {clip:true;contentWidth:width;contentHeight:opacityContents.implicitHeight
            ColumnLayout {id:opacityContents;width:parent.width;spacing:Theme.gap
                CarText {text:"主页卡片："+Math.round(systemTools.launcherOpacity*100)+"%";Layout.fillWidth:true}
                Slider {objectName:"launcherOpacitySlider";Layout.fillWidth:true;implicitHeight:Theme.hit;from:0;to:1;stepSize:0.01;value:systemTools.launcherOpacity;Accessible.name:"主页卡片不透明度";onMoved:systemTools.setLauncherOpacity(value)}
                CarText {text:"模块界面："+Math.round(systemTools.moduleOpacity*100)+"%";Layout.fillWidth:true}
                Slider {objectName:"moduleOpacitySlider";Layout.fillWidth:true;implicitHeight:Theme.hit;from:0;to:1;stepSize:0.01;value:systemTools.moduleOpacity;Accessible.name:"模块界面不透明度";onMoved:systemTools.setModuleOpacity(value)}
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarButton {text:"恢复 65%";onClicked:{systemTools.setLauncherOpacity(0.65);systemTools.setModuleOpacity(0.65)}}
                    CarButton {text:"完成";onClicked:opacityDialog.close()}
                }
            }
        }
    }
    Dialog {
        id:trustDialog; modal:true; closePolicy:Popup.NoAutoClose
        x:(win.width-width)/2; y:(win.height-height)/2
        width:Math.min(760*uiScale,win.width-Theme.gap*2)
        height:Math.min(trustContents.implicitHeight+Theme.gap*2,win.height-Theme.gap*2)
        padding:Theme.gap; background:Rectangle {color:Theme.surface;radius:20*uiScale;border.color:Theme.accent;border.width:2}
        onClosed:Qt.callLater(showNextTrust)
        contentItem:Flickable {
            clip:true; contentWidth:width; contentHeight:trustContents.implicitHeight
            boundsBehavior:Flickable.StopAtBounds; flickableDirection:Flickable.VerticalFlick
            ColumnLayout {
                id:trustContents; width:parent.width; spacing:Theme.gap
                CarText {text:"确认模块权限";heading:true;Layout.fillWidth:true}
                CarText {text:trustItem.title;heading:true;Layout.fillWidth:true}
                CarText {text:"签名已验证。声明权限："+trustItem.permissions.join("、");Layout.fillWidth:true}
                CarText {text:"插件在本程序内运行，请只允许可信发布者的功能。";subdued:true;Layout.fillWidth:true}
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarButton {text:"仅本次";accent:true;onClicked:approveTrustOnce()}
                    CarButton {text:"始终信任";onClicked:{connectionManager.trust(trustItem.instance,true);trustDialog.close()}}
                    CarButton {text:"拒绝";onClicked:{connectionManager.disconnectModule(trustItem.instance);trustDialog.close()}}
                }
            }
        }
    }
    Dialog {
        id:bookmarkDialog;modal:true;x:(win.width-width)/2;y:(win.height-height)/2
        width:Math.min(760*uiScale,win.width-Theme.gap*2)
        height:Math.min(bookmarkColumn.implicitHeight+Theme.gap*2,win.height-Theme.gap*2);padding:Theme.gap
        background:Rectangle {color:Theme.surface;radius:20*uiScale;border.color:Theme.outline}
        contentItem:Flickable {clip:true;contentWidth:width;contentHeight:bookmarkColumn.implicitHeight;boundsBehavior:Flickable.StopAtBounds
            ColumnLayout {id:bookmarkColumn;width:parent.width;spacing:Theme.gap
                CarText {text:"我的书签";heading:true;Layout.fillWidth:true}
                Repeater {model:marks;CarButton {
                    required property string modelData;Layout.fillWidth:true;text:modelData
                    onClicked:{if(activeBrowser)activeBrowser.url=modelData;bookmarkDialog.close();go(2)}
                }}
                CarButton {text:"关闭";Layout.fillWidth:true;onClicked:bookmarkDialog.close()}
            }
        }
    }
    Dialog {
        id:fileOperationDialog;modal:true;title:({newFile:"新建文件",newFolder:"新建文件夹",rename:"重命名",delete:"确认删除"})[fileOperation] || "文件操作"
        width:Math.min(600*uiScale,win.width-Theme.gap*2);height:Math.min(400*uiScale,win.height-Theme.gap*2);x:(win.width-width)/2;y:(win.height-height)/2
        padding:Theme.gap
        contentItem:Flickable {clip:true;contentWidth:width;contentHeight:operationContents.implicitHeight
        ColumnLayout {id:operationContents;width:parent.width;spacing:Theme.gap
            CarText {visible:fileOperation==="delete";text:"删除 "+fileSelection+"？";Layout.fillWidth:true;wrapMode:Text.Wrap}
            CarField {id:operationName;visible:fileOperation!=="delete";Layout.fillWidth:true;placeholderText:"名称";onAccepted:completeFileOperation()}
            CarText {visible:!!fileOperationError;text:fileOperationError;Layout.fillWidth:true;wrapMode:Text.Wrap;color:Theme.warning}
            Flow {Layout.fillWidth:true;spacing:Theme.gap
                CarButton {text:"取消";onClicked:fileOperationDialog.close()}
                CarButton {text:fileOperation==="delete"?"删除":"确定";accent:true;onClicked:completeFileOperation()}
            }
        }
        }
    }
    Dialog {
        id:fileDialog; modal:true; x:(win.width-width)/2;y:(win.height-height)/2
        property string saveError:""
        onOpened:saveError=""
        width:Math.min(1000*uiScale,win.width-Theme.gap*2);height:win.height-Theme.gap*2;padding:Theme.gap
        background:Rectangle {color:Theme.surface;radius:20*uiScale;border.color:Theme.outline}
        contentItem:Flickable {clip:true;contentWidth:width;contentHeight:editorColumn.implicitHeight;boundsBehavior:Flickable.StopAtBounds
            ColumnLayout {
                id:editorColumn;width:parent.width;spacing:Theme.gap
                CarText {text:"文件编辑";heading:true;Layout.fillWidth:true}
                CarField {id:editedName;text:filePath;readOnly:true;Layout.fillWidth:true;placeholderText:"文件名称"}
                CarText {visible:fileDialog.saveError!=="";text:fileDialog.saveError;Layout.fillWidth:true;wrapMode:Text.Wrap}
                Flow {Layout.fillWidth:true;spacing:Theme.gap
                    CarButton {text:"保存";accent:true;enabled:fileEditable;onClicked:{if(systemTools.write(filePath,viewer.text)){refreshFiles();fileDialog.close()}else fileDialog.saveError="保存失败：请检查写入权限、文件路径及 64 KiB 文本大小限制。"}}
                    CarButton {text:"重命名";enabled:!!fileSelection && !filesRecent;onClicked:{fileDialog.close();requestFileOperation("rename")}}
                    CarButton {text:"删除";enabled:!!fileSelection;onClicked:{fileDialog.close();requestFileOperation("delete")}}
                    CarButton {text:"关闭";onClicked:fileDialog.close()}
                }
                ScrollView {Layout.fillWidth:true;Layout.preferredHeight:Math.max(240*uiScale,fileDialog.height*0.5)
                    ScrollBar.vertical:ScrollBar {
                        implicitWidth:48*uiScale;leftPadding:20*uiScale;rightPadding:20*uiScale;minimumSize:0.2
                        contentItem:Rectangle {implicitWidth:8*uiScale;color:Theme.accent;radius:4*uiScale}
                    }
                    TextArea {id:viewer;readOnly:!fileEditable;wrapMode:Text.Wrap;font.pointSize:Theme.body;color:Theme.foreground
                        padding:Theme.gap;selectionColor:Theme.accent;selectedTextColor:Theme.accentInk
                        background:Rectangle {color:Theme.background}}
                }
                CarButton {visible:filePath!=="";text:"使用系统默认应用打开";Layout.fillWidth:true;onClicked:systemTools.openWithDefault(filePath)}
            }
        }
    }
    Component.onCompleted:{marks=systemTools.bookmarks();syncCards()}
}
