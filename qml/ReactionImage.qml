import QtQuick
import QtQuick.Controls

Item {
    id: root
    property url remoteSource
    readonly property bool ready: picture.status === Image.Ready
    readonly property bool failed: { return picture.status === Image.Error || picture.loadError.length > 0 }
    implicitWidth: 24
    implicitHeight: 24
    CachedImage {
        id: picture
        objectName: "reactionImage"
        anchors.fill: parent
        remoteSource: root.remoteSource
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        sourceSize.width: 80
        sourceSize.height: 80
    }
    Label {
        anchors.centerIn: parent
        visible: !root.ready
        text: root.failed ? "↻" : "···"
        font.pixelSize: 14
        color: palette.mid
    }
    TapHandler { enabled: root.failed; onTapped: messenger.media.retry(root.remoteSource) }
}
