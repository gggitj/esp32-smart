import QtQuick
import QtQuick.Controls

TextField {
    id: field
    property bool auditControl: true
    font.pointSize: Theme.body
    color: Theme.textOn(Theme.background); placeholderTextColor: color
    selectionColor: Theme.accent; selectedTextColor: Theme.textOn(selectionColor)
    padding: Theme.gap
    implicitHeight: Math.max(Theme.hit, contentHeight + padding * 2)
    background: Rectangle {
        color: Theme.background; radius: 12 * Theme.scaleFactor
        border.color: field.activeFocus ? Theme.accent : Theme.outline
        border.width: field.activeFocus ? 3 : 1
    }
    Accessible.name: placeholderText
}
