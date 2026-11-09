import QtQuick

Image {
    id: picture
    property url remoteSource: ""
    property bool active: true
    property string loadError: ""
    asynchronous: true
    function refresh() {
        source = active && remoteSource.toString().length ? messenger.media.source(remoteSource) : ""
        loadError = active ? messenger.media.error(remoteSource) : ""
    }
    onRemoteSourceChanged: refresh()
    onActiveChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: messenger.media
        function onSourceUpdated(remote) { if (remote.toString() === picture.remoteSource.toString()) picture.refresh() }
        function onSourcesReset() { picture.refresh() }
    }
}
