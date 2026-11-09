import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "PhotoLayout.js" as PhotoLayout

Item {
    id: grid
    property var photos: []
    property var siblings: photos
    property bool loadImages: true
    property color cornerColor: messenger.darkTheme ? "#222222" : "white"
    readonly property var geometry: PhotoLayout.layout(photos, width, 3)
    implicitHeight: geometry.height
    Repeater {
        model: grid.photos
        delegate: ItemDelegate {
            id: tile
            objectName: "attachmentOpen"
            required property var modelData
            required property int index
            readonly property var cell: grid.geometry.tiles[index] || {x: 0, y: 0, width: 0, height: 0}
            readonly property var attachment: modelData
            readonly property string thumbnail: PhotoLayout.thumbnail(attachment, width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
            readonly property bool failed: thumbnail.length > 0 && (image.status === Image.Error || image.loadError.length > 0)
            readonly property bool compact: height < 110 || width < 140
            x: cell.x; y: cell.y
            width: cell.width; height: cell.height
            padding: 0
            Accessible.name: "Открыть фотографию " + (index + 1) + " из " + grid.photos.length
            background: Rectangle { color: messenger.darkTheme ? "#303030" : "#e8ecf1" }
            contentItem: CachedImage {
                id: image
                objectName: "photoThumbnail"
                remoteSource: tile.thumbnail
                active: grid.loadImages && tile.width > 0 && tile.height > 0
                fillMode: Image.PreserveAspectCrop
                readonly property int decodeSize: Math.max(tile.width, tile.height) * Screen.devicePixelRatio <= 256 ? 256 : Math.max(tile.width, tile.height) * Screen.devicePixelRatio <= 512 ? 512 : 1024
                sourceSize.width: decodeSize; sourceSize.height: decodeSize
            }
            Column {
                objectName: "photoThumbnailState"
                anchors.centerIn: parent
                width: Math.max(1, Math.min(140, tile.width - 16))
                spacing: 6
                visible: image.status !== Image.Ready
                ActionIcon { anchors.horizontalCenter: parent.horizontalCenter; width: tile.compact ? 20 : 28; height: width; visible: !tile.compact || (!tile.failed && tile.height < 52); kind: "photo"; stroke: messenger.darkTheme ? "#939393" : "#818c99"; opacity: 0.65 }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    visible: tile.height >= 52
                    text: tile.failed ? "Не удалось загрузить" : tile.thumbnail.length ? "Загрузка…" : "Фото недоступно"
                    color: messenger.darkTheme ? "#b2b8c2" : "#626e7c"; font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: tile.compact ? 1 : 2; elide: Text.ElideRight
                }
                ToolButton {
                    objectName: "retryPhotoThumbnail"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(100, parent.width); height: 28
                    visible: tile.failed
                    text: "Повторить"
                    Accessible.name: "Повторить загрузку миниатюры"
                    contentItem: Text { text: parent.text; color: messenger.darkTheme ? "#8fc1ff" : "#1769c2"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 7; color: messenger.darkTheme ? "#354153" : "#dae9fa"; border.color: parent.visualFocus ? (messenger.darkTheme ? "#8fc1ff" : "#1769c2") : "transparent" }
                    onClicked: { tile.forceActiveFocus(); messenger.media.retry(tile.thumbnail) }
                }
            }
            Rectangle { anchors.fill: parent; color: "white"; opacity: tile.down ? 0.14 : tile.hovered ? 0.06 : 0; Behavior on opacity { enabled: !messenger.reduceMotion; NumberAnimation { duration: 120 } } }
            onClicked: messenger.actions.openAttachment(attachment, grid.siblings)
        }
    }
    // A small corner cover works with software rendering as well as Metal.
    Canvas {
        anchors.fill: parent
        property color fillColor: grid.cornerColor
        onFillColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"), r = Math.min(12, width / 2, height / 2)
            c.reset(); c.fillStyle = fillColor
            c.beginPath(); c.moveTo(0, r); c.arcTo(0, 0, r, 0, r); c.lineTo(0, 0); c.closePath(); c.fill()
            c.beginPath(); c.moveTo(width-r, 0); c.arcTo(width, 0, width, r, r); c.lineTo(width, 0); c.closePath(); c.fill()
            c.beginPath(); c.moveTo(width, height-r); c.arcTo(width, height, width-r, height, r); c.lineTo(width, height); c.closePath(); c.fill()
            c.beginPath(); c.moveTo(r, height); c.arcTo(0, height, 0, height-r, r); c.lineTo(0, height); c.closePath(); c.fill()
        }
    }
}
