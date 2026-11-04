import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

Dialog {
    id: viewer
    objectName: "mediaViewer"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1000, parent.width - 48)
    height: Math.min(760, parent.height - 48)
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: messenger.reduceMotion ? 0 : 240; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: messenger.reduceMotion ? 0 : 180; easing.type: Easing.OutCubic } }
    modal: true
    standardButtons: Dialog.Close
    property string kind: "photo"
    property url remote: ""
    property var gallery: []
    property int currentIndex: 0
    property real zoom: 1
    readonly property string photoError: {
        messenger.media.revision
        if (kind !== "photo") return ""
        if (!remote.toString().length) return "Ссылка на фотографию недоступна."
        const downloadError = messenger.media.error(remote)
        if (downloadError.length) return downloadError
        return photograph.status === Image.Error ? "Не удалось открыть фотографию. Попробуйте загрузить её заново." : ""
    }
    readonly property bool photoLoading: visible && kind === "photo" && !photoError.length
        && (photograph.status === Image.Loading || photograph.status === Image.Null)
    title: "Просмотр вложения"
    function resetPhotoView() {
        zoom = 1
        photoFlick.contentX = 0
        photoFlick.contentY = 0
    }
    function present(url, type, name) {
        gallery = []; currentIndex = 0
        remote = url; kind = type; title = name || "Вложение"
        resetPhotoView()
        open()
        if (kind !== "photo") player.play()
    }
    function presentGallery(photos, index) {
        if (!photos.length || index < 0 || index >= photos.length) return
        present(photos[index].source, "photo", photos[index].title)
        gallery = photos
        currentIndex = index
    }
    function showPhoto(index) {
        if (index < 0 || index >= gallery.length) return
        currentIndex = index
        remote = gallery[index].source
        title = gallery[index].title || "Фотография"
        resetPhotoView()
    }
    onClosed: { player.stop(); player.source = ""; gallery = []; currentIndex = 0 }
    onOpened: player.source = Qt.binding(function() { return viewer.kind === "photo" ? "" : (messenger.media.cached(viewer.remote).toString().length ? messenger.media.cached(viewer.remote) : viewer.remote) })
    Shortcut { sequence: "Left"; enabled: viewer.opened && viewer.kind === "photo" && viewer.gallery.length > 1; onActivated: viewer.showPhoto(viewer.currentIndex - 1) }
    Shortcut { sequence: "Right"; enabled: viewer.opened && viewer.kind === "photo" && viewer.gallery.length > 1; onActivated: viewer.showPhoto(viewer.currentIndex + 1) }
    contentItem: ColumnLayout {
        spacing: 12
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            Flickable {
                id: photoFlick
                objectName: "photoFlick"
                anchors.fill: parent
                visible: viewer.kind === "photo"
                contentWidth: width * viewer.zoom
                contentHeight: height * viewer.zoom
                boundsBehavior: Flickable.StopAtBounds
                Image {
                    id: photograph
                    objectName: "viewerPhotograph"
                    width: photoFlick.contentWidth; height: photoFlick.contentHeight
                    source: { messenger.media.revision; return viewer.visible && viewer.kind === "photo" ? messenger.media.source(viewer.remote) : "" }
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
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
            BusyIndicator { objectName: "viewerLoading"; anchors.centerIn: parent; running: viewer.kind === "photo" ? viewer.photoLoading : viewer.visible && player.mediaStatus === MediaPlayer.LoadingMedia; visible: running }
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
            Layout.fillWidth: true
            visible: viewer.kind === "photo" && viewer.gallery.length > 1
            Button { objectName: "viewerPrevious"; text: "← Назад"; Accessible.name: "Предыдущая фотография"; enabled: viewer.currentIndex > 0; onClicked: viewer.showPhoto(viewer.currentIndex - 1) }
            Label { objectName: "viewerPhotoCount"; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: (viewer.currentIndex + 1) + " из " + viewer.gallery.length; Accessible.name: "Фотография " + text }
            Button { objectName: "viewerNext"; text: "Вперёд →"; Accessible.name: "Следующая фотография"; enabled: viewer.currentIndex + 1 < viewer.gallery.length; onClicked: viewer.showPhoto(viewer.currentIndex + 1) }
        }
        RowLayout {
            visible: viewer.kind === "photo"
            Button { text: "−"; onClicked: viewer.zoom = Math.max(1, viewer.zoom - 0.25) }
            Button { text: "+"; onClicked: viewer.zoom = Math.min(4, viewer.zoom + 0.25) }
            Button { text: "По размеру окна"; onClicked: viewer.resetPhotoView() }
        }
        Label { Layout.fillWidth: true; text: player.errorString; visible: viewer.kind !== "photo" && player.error !== MediaPlayer.NoError; color: "#bd604f"; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        Label { objectName: "viewerError"; Layout.fillWidth: true; text: { messenger.media.revision; return viewer.kind === "photo" ? viewer.photoError : messenger.media.error(viewer.remote) } visible: text.length > 0; color: "#bd604f"; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        RowLayout {
            Button { objectName: "viewerRetry"; text: "Повторить загрузку"; visible: { messenger.media.revision; return viewer.kind === "photo" ? viewer.photoError.length > 0 && viewer.remote.toString().length > 0 : messenger.media.error(viewer.remote).length > 0 } onClicked: messenger.media.retry(viewer.remote) }
            Button { text: "Сохранить…"; onClicked: saveFile.open() }
            Button { text: "Сохранить в кэше для офлайн-просмотра"; visible: viewer.kind !== "photo"; onClicked: messenger.media.source(viewer.remote) }
            Button { text: "Открыть во внешнем приложении"; onClicked: Qt.openUrlExternally(viewer.remote) }
        }
    }
    MediaPlayer { id: player; audioOutput: AudioOutput { id: sound } videoOutput: video }
    FileDialog { id: saveFile; title: "Сохранить вложение"; fileMode: FileDialog.SaveFile; onAccepted: messenger.transfers.download(viewer.remote, selectedFile) }
}
