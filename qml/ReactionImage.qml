import QtQuick
import QtQuick.Controls

Item {
    id: root
    property url remoteSource
    readonly property bool ready: picture.status === Image.Ready
    readonly property bool failed: { messenger.media.revision; return picture.status === Image.Error || (remoteSource.toString().length > 0 && messenger.media.error(remoteSource).length > 0) }
    implicitWidth: 24
    implicitHeight: 24
    Image {
        id: picture
        objectName: "reactionImage"
        anchors.fill: parent
        source: { messenger.media.revision; return messenger.media.source(root.remoteSource) }
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
