import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool accent: false
    property bool auditControl: true
    property color buttonColor: !enabled ? Theme.disabledSurface : down ? Theme.pressed : accent || checked ? Theme.accent : Theme.raised
    property color textColor: Theme.textOn(buttonColor)
    font.pointSize: Theme.body
    leftPadding: Theme.gap; rightPadding: Theme.gap
    topPadding: Theme.gap; bottomPadding: Theme.gap
    implicitHeight: Math.max(Theme.hit, contentItem.implicitHeight + topPadding + bottomPadding)
    implicitWidth: Math.max(Theme.hit, contentItem.implicitWidth + leftPadding + rightPadding)
    focusPolicy: Qt.StrongFocus
    topInset:0;bottomInset:0;leftInset:0;rightInset:0
    Accessible.name: text
    background: Rectangle {
        radius: height / 2; color: control.buttonColor
        border.width: control.activeFocus ? 2 : 0
        border.color: control.activeFocus ? Theme.accent : Theme.outline
    }
    contentItem: Text {
        text: control.text; font: control.font; color: control.textColor
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
