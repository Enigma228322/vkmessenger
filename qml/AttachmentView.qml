import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import VK 1.0

ColumnLayout {
    id: root
    property var attachments: []
    property bool animationsEnabled: true
    Layout.fillHeight: false
    spacing: 6
    function cachedSource(url) { messenger.media.revision; return messenger.media.source(url) }
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
    Repeater {
        model: root.attachments
        delegate: ColumnLayout {
            id: row
            required property var modelData
            Layout.fillWidth: true
            spacing: 4
            Item {
                id: imageFrame
                property bool sticker: row.modelData.type === "sticker"
                property bool paused: false
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: sticker ? Math.min(208, root.width) : root.width
                Layout.preferredHeight: visible ? (sticker ? Math.min(208, root.width) : root.photoHeight(row.modelData, root.width)) : 0
                Layout.fillHeight: false
                visible: root.imageUrl(row.modelData).length > 0 || root.animationUrl(row.modelData).length > 0
                AnimatedImage {
                    objectName: "attachmentImage"
                    anchors.fill: parent
                    source: root.cachedSource(root.rasterAnimation(row.modelData) ? root.animationUrl(row.modelData) : root.imageUrl(row.modelData))
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: !root.rasterAnimation(row.modelData)
                    sourceSize.width: imageFrame.sticker ? 416 : 1120
                    sourceSize.height: imageFrame.sticker ? 416 : 1040
                    playing: frameCount > 1
                    paused: !root.animationsEnabled || imageFrame.paused
                    visible: !animation.ready
                }
                LottieSticker {
                    id: animation
                    anchors.fill: parent
                    source: imageFrame.sticker && !root.rasterAnimation(row.modelData) ? root.cachedSource(root.animationUrl(row.modelData)) : ""
                    playing: root.animationsEnabled && !imageFrame.paused
                    visible: source.toString().length > 0
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.animationUrl(row.modelData).length) imageFrame.paused = !imageFrame.paused
                        else messenger.actions.openAttachment(row.modelData)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                visible: row.modelData.type !== "sticker"
                Label { text: root.title(row.modelData); Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; textFormat: Text.PlainText }
                Button {
                    text: row.modelData.type === "audio_message" || row.modelData.type === "audio" ? "▶" : row.modelData.type === "video" ? "Смотреть" : "Открыть"
                    enabled: root.url(row.modelData).length > 0
                    onClicked: messenger.actions.openAttachment(row.modelData)
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
