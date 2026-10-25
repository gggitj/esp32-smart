import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:page;property var sessionModel:null
    property var sample:sessionModel?sessionModel.data:({})
    property string instance:""
    property string saveStatus:""
    property string snapshot:""
    color:"#102238";implicitHeight:620;implicitWidth:640
    Component.onCompleted:instance=activeSession
    function saveSnapshot(target) {
        const error=systemTools.saveCameraFrame(page.snapshot,target)
        page.saveStatus=error || ("照片已保存："+target)
        return error===""
    }
    Timer {interval:400;repeat:true;running:page.visible && !!page.sample.running
        onTriggered:connectionManager.action(page.instance,"frame")}
    ColumnLayout {anchors.fill:parent;anchors.margins:16;spacing:16
        Label {text:"相机 · 模拟测试画面";color:"white";font.pointSize:24;wrapMode:Text.Wrap;Layout.fillWidth:true}
        Label {text:"点击上方“开始”预览；画面由 TCP 模拟设备生成，不是真实摄像头。";color:"white";font.pointSize:18;wrapMode:Text.Wrap;Layout.fillWidth:true}
        Image {id:preview;Layout.fillWidth:true;Layout.preferredHeight:240;Layout.minimumHeight:160;fillMode:Image.PreserveAspectFit;source:page.sample.frame || "";cache:false
            Label {anchors.centerIn:parent;visible:preview.status!==Image.Ready;text:preview.status===Image.Error?"图像解码失败":"等待画面";color:"white";font.pointSize:18}}
        Label {text:(page.sample.running?"预览中":"已停止")+" · 帧序号 "+(page.sample.sequence || 0);color:"white";font.pointSize:18;Layout.fillWidth:true}
        Button {objectName:"cameraSaveButton";text:"拍照 / 保存 PNG";enabled:preview.status===Image.Ready;font.pointSize:18;implicitHeight:64
            onClicked:{page.snapshot=page.sample.frame;page.saveStatus="";savePath.text=systemTools.cameraPhotoPath();saveDialog.open()}}
        Label {text:page.saveStatus;color:"white";font.pointSize:18;Layout.fillWidth:true;wrapMode:Text.Wrap}
    }
    Dialog {id:saveDialog;objectName:"cameraSaveDialog";title:"保存模拟相机照片";modal:true;parent:Overlay.overlay
        width:Math.min(640,parent?parent.width-24:640);height:Math.min(420,parent?parent.height-24:420)
        x:parent?(parent.width-width)/2:0;y:parent?(parent.height-height)/2:0
        onRejected:page.saveStatus="已取消保存"
        contentItem:ScrollView {clip:true
            ColumnLayout {width:saveDialog.availableWidth;spacing:16
                Label {text:"保存路径（可修改目录和文件名）";font.pointSize:18;wrapMode:Text.Wrap;Layout.fillWidth:true}
                TextField {id:savePath;objectName:"cameraSavePath";font.pointSize:18;Layout.fillWidth:true;selectByMouse:true;implicitHeight:64}
                Label {text:page.saveStatus;font.pointSize:18;wrapMode:Text.Wrap;Layout.fillWidth:true}
                Flow {Layout.fillWidth:true;spacing:16
                    Button {text:"取消";font.pointSize:18;implicitHeight:64;onClicked:saveDialog.reject()}
                    Button {objectName:"cameraConfirmSave";text:"保存 PNG";font.pointSize:18;implicitHeight:64;onClicked:{let target=savePath.text.trim();if(!target.toLowerCase().endsWith(".png"))target+=".png";if(page.saveSnapshot(target))saveDialog.close()}}
                }
            }
        }
    }
}
