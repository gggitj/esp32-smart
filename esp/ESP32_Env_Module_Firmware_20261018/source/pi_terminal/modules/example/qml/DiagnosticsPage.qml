import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    property var sessionModel: null
    property real uiScale: 1
    spacing: 16 * uiScale
    Label { text: "模块数据诊断"; color: "#f4f8ff"; font.pointSize: 24 * uiScale; wrapMode:Text.Wrap; Layout.fillWidth:true }
    Label {
        text: "此页面来自插件包。数据对象由宿主按会话隔离，卸载页面后再销毁。"
        color: "#c0d2e5"; font.pointSize: 18 * uiScale; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
    TextArea {
        readOnly: true; text: sessionModel ? JSON.stringify(sessionModel.data, null, 2) : "等待数据"
        font.pointSize: 18 * uiScale; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.fillHeight: true
        color:"#f4f8ff";selectionColor:"#85deff";selectedTextColor:"#081b29"
        background:Rectangle {color:"#08121f"}
    }
}
