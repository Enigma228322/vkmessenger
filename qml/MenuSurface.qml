import QtQuick

GlassSurface {
    id: surface
    dark: (color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722) < 0.5
    color: "#c2142338"
    borderColor: dark ? "#587f9ebb" : "#c9ffffff"
    highlightColor: dark ? "#8eb7daf9" : "#f2ffffff"
    radius: 18
    implicitWidth: 240
    implicitHeight: 44
}
