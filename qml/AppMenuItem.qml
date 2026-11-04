import QtQuick
import QtQuick.Controls

MenuItem {
    id: entry
    property string kind: ""
    property bool destructive: false
    hoverEnabled: true
    implicitHeight: 42
    leftPadding: kind.length ? 44 : 14
    rightPadding: 14
    font.pixelSize: 14
    ActionIcon {
        x: 14; y: (entry.height - height) / 2
        width: 22; height: 22
        visible: entry.kind.length > 0
        kind: entry.kind
        stroke: entry.destructive ? "#ff6b6b" : entry.palette.placeholderText
        opacity: entry.enabled ? 1 : 0.45
    }
    background: Rectangle {
        implicitWidth: 220
        implicitHeight: 42
        radius: 8
        color: entry.down ? entry.palette.midlight : entry.highlighted ? entry.palette.light : "transparent"
        Behavior on color { enabled: !messenger.reduceMotion; ColorAnimation { duration: 160; easing.type: Easing.OutCubic } }
    }
}
