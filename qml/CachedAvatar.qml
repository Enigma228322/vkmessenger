import QtQuick
import VK 1.0 as Native

Native.AvatarImage {
    id: picture
    property url remoteSource: ""
    function refresh() { source = remoteSource.toString().length ? messenger.media.source(remoteSource) : "" }
    onRemoteSourceChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: messenger.media
        function onSourceUpdated(remote) { if (remote.toString() === picture.remoteSource.toString()) picture.refresh() }
        function onSourcesReset() { picture.refresh() }
    }
}
