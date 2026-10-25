import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:page;property var sessionModel:null
    property var sample:sessionModel?sessionModel.data:({})
    color:"#102238";implicitHeight:600;implicitWidth:600
    ListModel {id:history}
    onSampleChanged:{if(sample.value!==undefined){history.insert(0,{message:"计数 "+sample.value+" · "+sample.operation});while(history.count>100)history.remove(history.count-1)}}
    ColumnLayout {anchors.fill:parent;anchors.margins:16;spacing:16
        Label {text:"计数器 · TCP 模拟设备";color:"white";font.pointSize:24;wrapMode:Text.Wrap;Layout.fillWidth:true}
        Label {text:"使用上方“加一 / 减一 / 复位”。结果由模拟设备返回，各连接独立计数。";color:"white";font.pointSize:18;wrapMode:Text.Wrap;Layout.fillWidth:true}
        Label {text:sample.value===undefined?"等待操作":String(sample.value);color:"#85deff";font.pointSize:40;Layout.fillWidth:true;horizontalAlignment:Text.AlignHCenter}
        ListView {Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:160;clip:true;model:history
            delegate:Label {required property string message;width:ListView.view.width;text:message;color:"white";font.pointSize:18;padding:12;wrapMode:Text.Wrap}
            ScrollBar.vertical:ScrollBar {}
        }
        Button {text:"清除本页记录";font.pointSize:18;implicitHeight:64;onClicked:history.clear()}
    }
}
