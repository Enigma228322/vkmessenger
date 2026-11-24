import QtQuick

Item {
    id: glass
    property bool dark: true
    property bool elevated: true
    property color color: dark ? "#c2142338" : "#cceef5ff"
    property color borderColor: dark ? "#587f9ebb" : "#c9ffffff"
    property color highlightColor: dark ? "#8eb7daf9" : "#f2ffffff"
    property real radius: 24
    readonly property real corner: Math.max(0, Math.min(radius, width / 2, height / 2))

    // Simple translucent layers keep the material usable with the software
    // renderer as well as Metal, without a live backdrop capture per surface.
    Rectangle {
        visible: glass.elevated
        x: -8; y: 2; width: glass.width + 16; height: glass.height + 18
        radius: glass.corner + 8
        color: glass.dark ? "#09030a14" : "#050f2948"
    }
    Rectangle {
        visible: glass.elevated
        x: -3; y: 4; width: glass.width + 6; height: glass.height + 10
        radius: glass.corner + 3
        color: glass.dark ? "#15030914" : "#0a25466d"
    }
    Rectangle {
        visible: glass.elevated
        x: 0; y: 6; width: glass.width; height: glass.height
        radius: glass.corner
        color: glass.dark ? "#26020813" : "#11223c60"
    }
    Rectangle {
        anchors.fill: parent
        radius: glass.corner
        color: glass.color
        border.width: 1
        border.color: glass.borderColor
    }
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: Math.max(0, glass.corner - 1)
        gradient: Gradient {
            GradientStop { position: 0; color: glass.dark ? "#18ffffff" : "#75ffffff" }
            GradientStop { position: 0.32; color: glass.dark ? "#04ffffff" : "#0dffffff" }
            GradientStop { position: 0.72; color: "transparent" }
            GradientStop { position: 1; color: glass.dark ? "#0b3b6aa0" : "#123f7dc5" }
        }
        border.width: 1
        border.color: Qt.rgba(glass.highlightColor.r, glass.highlightColor.g,
                              glass.highlightColor.b, glass.highlightColor.a * 0.22)
    }
    Rectangle {
        x: Math.max(2, glass.corner * 0.7); y: 1
        width: Math.max(0, glass.width - x * 2); height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.28; color: glass.highlightColor }
            GradientStop { position: 0.72; color: glass.highlightColor }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    Rectangle {
        x: 1; y: Math.max(2, glass.corner * 0.7)
        width: 1; height: Math.max(0, glass.height - y * 2)
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.2; color: Qt.rgba(glass.highlightColor.r, glass.highlightColor.g,
                                                      glass.highlightColor.b, glass.highlightColor.a * 0.55) }
            GradientStop { position: 0.85; color: "transparent" }
        }
    }
}
