import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card
    property string filename: ""
    property string status: ""
    property string error: ""
    property bool busy: false
    property bool canRetry: false
    property bool download: false
    property real progress: 0
    property real transferred: 0
    property real total: -1
    property int waiting: 0
    property string statusObjectName: ""
    property string actionPrefix: ""
    signal retryRequested()
    signal cancelRequested()
    Theme { id: design; dark: messenger.darkTheme }
    implicitHeight: contents.implicitHeight + 20
    radius: 10; color: error.length ? design.errorSurface : design.secondary
    border.color: error.length ? design.errorText : design.divider
    function bytes(value) {
        if (value < 1024) return Math.round(Math.max(0, value)) + " Б"
        const units = ["КБ", "МБ", "ГБ", "ТБ"]
        let number = value / 1024, unit = 0
        while (number >= 1024 && unit < units.length - 1) { number /= 1024; ++unit }
        return number.toFixed(number < 10 ? 1 : 0) + " " + units[unit]
    }
    ColumnLayout {
        id: contents
        x: 10; y: 10; width: Math.max(1, card.width - 20); spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            ActionIcon { Layout.preferredWidth: 18; Layout.preferredHeight: 18; kind: card.download ? "download" : "document"; stroke: card.error.length ? design.errorText : design.accent }
            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: card.filename || (card.download ? "Скачивание файла" : "Отправка файла"); color: design.ink; font.pixelSize: 12; font.weight: Font.DemiBold; textFormat: Text.PlainText; elide: Text.ElideMiddle }
            Text { visible: card.busy && card.total > 0; text: Math.round(card.progress * 100) + "%"; color: design.muted; font.pixelSize: 11 }
            ToolButton {
                objectName: card.actionPrefix + "CopyError"
                Layout.preferredWidth: 28; Layout.preferredHeight: 28
                visible: card.error.length > 0
                Accessible.name: "Скопировать ошибку передачи"
                ToolTip.visible: visible && (hovered || visualFocus); ToolTip.text: Accessible.name
                contentItem: ActionIcon { kind: "copy"; stroke: design.muted }
                onClicked: messenger.actions.copyText(card.error)
            }
            ToolButton {
                objectName: card.actionPrefix + "Retry"
                Layout.preferredWidth: 28; Layout.preferredHeight: 28
                visible: card.error.length > 0 && card.canRetry
                Accessible.name: (card.download ? "Повторить скачивание: " : "Повторить отправку: ") + card.filename
                ToolTip.visible: visible && (hovered || visualFocus); ToolTip.text: Accessible.name
                contentItem: ActionIcon { kind: "retry"; stroke: design.accent }
                onClicked: card.retryRequested()
            }
            ToolButton {
                objectName: card.actionPrefix + "Cancel"
                Layout.preferredWidth: 28; Layout.preferredHeight: 28
                Accessible.name: (card.download ? "Отменить скачивание: " : "Отменить отправку: ") + card.filename
                ToolTip.visible: visible && (hovered || visualFocus); ToolTip.text: Accessible.name
                contentItem: ActionIcon { kind: "close"; stroke: design.muted }
                onClicked: card.cancelRequested()
            }
        }
        Text {
            objectName: card.statusObjectName
            Layout.fillWidth: true; Layout.minimumWidth: 0
            text: card.error || card.status
            visible: text.length > 0; color: card.error.length ? design.errorText : design.muted
            font.pixelSize: 12; textFormat: Text.PlainText
            wrapMode: card.error.length ? Text.Wrap : Text.NoWrap
            elide: card.error.length ? Text.ElideNone : Text.ElideRight
        }
        ProgressBar {
            objectName: card.actionPrefix + "Progress"
            Layout.fillWidth: true; visible: card.busy
            value: card.progress; indeterminate: card.total <= 0
        }
        Text {
            Layout.fillWidth: true
            visible: card.busy && (card.transferred > 0 || card.waiting > 0)
            text: (card.transferred > 0 ? card.bytes(card.transferred) + (card.total > 0 ? " из " + card.bytes(card.total) : "") : "")
                + (card.waiting > 0 ? (card.transferred > 0 ? " · " : "") + "В очереди: " + card.waiting : "")
            color: design.muted; font.pixelSize: 11; elide: Text.ElideRight
        }
    }
}
