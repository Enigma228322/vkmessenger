import QtQuick

QtObject {
    property bool dark: true
    readonly property color ink: dark ? "#e1e3e6" : "#222222"
    readonly property color muted: dark ? "#939393" : "#818c99"
    readonly property color accent: dark ? "#71aaeb" : "#2688eb"
    readonly property color surface: dark ? "#222222" : "#ffffff"
    readonly property color secondary: dark ? "#2c2c2c" : "#f0f2f5"
    readonly property color hover: dark ? "#343434" : "#e7ecf2"
    readonly property color pressed: dark ? "#3e3e3e" : "#dce3ec"
    readonly property color selected: dark ? "#363636" : "#e6edf6"
    readonly property color focusBorder: dark ? "#58789b" : "#8ebce9"
    readonly property color errorSurface: dark ? "#382827" : "#fff1e9"
    readonly property color errorText: dark ? "#f1aca3" : "#a84b21"
    readonly property color canvas: dark ? "#222222" : "#ffffff"
    readonly property color divider: dark ? "#363636" : "#e7e8ec"
    readonly property int messageWidth: 580
    readonly property int avatarSize: 36
    readonly property int bodySize: 15
    readonly property int hoverDuration: 180
    readonly property int pressDuration: 140
    readonly property int motionDuration: 240
    readonly property int panelDuration: 300
}
