import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:page;property var sessionModel:null
    property var sample:sessionModel?sessionModel.data:({})
    property var history:[]
    color:"#102238";implicitHeight:600;implicitWidth:600
    onSampleChanged:{if(sample.temperature!==undefined){history=history.concat([sample.temperature]).slice(-40);chart.requestPaint()}}
    ColumnLayout {anchors.fill:parent;anchors.margins:16;spacing:16
        Label {text:"环境监测 · 模拟数据";color:"white";font.pointSize:24;Layout.fillWidth:true;wrapMode:Text.Wrap}
        Label {text:"点击上方“开始”读取一组温湿度；“停止”暂停，“复位”清零采样序号。";color:"white";font.pointSize:18;wrapMode:Text.Wrap;Layout.fillWidth:true}
        Label {text:sample.temperature===undefined?"等待采样":("温度："+sample.temperature+" °C\n湿度："+sample.humidity+" %\n采样序号："+sample.sequence+" · "+sample.status);color:"white";font.pointSize:24;Layout.fillWidth:true;wrapMode:Text.Wrap}
        Canvas {id:chart;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:160
            onWidthChanged:requestPaint();onHeightChanged:requestPaint()
            onPaint:{let c=getContext("2d");c.reset();c.fillStyle="#08121f";c.fillRect(0,0,width,height);c.strokeStyle="#85deff";c.lineWidth=3;c.beginPath();for(let i=0;i<page.history.length;i++){let x=10+i*(width-20)/39,y=height-10-(page.history[i]-15)/20*(height-20);if(i===0)c.moveTo(x,y);else c.lineTo(x,y)}c.stroke()}
        }
        Button {text:"清除本页曲线";font.pointSize:18;implicitHeight:64;onClicked:{page.history=[];chart.requestPaint()}}
    }
}
