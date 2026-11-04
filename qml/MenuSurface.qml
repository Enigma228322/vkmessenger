import QtQuick

Item {
    id: surface
    property color color: "#222222"
    property color borderColor: "#363636"
    implicitWidth: 240
    implicitHeight: 44
    // Keep the surface visible with both hardware and software rendering.
    Rectangle { x: -6; y: -2; width: parent.width + 12; height: parent.height + 14; radius: 20; color: "#06000000" }
    Rectangle { x: -3; y: 1; width: parent.width + 6; height: parent.height + 8; radius: 17; color: "#0a000000" }
    Rectangle { x: -1; y: 3; width: parent.width + 2; height: parent.height + 3; radius: 15; color: "#14000000" }
    Rectangle { anchors.fill: parent; radius: 14; color: surface.color; border.color: surface.borderColor }
}
