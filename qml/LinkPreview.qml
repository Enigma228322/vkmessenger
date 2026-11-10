import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "PhotoLayout.js" as PhotoLayout

ItemDelegate {
    id: card
    objectName: "linkPreview"
    property var link: ({})
    property bool loadImages: true
    readonly property string target: String(link.url || "")
    readonly property string domain: {
        const match = target.match(/^https?:\/\/([^\/?#]+)/i)
        return match ? match[1] : target
    }
    readonly property string thumbnail: PhotoLayout.thumbnail({photo: link.photo || {}}, Math.max(1, width * Screen.devicePixelRatio), 200 * Screen.devicePixelRatio)
    readonly property bool imageFailed: thumbnail.length > 0 && (image.status === Image.Error || image.loadError.length > 0)
    Theme { id: design; dark: messenger.darkTheme }
    padding: 12
    implicitHeight: body.implicitHeight + 24
    enabled: /^https?:\/\//i.test(target)
    Accessible.role: Accessible.Link
    Accessible.name: "Открыть ссылку: " + (link.title || domain)
    background: Rectangle { radius: 10; color: card.hovered ? design.hover : design.secondary; border.color: card.visualFocus ? design.accent : design.divider; border.width: card.visualFocus ? 2 : 1 }
    contentItem: Column {
        id: body
        spacing: 7
        Item {
            id: picture
            width: parent.width
            height: visible ? Math.min(200, width * 0.5625) : 0
            visible: card.thumbnail.length > 0
            clip: true
            Rectangle { anchors.fill: parent; color: design.divider }
            CachedImage {
                id: image
                objectName: "linkPreviewImage"
                anchors.fill: parent
                remoteSource: card.thumbnail
                active: card.loadImages && card.enabled && picture.width > 0
                sourceSize.width: Math.min(1024, Math.max(1, Math.ceil(picture.width * Screen.devicePixelRatio)))
                sourceSize.height: 600
                fillMode: Image.PreserveAspectCrop
            }
            Column {
                anchors.centerIn: parent; width: Math.max(1, parent.width - 16); spacing: 6
                visible: image.status !== Image.Ready
                ActionIcon { anchors.horizontalCenter: parent.horizontalCenter; width: 24; height: 24; kind: "link"; stroke: design.muted }
                Text { width: parent.width; text: card.imageFailed ? "Не удалось загрузить превью" : "Загрузка превью…"; color: design.muted; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
                ToolButton {
                    objectName: "retryLinkPreview"
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: card.imageFailed
                    text: "Повторить"; Accessible.name: "Повторить загрузку превью ссылки"
                    onClicked: { card.forceActiveFocus(); messenger.media.retry(card.thumbnail) }
                }
            }
        }
        Text { objectName: "linkPreviewTitle"; width: parent.width; text: card.link.title || card.domain; textFormat: Text.PlainText; color: design.ink; font.pixelSize: 14; font.weight: Font.DemiBold; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
        Text { objectName: "linkPreviewDescription"; width: parent.width; text: card.link.description || ""; visible: text.length > 0; textFormat: Text.PlainText; color: design.muted; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
        Text { width: parent.width; text: card.domain; textFormat: Text.PlainText; color: design.accent; font.pixelSize: 11; elide: Text.ElideMiddle }
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    onClicked: messenger.actions.openAttachment({type: "link", link: card.link})
}
