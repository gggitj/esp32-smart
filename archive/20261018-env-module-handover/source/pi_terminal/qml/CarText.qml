import QtQuick
import QtQuick.Controls

Label {
    id:label
    property bool heading: false
    property bool hero: false
    property bool subdued: false
    property bool auditText: true
    font.pointSize: hero ? Theme.hero : heading ? Theme.title : Theme.body
    font.weight: heading || hero ? Font.DemiBold : Font.Normal
    color: {
        const window=ApplicationWindow.window
        return window && typeof window.textBackground==="function" ? Theme.textOn(window.textBackground(label)) : Theme.textOn(Theme.background)
    }
    wrapMode: Text.Wrap
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
