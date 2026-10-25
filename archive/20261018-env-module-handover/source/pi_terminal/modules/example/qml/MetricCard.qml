import QtQuick
import QtQuick.Controls

Rectangle {
    id: card
    property string title: ""
    property string value: "—"
    property string suffix: ""
    property string detail: ""
    property color tint: "#72e3ff"
    property real uiScale: 1
    implicitHeight: 76 * uiScale
    radius: 11 * uiScale
    color: "#0c1d2b"
    border.color: "#1d3a4e"

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3 * card.uiScale
        radius: width / 2
        color: card.tint
        opacity: 0.9
    }
    Column {
        anchors.fill: parent
        anchors.leftMargin: 11 * card.uiScale
        anchors.rightMargin: 8 * card.uiScale
        anchors.topMargin: 7 * card.uiScale
        anchors.bottomMargin: 6 * card.uiScale
        spacing: 1 * card.uiScale
        Label { text: card.title; color: "#8fa9bb"; font.pixelSize: 10 * card.uiScale; elide: Text.ElideRight; width: parent.width }
        Row {
            spacing: 4 * card.uiScale
            Label { text: card.value; color: "#f0f7ff"; font.pixelSize: 20 * card.uiScale; font.bold: true }
            Label { text: card.suffix; visible: card.suffix.length > 0; color: card.tint; font.pixelSize: 10 * card.uiScale; anchors.baseline: parent.children[0].baseline }
        }
        Label { text: card.detail; color: "#607d91"; font.pixelSize: 9 * card.uiScale; elide: Text.ElideRight; width: parent.width }
    }
}
