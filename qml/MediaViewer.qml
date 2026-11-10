import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import QtMultimedia
import VK 1.0 as Native

Dialog {
    id: viewer
    objectName: "mediaViewer"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: fullscreen ? parent.width : Math.min(1120, parent.width - 40)
    height: fullscreen ? parent.height : Math.min(840, parent.height - 40)
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    standardButtons: Dialog.NoButton
    property string kind: "photo"
    property url remote: ""
    property var gallery: []
    property int currentIndex: 0
    property real zoom: 1
    property bool fullscreen: false
    property bool switchingMedia: false
    property string cacheNotice: ""
    property int previousVisibility: Window.Windowed
    readonly property bool isPhoto: kind === "photo"
    readonly property bool isPlaying: player.playbackState === MediaPlayer.PlayingState
    readonly property bool canSave: remote.toString().length > 0 && !/\.(m3u8|mpd)(\?|$)/i.test(remote.toString())
    readonly property color ink: "#f2f5f9"
    readonly property color muted: "#9ba7b8"
    readonly property color accent: "#82b9ff"
    readonly property string photoError: !isPhoto ? "" : !remote.toString().length ? "Ссылка на фотографию недоступна."
        : photograph.loadError || (photograph.status === Image.Error ? "Не удалось открыть фотографию. Попробуйте загрузить её заново." : "")
    readonly property string playbackError: isPhoto ? "" : !remote.toString().length ? "Ссылка на вложение недоступна."
        : player.error !== MediaPlayer.NoError ? player.errorString || "Не удалось воспроизвести вложение." : ""
    readonly property string currentError: isPhoto ? photoError : playbackError
    readonly property bool photoLoading: visible && isPhoto && !photoError.length && (photograph.status === Image.Loading || photograph.status === Image.Null)
    readonly property bool mediaLoading: visible && !isPhoto && !playbackError.length
        && (player.mediaStatus === MediaPlayer.LoadingMedia || player.mediaStatus === MediaPlayer.BufferingMedia || player.mediaStatus === MediaPlayer.StalledMedia)
    title: "Просмотр вложения"
    palette.window: "#171d25"
    palette.base: "#202833"
    palette.button: "#28323f"
    palette.text: ink
    palette.buttonText: ink
    palette.windowText: ink
    palette.highlight: accent
    palette.highlightedText: "#10151d"
    Overlay.modal: Rectangle { color: "#d9070a10" }
    background: Rectangle { color: "#10151c"; radius: viewer.fullscreen ? 0 : 16; border.color: "#303945" }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: messenger.reduceMotion ? 0 : 160; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: messenger.reduceMotion ? 0 : 120; easing.type: Easing.OutCubic } }

    function timeText(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000))
        return (seconds >= 3600 ? Math.floor(seconds / 3600) + ":" : "")
            + (seconds >= 3600 ? String(Math.floor(seconds / 60) % 60).padStart(2, "0") : Math.floor(seconds / 60))
            + ":" + String(seconds % 60).padStart(2, "0")
    }
    function resetPhotoView() { zoom = 1; photoFlick.contentX = 0; photoFlick.contentY = 0 }
    function setZoom(value, x, y) {
        const px = x === undefined ? photoFlick.width / 2 : x, py = y === undefined ? photoFlick.height / 2 : y
        const fx = (photoFlick.contentX + px) / Math.max(1, photoFlick.contentWidth)
        const fy = (photoFlick.contentY + py) / Math.max(1, photoFlick.contentHeight)
        zoom = Math.max(1, Math.min(4, value))
        photoFlick.contentX = Math.max(0, Math.min(photoFlick.contentWidth - photoFlick.width, fx * photoFlick.contentWidth - px))
        photoFlick.contentY = Math.max(0, Math.min(photoFlick.contentHeight - photoFlick.height, fy * photoFlick.contentHeight - py))
    }
    function startPlayback() {
        player.stop(); player.source = ""
        if (isPhoto || !remote.toString().length || !visible) return
        const local = messenger.media.cached(remote)
        player.source = local.toString().length ? local : remote
        player.play()
    }
    function present(url, type, name) {
        switchingMedia = true
        cacheNotice = ""
        player.stop(); player.source = ""
        gallery = []; currentIndex = 0
        kind = type; remote = url; title = name || (type === "photo" ? "Фотография" : type === "video" ? "Видео" : "Аудио")
        resetPhotoView()
        player.playbackRate = 1; speed.currentIndex = 1
        open()
        switchingMedia = false
        if (!isPhoto) startPlayback()
    }
    function presentGallery(photos, index) {
        if (!photos.length || index < 0 || index >= photos.length) return
        present(photos[index].source, "photo", photos[index].title)
        gallery = photos; currentIndex = index
    }
    function showPhoto(index) {
        if (index < 0 || index >= gallery.length) return
        currentIndex = index; remote = gallery[index].source
        title = gallery[index].title || "Фотография"
        resetPhotoView()
    }
    function togglePlayback() {
        if (isPhoto || !remote.toString().length || playbackError.length) return
        if (isPlaying) player.pause()
        else { if (player.mediaStatus === MediaPlayer.EndOfMedia) player.setPosition(0); player.play() }
    }
    function seekTo(position) { if (player.seekable) player.setPosition(Math.max(0, Math.min(player.duration, position))) }
    function seekBy(delta) { seekTo(player.position + delta) }
    function toggleFullscreen() {
        const host = viewer.contentItem ? viewer.contentItem.Window.window : null
        if (!host) return
        if (fullscreen) { fullscreen = false; host.visibility = previousVisibility }
        else { previousVisibility = host.visibility; fullscreen = true; host.showFullScreen() }
    }
    function retryCurrent() { if (isPhoto) messenger.media.retry(remote); else startPlayback() }
    function updateCacheNotice() {
        if (!cacheNotice.length || isPhoto) return
        const error = messenger.media.error(remote)
        if (error.length) cacheNotice = error
        else if (messenger.media.cached(remote).toString().length) cacheNotice = "Файл сохранён для офлайн-просмотра"
    }
    function cacheForOffline() {
        cacheNotice = "Сохраняем для офлайн-просмотра…"
        if (messenger.media.error(remote).length) messenger.media.retry(remote)
        else messenger.media.source(remote)
        updateCacheNotice()
    }
    onAboutToHide: { player.stop(); player.source = ""; if (fullscreen) toggleFullscreen() }
    onClosed: { gallery = []; currentIndex = 0 }
    Connections {
        target: viewer.contentItem ? viewer.contentItem.Window.window : null
        function onVisibilityChanged() { if (viewer.fullscreen && target.visibility !== Window.FullScreen) viewer.fullscreen = false }
    }
    Connections {
        target: messenger.media
        function onSourceUpdated(remote) { if (remote.toString() === viewer.remote.toString()) viewer.updateCacheNotice() }
        function onSourcesReset() { viewer.cacheNotice = "" }
    }

    Shortcut { sequence: "Escape"; enabled: viewer.visible; onActivated: viewer.fullscreen ? viewer.toggleFullscreen() : viewer.close() }
    Shortcut { sequence: "Left"; enabled: viewer.opened; onActivated: viewer.isPhoto ? viewer.showPhoto(viewer.currentIndex - 1) : viewer.seekBy(-5000) }
    Shortcut { sequence: "Right"; enabled: viewer.opened; onActivated: viewer.isPhoto ? viewer.showPhoto(viewer.currentIndex + 1) : viewer.seekBy(5000) }
    Shortcut { sequence: "Home"; enabled: viewer.opened && viewer.isPhoto && viewer.gallery.length > 1; onActivated: viewer.showPhoto(0) }
    Shortcut { sequence: "End"; enabled: viewer.opened && viewer.isPhoto && viewer.gallery.length > 1; onActivated: viewer.showPhoto(viewer.gallery.length - 1) }
    Shortcut { sequence: "Space"; enabled: viewer.opened && !viewer.isPhoto; onActivated: viewer.togglePlayback() }
    Shortcut { sequence: "M"; enabled: viewer.opened && !viewer.isPhoto; onActivated: sound.muted = !sound.muted }
    Shortcut { sequence: "F"; enabled: viewer.opened; onActivated: viewer.toggleFullscreen() }
    Shortcut { sequences: ["+", "="]; enabled: viewer.opened && viewer.isPhoto; onActivated: viewer.setZoom(viewer.zoom + 0.25) }
    Shortcut { sequence: "-"; enabled: viewer.opened && viewer.isPhoto; onActivated: viewer.setZoom(viewer.zoom - 0.25) }
    Shortcut { sequence: "0"; enabled: viewer.opened && viewer.isPhoto; onActivated: viewer.resetPhotoView() }

    component MediaButton: ToolButton {
        id: button
        property string iconKind: ""
        property string label: text
        property bool primary: false
        implicitWidth: 40; implicitHeight: 40
        padding: 9
        hoverEnabled: true
        Accessible.name: label
        ToolTip.visible: hovered; ToolTip.text: label; ToolTip.delay: 600
        background: Rectangle {
            radius: Math.min(12, width / 2)
            color: button.primary ? (button.hovered ? "#9ac8ff" : viewer.accent) : button.down ? "#3a4655" : button.hovered ? "#2b3542" : "#1d2530"
            border.color: button.visualFocus ? viewer.accent : "#343e4c"
            opacity: button.enabled ? 1 : 0.4
        }
        contentItem: Item {
            opacity: button.enabled ? 1 : 0.35
            ActionIcon { anchors.centerIn: parent; width: 22; height: 22; visible: button.iconKind.length > 0; kind: button.iconKind; stroke: button.primary ? "#10151d" : viewer.ink }
            Text { anchors.centerIn: parent; visible: !button.iconKind.length; text: button.text; color: button.primary ? "#10151d" : viewer.ink; font.pixelSize: 13; font.weight: Font.Medium }
        }
    }
    component MediaSlider: Slider {
        id: slider
        implicitHeight: 24
        background: Rectangle {
            x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth; height: 4; radius: 2; color: "#3c4756"
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 2; color: viewer.accent }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.pressed ? 14 : 11; height: width; radius: width / 2
            color: slider.enabled ? viewer.ink : "#677585"
            border.color: slider.visualFocus ? viewer.accent : "transparent"; border.width: 2
        }
    }
    header: Item {
        implicitHeight: 72
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 16; spacing: 12
            ActionIcon { kind: viewer.isPhoto ? "photo" : viewer.kind === "video" ? "video" : "microphone"; stroke: viewer.accent; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Text { Layout.fillWidth: true; text: viewer.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: viewer.ink; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; elide: Text.ElideRight; color: viewer.muted; font.pixelSize: 12; text: viewer.isPhoto ? "Колесо — масштаб · двойной щелчок — приблизить" : viewer.cacheNotice || "Пробел — пауза · ← → — перемотка · F — полный экран" }
            }
            MediaButton { objectName: "viewerSave"; iconKind: "download"; label: "Сохранить файл"; enabled: viewer.canSave; onClicked: saveFile.open() }
            MediaButton {
                iconKind: "more"; label: "Другие действия"
                onClicked: moreActions.popup()
                Menu {
                    id: moreActions
                    background: Rectangle { color: "#202833"; radius: 10; border.color: "#394657" }
                    MenuItem { text: "Открыть во внешнем приложении"; enabled: viewer.remote.toString().length > 0; onTriggered: Qt.openUrlExternally(viewer.remote) }
                    MenuItem { text: "Сохранить в кэше для офлайн-просмотра"; visible: !viewer.isPhoto; height: visible ? implicitHeight : 0; enabled: viewer.canSave && viewer.remote.toString().indexOf("https:") === 0; onTriggered: viewer.cacheForOffline() }
                }
            }
            MediaButton { objectName: "viewerClose"; iconKind: "close"; label: "Закрыть просмотр"; onClicked: viewer.close() }
        }
    }
    contentItem: ColumnLayout {
        spacing: 0
        Item {
            id: stage
            objectName: "viewerStage"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            Rectangle { anchors.fill: parent; color: "#0a0e14" }
            Flickable {
                id: photoFlick
                objectName: "photoFlick"
                anchors.fill: parent
                anchors.leftMargin: viewer.gallery.length > 1 ? 64 : 20
                anchors.rightMargin: anchors.leftMargin
                anchors.topMargin: 12; anchors.bottomMargin: 12
                visible: viewer.isPhoto
                clip: true
                contentWidth: width * viewer.zoom; contentHeight: height * viewer.zoom
                boundsBehavior: Flickable.StopAtBounds
                CachedImage {
                    id: photograph
                    objectName: "viewerPhotograph"
                    width: photoFlick.contentWidth; height: photoFlick.contentHeight
                    active: viewer.visible && viewer.isPhoto && !viewer.switchingMedia; remoteSource: viewer.remote
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    sourceSize.width: 2048; sourceSize.height: 2048
                }
                PinchHandler {
                    target: null
                    property real startZoom: 1
                    onActiveChanged: if (active) startZoom = viewer.zoom
                    onActiveScaleChanged: viewer.setZoom(startZoom * activeScale, centroid.position.x, centroid.position.y)
                }
                WheelHandler {
                    target: null; acceptedDevices: PointerDevice.Mouse
                    onWheel: function(event) { if (event.angleDelta.y) viewer.setZoom(viewer.zoom * (event.angleDelta.y > 0 ? 1.15 : 1 / 1.15), event.x, event.y); event.accepted = true }
                }
                TapHandler { onDoubleTapped: function(point) { viewer.setZoom(viewer.zoom > 1 ? 1 : 2, point.position.x, point.position.y) } }
            }
            VideoOutput {
                id: video
                objectName: "viewerVideoOutput"
                anchors.fill: parent
                visible: viewer.kind === "video"
                fillMode: VideoOutput.PreserveAspectFit
                TapHandler {
                    exclusiveSignals: TapHandler.SingleTap | TapHandler.DoubleTap
                    onSingleTapped: viewer.togglePlayback()
                    onDoubleTapped: viewer.toggleFullscreen()
                }
            }
            Native.VideoFrameOutput {
                objectName: "viewerSoftwareVideoOutput"
                anchors.fill: parent
                videoSink: video.videoSink
                visible: viewer.kind === "video" && active
                TapHandler {
                    exclusiveSignals: TapHandler.SingleTap | TapHandler.DoubleTap
                    onSingleTapped: viewer.togglePlayback()
                    onDoubleTapped: viewer.toggleFullscreen()
                }
            }
            ActionIcon { anchors.centerIn: parent; width: 80; height: 80; visible: !viewer.isPhoto && viewer.kind !== "video"; kind: "microphone"; stroke: viewer.accent }
            MediaButton {
                anchors.centerIn: parent; width: 72; height: 72; primary: true
                iconKind: player.mediaStatus === MediaPlayer.EndOfMedia ? "retry" : "play"
                label: player.mediaStatus === MediaPlayer.EndOfMedia ? "Смотреть снова" : "Воспроизвести"
                visible: viewer.kind === "video" && !viewer.isPlaying && !viewer.mediaLoading && !viewer.currentError.length
                onClicked: viewer.togglePlayback()
            }
            MediaButton { objectName: "viewerPrevious"; anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; width: 44; height: 52; iconKind: "chevronLeft"; label: "Предыдущая фотография"; visible: viewer.isPhoto && viewer.gallery.length > 1; enabled: viewer.currentIndex > 0; onClicked: viewer.showPhoto(viewer.currentIndex - 1) }
            MediaButton { objectName: "viewerNext"; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; width: 44; height: 52; iconKind: "chevronRight"; label: "Следующая фотография"; visible: viewer.isPhoto && viewer.gallery.length > 1; enabled: viewer.currentIndex + 1 < viewer.gallery.length; onClicked: viewer.showPhoto(viewer.currentIndex + 1) }
            Column {
                anchors.centerIn: parent; spacing: 12
                visible: viewer.photoLoading || viewer.mediaLoading
                BusyIndicator { objectName: "viewerLoading"; anchors.horizontalCenter: parent.horizontalCenter; running: viewer.photoLoading || viewer.mediaLoading; visible: running }
                Text { text: viewer.isPhoto ? "Загрузка фотографии…" : "Загрузка…"; color: viewer.muted; font.pixelSize: 13 }
            }
            Column {
                anchors.centerIn: parent; width: Math.min(440, parent.width - 80); spacing: 18
                visible: viewer.currentError.length > 0
                ActionIcon { anchors.horizontalCenter: parent.horizontalCenter; width: 40; height: 40; kind: "retry"; stroke: "#e8a699" }
                Text { objectName: "viewerError"; width: parent.width; text: viewer.currentError; visible: text.length > 0; color: viewer.ink; font.pixelSize: 15; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; textFormat: Text.PlainText }
                MediaButton { objectName: "viewerRetry"; anchors.horizontalCenter: parent.horizontalCenter; implicitWidth: 190; text: "Повторить загрузку"; primary: true; visible: viewer.currentError.length > 0 && viewer.remote.toString().length > 0; onClicked: viewer.retryCurrent() }
            }
        }
        Item {
            Layout.fillWidth: true; Layout.preferredHeight: visible ? 72 : 0
            visible: viewer.isPhoto && viewer.gallery.length > 1
            ListView {
                id: filmstrip
                objectName: "viewerThumbnails"
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, viewer.gallery.length * 80 - 8); height: 56
                orientation: ListView.Horizontal
                spacing: 8; clip: true
                cacheBuffer: 0
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: viewer.visible && viewer.isPhoto && viewer.gallery.length > 1 ? viewer.gallery : []
                currentIndex: viewer.currentIndex
                function revealCurrent() {
                    if (count && viewer.visible) positionViewAtIndex(viewer.currentIndex, ListView.Contain)
                }
                onCurrentIndexChanged: Qt.callLater(revealCurrent)
                onCountChanged: Qt.callLater(revealCurrent)
                onWidthChanged: Qt.callLater(revealCurrent)
                delegate: Button {
                    id: miniature
                    objectName: "viewerThumbnail"
                    required property var modelData
                    required property int index
                    property bool pooled: false
                    ListView.onPooled: pooled = true
                    ListView.onReused: pooled = false
                    readonly property bool selected: index === viewer.currentIndex
                    readonly property bool inViewport: x + width > filmstrip.contentX && x < filmstrip.contentX + filmstrip.width
                    width: 72; height: 56; padding: 3
                    focusPolicy: Qt.NoFocus
                    Accessible.name: "Фотография " + (index + 1) + " из " + viewer.gallery.length + (modelData.title ? ": " + modelData.title : "")
                    Accessible.role: Accessible.Button
                    Accessible.checkable: true
                    Accessible.checked: selected
                    ToolTip.visible: hovered; ToolTip.text: modelData.title || "Фотография " + (index + 1); ToolTip.delay: 600
                    background: Rectangle {
                        radius: 8
                        color: miniature.hovered ? "#2b3542" : "#1d2530"
                        border.width: miniature.selected ? 2 : 1
                        border.color: miniature.selected ? viewer.accent : "#343e4c"
                    }
                    contentItem: Item {
                        clip: true
                        ActionIcon { anchors.centerIn: parent; width: 20; height: 20; kind: "photo"; stroke: viewer.muted; visible: preview.status !== Image.Ready }
                        CachedImage {
                            id: preview
                            objectName: "viewerThumbnailImage"
                            anchors.fill: parent
                            remoteSource: miniature.modelData.thumbnail || miniature.modelData.source || ""
                            active: viewer.visible && viewer.isPhoto && miniature.inViewport && !miniature.pooled
                            sourceSize.width: 160; sourceSize.height: 160
                            fillMode: Image.PreserveAspectCrop
                        }
                    }
                    onClicked: if (!selected) viewer.showPhoto(index)
                }
            }
        }
        Item {
            Layout.fillWidth: true; Layout.preferredHeight: viewer.isPhoto ? 66 : 98
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20; spacing: 8
                visible: viewer.isPhoto
                MediaButton { objectName: "viewerZoomOut"; iconKind: "zoomOut"; label: "Уменьшить"; enabled: viewer.zoom > 1; onClicked: viewer.setZoom(viewer.zoom - 0.25) }
                MediaButton { objectName: "viewerZoomIn"; iconKind: "zoomIn"; label: "Увеличить"; enabled: viewer.zoom < 4 && photograph.status === Image.Ready; onClicked: viewer.setZoom(viewer.zoom + 0.25) }
                Text { Layout.preferredWidth: 48; text: Math.round(viewer.zoom * 100) + "%"; color: viewer.ink; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter }
                MediaButton { objectName: "viewerFit"; iconKind: "fit"; label: "По размеру окна"; onClicked: viewer.resetPhotoView() }
                Item { Layout.fillWidth: true }
                Text { objectName: "viewerPhotoCount"; text: (viewer.currentIndex + 1) + " из " + Math.max(1, viewer.gallery.length); visible: viewer.gallery.length > 1; color: viewer.muted; font.pixelSize: 13; Accessible.name: "Фотография " + text }
                Item { Layout.fillWidth: true }
                MediaButton { objectName: "viewerPhotoFullscreen"; iconKind: viewer.fullscreen ? "collapse" : "expand"; label: viewer.fullscreen ? "Выйти из полного экрана" : "На весь экран"; onClicked: viewer.toggleFullscreen() }
            }
            ColumnLayout {
                anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.topMargin: 8; anchors.bottomMargin: 12; spacing: 2
                visible: !viewer.isPhoto
                MediaSlider {
                    id: seek
                    objectName: "viewerSeek"
                    Layout.fillWidth: true
                    from: 0; to: Math.max(1, player.duration)
                    enabled: player.seekable && player.duration > 0 && !viewer.playbackError.length
                    Accessible.name: "Позиция воспроизведения"
                    onMoved: viewer.seekTo(value)
                    Binding { target: seek; property: "value"; value: player.position; when: !seek.pressed }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    MediaButton { objectName: "viewerPlayPause"; iconKind: viewer.isPlaying ? "pause" : player.mediaStatus === MediaPlayer.EndOfMedia ? "retry" : "play"; label: viewer.isPlaying ? "Пауза" : "Воспроизвести"; primary: true; enabled: viewer.remote.toString().length > 0 && !viewer.playbackError.length; onClicked: viewer.togglePlayback() }
                    MediaButton { objectName: "viewerSkipBack"; iconKind: "skipBack"; label: "Назад на 10 секунд"; enabled: player.seekable; onClicked: viewer.seekBy(-10000) }
                    MediaButton { objectName: "viewerSkipForward"; iconKind: "skipForward"; label: "Вперёд на 10 секунд"; enabled: player.seekable; onClicked: viewer.seekBy(10000) }
                    Text { objectName: "viewerPlaybackTime"; text: viewer.timeText(player.position) + " / " + viewer.timeText(player.duration); color: viewer.muted; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    MediaButton { objectName: "viewerMute"; iconKind: sound.muted || sound.volume === 0 ? "volumeOff" : "volume"; label: sound.muted ? "Включить звук" : "Выключить звук"; onClicked: sound.muted = !sound.muted }
                    MediaSlider { objectName: "viewerVolume"; Layout.preferredWidth: 80; visible: viewer.width > 820; from: 0; to: 1; value: sound.volume; Accessible.name: "Громкость"; onMoved: { sound.volume = value; sound.muted = false } }
                    ComboBox { id: speed; objectName: "viewerSpeed"; Layout.preferredWidth: 78; implicitHeight: 36; model: ["0.5×", "1×", "1.25×", "1.5×", "2×"]; currentIndex: 1; Accessible.name: "Скорость воспроизведения"; onActivated: player.playbackRate = [0.5, 1, 1.25, 1.5, 2][currentIndex] }
                    MediaButton { objectName: "viewerFullscreen"; iconKind: viewer.fullscreen ? "collapse" : "expand"; label: viewer.fullscreen ? "Выйти из полного экрана" : "На весь экран"; onClicked: viewer.toggleFullscreen() }
                }
            }
        }
    }
    MediaPlayer {
        id: player; objectName: "viewerPlayer"
        audioOutput: AudioOutput { id: sound; objectName: "viewerAudioOutput"; volume: 0.8 }
        videoOutput: video
    }
    FileDialog { id: saveFile; title: "Сохранить вложение"; fileMode: FileDialog.SaveFile; onAccepted: messenger.transfers.download(viewer.remote, selectedFile) }
}
