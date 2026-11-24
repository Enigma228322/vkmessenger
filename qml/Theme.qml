import QtQuick

QtObject {
    property bool dark: true
    readonly property color ink: dark ? "#ecf3ff" : "#20334d"
    readonly property color muted: dark ? "#9aacc4" : "#61748e"
    readonly property color accent: dark ? "#91c4ff" : "#2173d8"
    readonly property color surface: dark ? "#152238" : "#f1f6fd"
    readonly property color secondary: dark ? "#1b2b43" : "#e4edf8"
    readonly property color hover: dark ? "#2c4562" : "#dce9f7"
    readonly property color pressed: dark ? "#36516f" : "#ccdef1"
    readonly property color selected: dark ? "#29435f" : "#d9e9fc"
    readonly property color focusBorder: dark ? "#8bc5ff" : "#3e88d9"
    readonly property color errorSurface: dark ? "#492f3a" : "#fff0ed"
    readonly property color errorText: dark ? "#ffc2b9" : "#a84431"
    readonly property color canvas: dark ? "#0b1423" : "#e9f1fb"
    readonly property color divider: dark ? "#33445c" : "#d0dcea"
    readonly property color glassTint: dark ? "#c2142338" : "#cceef5ff"
    readonly property color glassBorder: dark ? "#587f9ebb" : "#c9ffffff"
    readonly property color glassHighlight: dark ? "#8eb7daf9" : "#f2ffffff"
    readonly property color ambientBlue: dark ? "#643a78c7" : "#889bc7fa"
    readonly property color ambientTeal: dark ? "#4d248f9c" : "#708fe2df"
    readonly property color ambientViolet: dark ? "#4d7767bc" : "#6cb9b3f4"
    readonly property color glassInput: dark ? "#a60c192c" : "#b3ffffff"
    readonly property color glassSelection: dark ? "#665f9bd8" : "#7db9d9ff"
    readonly property int messageWidth: 580
    readonly property int avatarSize: 36
    readonly property int bodySize: 15
    readonly property int hoverDuration: 180
    readonly property int pressDuration: 140
    readonly property int motionDuration: 240
    readonly property int panelDuration: 300
}
