import QtQuick
import QtQuick.Controls
import "PhotoLayout.js" as PhotoLayout

ItemDelegate {
    id: quote
    property var message: ({})
    property string author: "Сообщение"
    property bool dismissible: false
    property bool clickable: true
    property bool loadImages: true
    signal activated()
    signal dismissed()
    readonly property var attachments: message.attachments || []
    readonly property var photo: attachments.find(function(a) { return a.type === "photo" }) || ({})
    readonly property string thumbnail: PhotoLayout.thumbnail(photo, 88, 88)
    readonly property string summary: {
        if (message.text && message.text.trim().length) return message.text.replace(/\s+/g, " ")
        const names = {photo: "Фотография", video: "Видео", doc: "Файл", sticker: "Стикер", audio_message: "Голосовое сообщение", audio: "Музыка", link: "Ссылка"}
        return attachments.length ? names[attachments[0].type] || "Вложение" : "Сообщение"
    }
    implicitHeight: 60
    padding: 0
    focusPolicy: clickable ? Qt.StrongFocus : Qt.NoFocus
    Accessible.name: "Ответ: " + author + ". " + summary
    background: Rectangle {
        radius: 8
        color: messenger.darkTheme ? "#2b2f34" : "#edf3fa"
        opacity: quote.hovered && quote.clickable ? 1 : 0.75
    }
    Rectangle { x: 0; y: 7; width: 3; height: parent.height - 14; radius: 1.5; color: messenger.darkTheme ? "#71aaeb" : "#2688eb" }
    CachedImage {
        id: thumb
        x: 12; y: 8; width: 44; height: 44
        remoteSource: quote.thumbnail; active: quote.loadImages
        visible: quote.thumbnail.length > 0
        fillMode: Image.PreserveAspectCrop; sourceSize.width: 88; sourceSize.height: 88
    }
    Column {
        x: thumb.visible ? 66 : 12; y: 10
        width: Math.max(1, quote.width - x - (quote.dismissible ? 44 : 12)); spacing: 4
        Text { width: parent.width; text: quote.author; textFormat: Text.PlainText; font.pixelSize: 13; font.weight: Font.DemiBold; color: messenger.darkTheme ? "#71aaeb" : "#2688eb"; elide: Text.ElideRight }
        Text { width: parent.width; text: quote.summary; textFormat: Text.PlainText; font.pixelSize: 13; color: messenger.darkTheme ? "#b3bac3" : "#626e7c"; elide: Text.ElideRight }
    }
    ToolButton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 36; text: "×"; visible: quote.dismissible; Accessible.name: "Отменить ответ"; onClicked: quote.dismissed() }
    onClicked: if (clickable) activated()
}
