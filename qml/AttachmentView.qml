import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import VK 1.0 as Native

Column {
    id: root
    property var attachments: []
    property bool animationsEnabled: true
    property bool loadImages: true
    property color photoBackground: messenger.darkTheme ? "#222222" : "white"
    readonly property var photos: attachments.filter(function(a) { return a.type === "photo" })
    readonly property var otherAttachments: attachments.filter(function(a) { return a.type !== "photo" })
    Layout.fillHeight: false
    spacing: 6
    function payload(attachment) { return attachment[attachment.type] || {} }
    function imageUrl(attachment) {
        const p = payload(attachment)
        if (attachment.type === "photo") {
            const sizes = p.sizes || []
            return sizes.length ? sizes[sizes.length - 1].url : ""
        }
        if (attachment.type === "sticker") {
            const images = p.images || []
            return images.length ? images[images.length - 1].url : ""
        }
        return ""
    }
    function animationUrl(attachment) {
        if (attachment.type !== "sticker") return ""
        const p = payload(attachment)
        const animations = p.animations || []
        for (const animation of animations) if (animation.type === "light" && animation.url) return animation.url
        return p.animation_url || (animations.length ? animations[0].url || "" : "")
    }
    function rasterAnimation(attachment) { return /\.(gif|webp|apng)(\?|$)/i.test(animationUrl(attachment)) }
    function photoHeight(attachment, width) {
        const sizes = payload(attachment).sizes || []
        const size = sizes.length ? sizes[sizes.length - 1] : null
        return Math.min(260, size && size.width > 0 ? width * size.height / size.width : 180)
    }
    function url(attachment) {
        const p = payload(attachment)
        if (attachment.type === "photo" || attachment.type === "sticker") return imageUrl(attachment)
        if (attachment.type === "audio_message") return p.link_mp3 || p.link_ogg || ""
        if (attachment.type === "audio") return p.url || ""
        if (attachment.type === "video") return p.player || ("https://vk.ru/video" + p.owner_id + "_" + p.id)
        return p.url || ""
    }
    function title(attachment) {
        const p = payload(attachment)
        return p.title || p.text || ({photo: "Фотография", sticker: "Стикер", audio_message: "Голосовое сообщение", video: "Видео", doc: "Документ", link: "Ссылка", audio: "Аудио", wall: "Запись", poll: "Опрос"}[attachment.type] || attachment.type)
    }
    function videoPreview(attachment) {
        const video = payload(attachment), images = video.image || video.first_frame || []
        let best = null
        const target = Math.max(320, width * 2)
        for (const image of images) {
            if (!image.url) continue
            if (!best || (best.width < target && image.width > best.width) || (image.width >= target && image.width < best.width)) best = image
        }
        return best ? best.url : ""
    }
    function videoDuration(attachment) {
        const seconds = Math.max(0, Number(payload(attachment).duration || 0))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
    PhotoGrid {
        id: photoGrid
        width: root.width
        height: photoGrid.implicitHeight
        visible: root.photos.length > 0
        photos: root.photos; siblings: root.attachments
        loadImages: root.loadImages; cornerColor: root.photoBackground
    }
    Repeater {
        model: root.otherAttachments
        delegate: ColumnLayout {
            id: row
            required property var modelData
            width: root.width
            spacing: 4
            Loader {
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Math.min(260, Math.max(140, root.width * 9 / 16)) : 0
                active: row.modelData.type === "video"
                visible: active
                sourceComponent: Component {
                    ItemDelegate {
                        id: videoCard
                        objectName: "videoAttachmentOpen"
                        padding: 0
                        Accessible.name: "Смотреть видео: " + root.title(row.modelData)
                        background: Rectangle { color: "#182331"; radius: 10 }
                        contentItem: Item {
                            clip: true
                            CachedImage { anchors.fill: parent; active: videoCard.visible && root.loadImages; remoteSource: root.videoPreview(row.modelData); fillMode: Image.PreserveAspectCrop; sourceSize.width: 960; sourceSize.height: 540 }
                            Rectangle { anchors.fill: parent; gradient: Gradient { GradientStop { position: 0; color: "#00000000" } GradientStop { position: 1; color: "#99000000" } } }
                            Rectangle { anchors.centerIn: parent; width: 54; height: 54; radius: 27; color: videoCard.down ? "#d9ffffff" : videoCard.hovered ? "#f5ffffff" : "#e6ffffff"; ActionIcon { anchors.centerIn: parent; width: 25; height: 25; kind: "play"; stroke: "#182331" } }
                            Rectangle {
                                anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10
                                width: duration.implicitWidth + 12; height: 24; radius: 6; color: "#b3000000"
                                Text { id: duration; anchors.centerIn: parent; text: root.videoDuration(row.modelData); color: "white"; font.pixelSize: 12 }
                            }
                            Rectangle { anchors.fill: parent; color: "transparent"; border.color: videoCard.visualFocus ? "#82b9ff" : "#203344"; border.width: videoCard.visualFocus ? 2 : 1 }
                        }
                        onClicked: messenger.actions.openAttachment(row.modelData, root.attachments)
                    }
                }
            }
            Item {
                id: imageFrame
                property bool sticker: row.modelData.type === "sticker"
                property bool paused: false
                readonly property url rasterRemote: root.rasterAnimation(row.modelData) ? root.animationUrl(row.modelData) : root.imageUrl(row.modelData)
                readonly property url lottieRemote: sticker && !root.rasterAnimation(row.modelData) ? root.animationUrl(row.modelData) : ""
                property url rasterLocal: ""
                property url lottieLocal: ""
                property string loadError: ""
                function refreshSources() {
                    const active = root.loadImages && visible
                    rasterLocal = active && rasterRemote.toString().length ? messenger.media.source(rasterRemote) : ""
                    lottieLocal = active && lottieRemote.toString().length ? messenger.media.source(lottieRemote) : ""
                    loadError = active ? messenger.media.error(rasterRemote) || messenger.media.error(lottieRemote) : ""
                }
                onRasterRemoteChanged: refreshSources()
                onLottieRemoteChanged: refreshSources()
                onVisibleChanged: refreshSources()
                Component.onCompleted: refreshSources()
                Connections {
                    target: root
                    function onLoadImagesChanged() { imageFrame.refreshSources() }
                }
                Connections {
                    target: messenger.media
                    function onSourceUpdated(remote) {
                        if (remote.toString() === imageFrame.rasterRemote.toString() || remote.toString() === imageFrame.lottieRemote.toString()) imageFrame.refreshSources()
                    }
                    function onSourcesReset() { imageFrame.refreshSources() }
                }
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: sticker ? Math.min(208, root.width) : root.width
                Layout.preferredHeight: visible ? (sticker ? Math.min(208, root.width) : root.photoHeight(row.modelData, root.width)) : 0
                Layout.fillHeight: false
                visible: root.imageUrl(row.modelData).length > 0 || root.animationUrl(row.modelData).length > 0
                AnimatedImage {
                    id: raster
                    objectName: "attachmentImage"
                    anchors.fill: parent
                    source: imageFrame.rasterLocal
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: !root.rasterAnimation(row.modelData)
                    sourceSize.width: imageFrame.sticker ? 416 : 1120
                    sourceSize.height: imageFrame.sticker ? 416 : 1040
                    playing: frameCount > 1 && root.animationsEnabled && root.loadImages && !imageFrame.paused
                    paused: !root.animationsEnabled || imageFrame.paused
                    visible: !animation.ready
                }
                Native.LottieSticker {
                    id: animation
                    anchors.fill: parent
                    source: imageFrame.lottieLocal
                    playing: root.animationsEnabled && root.loadImages && !imageFrame.paused
                    visible: source.toString().length > 0
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.animationUrl(row.modelData).length) imageFrame.paused = !imageFrame.paused
                        else messenger.actions.openAttachment(row.modelData, root.attachments)
                    }
                }
                ToolButton {
                    objectName: "retrySticker"
                    anchors.centerIn: parent
                    visible: root.loadImages && !animation.ready && (raster.status === Image.Error || imageFrame.loadError.length > 0 || animation.error.length > 0)
                    text: "Повторить загрузку"
                    Accessible.name: "Повторить загрузку стикера"
                    onClicked: {
                        if (imageFrame.rasterRemote.toString().length) messenger.media.retry(imageFrame.rasterRemote)
                        if (imageFrame.lottieRemote.toString().length) messenger.media.retry(imageFrame.lottieRemote)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                visible: row.modelData.type !== "sticker"
                Label { text: root.title(row.modelData); Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; textFormat: Text.PlainText }
                Button {
                    objectName: "attachmentOpen"
                    text: row.modelData.type === "audio_message" || row.modelData.type === "audio" ? "▶" : row.modelData.type === "video" ? "Смотреть" : "Открыть"
                    enabled: root.url(row.modelData).length > 0
                    onClicked: messenger.actions.openAttachment(row.modelData, root.attachments)
                }
                Button { text: "В VK"; visible: row.modelData.type === "video"; enabled: root.url(row.modelData).length > 0; onClicked: Qt.openUrlExternally(root.url(row.modelData)) }
                Button {
                    text: "↓"
                    Accessible.name: "Сохранить вложение"
                    enabled: root.url(row.modelData).length > 0 && row.modelData.type !== "video"
                    onClicked: { saveFile.remote = root.url(row.modelData); saveFile.open() }
                }
            }
        }
    }
    FileDialog {
        id: saveFile
        property string remote: ""
        title: "Сохранить вложение"
        fileMode: FileDialog.SaveFile
        onAccepted: messenger.transfers.download(remote, selectedFile)
    }
}
