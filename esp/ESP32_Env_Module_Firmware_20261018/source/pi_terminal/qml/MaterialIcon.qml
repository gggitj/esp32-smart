import QtQuick
import QtQuick.Window
import QtQuick.Controls

Image {
    id:icon
    property string name: "apps"
    property bool light: true
    readonly property var available: ["apps","language","folder","settings","extension","description","image","text_snippet","code","archive","close","arrow_back","arrow_forward","chevron_left","chevron_right","notifications","wifi","wifi_off","lan","bluetooth","bluetooth_disabled","battery_full","battery_unknown","battery_0_bar","battery_1_bar","battery_2_bar","battery_3_bar","battery_4_bar","battery_5_bar","battery_6_bar","battery_charging_full","power","tune","volume_up","volume_off","memory","storage","refresh","check","fullscreen","close_fullscreen","minimize","search","view_list"]
    readonly property color contrastInk: {
        const window=ApplicationWindow.window
        return window && typeof window.textBackground==="function" ? Theme.textOn(window.textBackground(icon)) : (light?"#ffffff":"#000000")
    }
    source: systemTools.monochromeIcon(available.indexOf(name)>=0?name:"extension",contrastInk.r>0.5)
    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
    fillMode: Image.PreserveAspectFit
    smooth:true;antialiasing:true
    Accessible.ignored:true
}
