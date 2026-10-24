import QtQuick

QtObject {
    property bool dark: true
    readonly property color ink: dark ? "#e1e3e6" : "#222222"
    readonly property color muted: dark ? "#939393" : "#818c99"
    readonly property color accent: dark ? "#71aaeb" : "#2688eb"
    readonly property color surface: dark ? "#222222" : "#ffffff"
    readonly property color secondary: dark ? "#2c2c2c" : "#f0f2f5"
    readonly property color canvas: dark ? "#222222" : "#ffffff"
    readonly property color divider: dark ? "#363636" : "#e7e8ec"
    readonly property int messageWidth: 580
    readonly property int avatarSize: 36
    readonly property int bodySize: 15
    readonly property int motionDuration: 160
}
