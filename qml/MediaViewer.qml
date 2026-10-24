import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

Dialog {
    id: viewer
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1000, parent.width - 48)
    height: Math.min(760, parent.height - 48)
    modal: true
    standardButtons: Dialog.Close
    property string kind: "photo"
    property url remote: ""
    property var gallery: []
    property int currentIndex: 0
    property real zoom: 1
    title: "Просмотр вложения"
    function present(url, type, name) {
        remote = url; kind = type; title = name || "Вложение"; zoom = 1
        open()
        if (kind !== "photo") player.play()
    }
    onClosed: { player.stop(); player.source = "" }
    onOpened: player.source = Qt.binding(function() { return viewer.kind === "photo" ? "" : (messenger.media.cached(viewer.remote).toString().length ? messenger.media.cached(viewer.remote) : viewer.remote) })
    contentItem: ColumnLayout {
        spacing: 12
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            Flickable {
                id: photoFlick
                anchors.fill: parent
                visible: viewer.kind === "photo"
                contentWidth: width * viewer.zoom
                contentHeight: height * viewer.zoom
                boundsBehavior: Flickable.StopAtBounds
                Image {
                    id: photograph
                    width: photoFlick.contentWidth; height: photoFlick.contentHeight
                    source: { messenger.media.revision; return messenger.media.source(viewer.remote) }
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    sourceSize.width: 2048; sourceSize.height: 2048
                }
                PinchHandler {
                    target: null
                    property real startZoom: 1
                    onActiveChanged: if (active) startZoom = viewer.zoom
                    onActiveScaleChanged: viewer.zoom = Math.max(1, Math.min(4, startZoom * activeScale))
                }
            }
            VideoOutput { id: video; anchors.fill: parent; visible: viewer.kind === "video" }
            Label { anchors.centerIn: parent; visible: viewer.kind === "audio" || viewer.kind === "audio_message"; text: "♫"; font.pixelSize: 100; color: "#0077ff" }
            BusyIndicator { anchors.centerIn: parent; running: viewer.kind === "photo" ? photograph.status === Image.Loading || !photograph.source.toString().length : player.mediaStatus === MediaPlayer.LoadingMedia; visible: running }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: viewer.kind !== "photo"
            Button { text: player.playbackState === MediaPlayer.PlayingState ? "Пауза" : "Слушать / смотреть"; onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
            Slider { Layout.fillWidth: true; from: 0; to: Math.max(1, player.duration); value: player.position; onMoved: player.setPosition(value) }
            Label { text: Math.floor(player.position / 60000) + ":" + String(Math.floor(player.position / 1000) % 60).padStart(2, "0") }
            ComboBox { model: ["1×", "1.5×", "2×"]; onActivated: player.playbackRate = [1, 1.5, 2][currentIndex] }
            Slider { from: 0; to: 1; value: sound.volume; Layout.preferredWidth: 90; onMoved: sound.volume = value; Accessible.name: "Громкость" }
        }
        RowLayout {
            visible: viewer.kind === "photo"
            Button { text: "−"; onClicked: viewer.zoom = Math.max(1, viewer.zoom - 0.25) }
            Button { text: "+"; onClicked: viewer.zoom = Math.min(4, viewer.zoom + 0.25) }
            Button { text: "По размеру окна"; onClicked: { viewer.zoom = 1; photoFlick.contentX = 0; photoFlick.contentY = 0 } }
        }
        Label { Layout.fillWidth: true; text: player.errorString; visible: viewer.kind !== "photo" && player.error !== MediaPlayer.NoError; color: "#bd604f"; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        Label { Layout.fillWidth: true; text: { messenger.media.revision; return messenger.media.error(viewer.remote) } visible: text.length > 0; color: "#bd604f"; wrapMode: Text.Wrap }
        RowLayout {
            Button { text: "Повторить загрузку"; visible: { messenger.media.revision; return messenger.media.error(viewer.remote).length > 0 } onClicked: messenger.media.retry(viewer.remote) }
            Button { text: "Сохранить…"; onClicked: saveFile.open() }
            Button { text: "Сохранить в кэше для офлайн-просмотра"; visible: viewer.kind !== "photo"; onClicked: messenger.media.source(viewer.remote) }
            Button { text: "Открыть во внешнем приложении"; onClicked: Qt.openUrlExternally(viewer.remote) }
        }
    }
    MediaPlayer { id: player; audioOutput: AudioOutput { id: sound } videoOutput: video }
    FileDialog { id: saveFile; title: "Сохранить вложение"; fileMode: FileDialog.SaveFile; onAccepted: messenger.transfers.download(viewer.remote, selectedFile) }
}
