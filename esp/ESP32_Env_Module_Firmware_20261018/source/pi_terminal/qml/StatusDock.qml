import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:dock;objectName:"systemStatusDock"
    property var status:({})
    property var recentModel
    property int unread:0
    property string clock:""
    property bool appOpen:false
    property string activeEntryId:""
    readonly property string batteryIcon:!status.batteryPresent?"power":status.batteryState==="Charging"?"battery_charging_full":status.batteryPercent>=98?"battery_full":"battery_"+Math.max(0,Math.min(6,Math.floor(status.batteryPercent*7/100)))+"_bar"
    signal homeRequested()
    signal statusRequested()
    signal notificationsRequested()
    signal recentRequested(var entry)
    color:Theme.background;implicitHeight:Theme.hit+Theme.gap*2
    RowLayout {
        anchors.fill:parent;anchors.margins:Theme.gap;spacing:Theme.gap
        IconButton {objectName:"homeButton";iconName:"apps";label:"返回应用主页";onClicked:dock.homeRequested()}
        Flickable {
            objectName:"taskbarTasks";Layout.fillWidth:true;Layout.minimumWidth:Theme.hit;Layout.preferredHeight:Theme.hit
            contentWidth:taskIcons.implicitWidth;contentHeight:height;clip:true;boundsBehavior:Flickable.StopAtBounds;flickableDirection:Flickable.HorizontalFlick
        RowLayout {
            id:taskIcons;height:parent.height;spacing:Theme.gap/2
            Repeater {model:dock.recentModel
                IconButton {
                    required property var model;iconName:model.iconName
                    filled:true;accentColor:model.accentColor
                    label:(model.minimized?"恢复最小化应用：":"切换到应用：")+model.title
                    onClicked:dock.recentRequested({entryId:model.entryId,title:model.title,iconName:model.iconName,accentColor:model.accentColor,route:model.route,instance:model.instance,ready:true})
                    Rectangle {anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.bottom;width:parent.width/3;height:3;color:model.entryId===dock.activeEntryId && !model.minimized?Theme.foreground:Theme.secondary}
                }
            }
        }
        }
        IconButton {
            visible:dock.width>=540*Theme.scaleFactor
            objectName:"notificationsButton";iconName:"notifications";label:"通知，"+dock.unread+"条未读";onClicked:dock.notificationsRequested()
            Rectangle {visible:dock.unread>0;anchors.right:parent.right;anchors.top:parent.top;width:Math.max(32*Theme.scaleFactor,badge.implicitWidth+8*Theme.scaleFactor);height:32*Theme.scaleFactor;radius:height/2;color:Theme.accent
                CarText{id:badge;anchors.centerIn:parent;text:dock.unread>99?"99+":dock.unread.toString();color:Theme.accentInk}}
        }
        Button {
            id:statusButton;objectName:"deviceStatusButton";property bool auditControl:true
            font.pointSize:Theme.body;implicitHeight:Theme.hit;implicitWidth:Math.max(Theme.hit,indicators.implicitWidth)
            padding:0;leftPadding:0;rightPadding:0;topPadding:0;bottomPadding:0;leftInset:0;rightInset:0;topInset:0;bottomInset:0
            Accessible.name:"设备状态和快捷设置";onClicked:dock.statusRequested()
            background:Rectangle{radius:16*Theme.scaleFactor;color:statusButton.down?Theme.pressed:"transparent";border.width:statusButton.activeFocus?2:0;border.color:Theme.accent}
            contentItem:RowLayout {
                id:indicators;spacing:Theme.gap
                MaterialIcon {Layout.preferredWidth:24*Theme.scaleFactor;Layout.preferredHeight:24*Theme.scaleFactor
                    visible:dock.width>=420*Theme.scaleFactor
                    name:dock.status.wifiConnected?"wifi":dock.status.networkOnline?"lan":"wifi_off"}
                MaterialIcon {visible:dock.width>=600*Theme.scaleFactor;Layout.preferredWidth:24*Theme.scaleFactor;Layout.preferredHeight:24*Theme.scaleFactor
                    name:dock.status.bluetoothOn?"bluetooth":"bluetooth_disabled"}
                MaterialIcon {visible:dock.width>=420*Theme.scaleFactor;Layout.preferredWidth:24*Theme.scaleFactor;Layout.preferredHeight:24*Theme.scaleFactor
                    name:dock.batteryIcon}
                CarText {visible:dock.status.batteryPresent && dock.width>=1000*Theme.scaleFactor;text:dock.status.batteryPercent+"%"}
                MaterialIcon {visible:dock.width<420*Theme.scaleFactor;name:"settings";Layout.preferredWidth:32*Theme.scaleFactor;Layout.preferredHeight:32*Theme.scaleFactor}
                CarText {visible:dock.width>=480*Theme.scaleFactor;text:dock.clock}
            }
        }
    }
}
