import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "cacheDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(540, parent.width - 48)
    height: Math.min(660, parent.height - 48)
    modal: true
    padding: 20
    title: "Данные и память"
    Theme { id: theme; dark: messenger.darkTheme }
    function bytes(value) {
        let number = Math.max(0, Number(value) || 0), unit = 0
        const units = ["Б", "КиБ", "МиБ", "ГиБ"]
        while (number >= 1024 && unit < units.length - 1) { number /= 1024; ++unit }
        return number.toLocaleString(Qt.locale(), 'f', unit && number < 10 ? 1 : 0) + " " + units[unit]
    }
    onOpened: messenger.refreshCacheStats()
    background: Rectangle { radius: 16; color: theme.surface; border.color: theme.divider }
    header: Label {
        text: dialog.title; color: theme.ink; font.pixelSize: 19; font.weight: Font.DemiBold
        leftPadding: 20; rightPadding: 20; topPadding: 20; bottomPadding: 8
    }
    footer: Item {
        implicitHeight: 64
        CacheButton { anchors.right: parent.right; anchors.rightMargin: 20; anchors.top: parent.top; anchors.topMargin: 8; text: "Закрыть"; onClicked: dialog.close() }
    }
    component CacheButton: Button {
        id: button
        implicitHeight: 40
        leftPadding: 14; rightPadding: 14
        contentItem: Text { text: button.text; color: theme.ink; opacity: button.enabled ? 1 : 0.45; font: button.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { radius: 8; color: button.down ? theme.pressed : button.hovered ? theme.hover : theme.surface; border.color: button.visualFocus ? theme.accent : theme.divider }
    }
    component Section: Rectangle {
        default property alias content: stack.data
        Layout.fillWidth: true
        implicitHeight: stack.implicitHeight + 28
        radius: 12; color: theme.secondary
        data: [ColumnLayout { id: stack; x: 14; y: 14; width: parent.width - 28; spacing: 10 }]
    }
    contentItem: ScrollView {
        id: scroll
        objectName: "cacheScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: ScrollBar { parent: scroll; x: scroll.width - width; height: scroll.height; policy: ScrollBar.AsNeeded; active: true }
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 16
            Section {
                RowLayout {
                    Layout.fillWidth: true
                    ActionIcon { kind: "photo"; stroke: theme.accent; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                    Label { Layout.fillWidth: true; text: "Медиакэш на устройстве"; color: theme.ink; font.bold: true; wrapMode: Text.Wrap }
                    Label { objectName: "cacheMediaSize"; text: dialog.bytes(messenger.media.bytes); color: theme.ink; font.bold: true }
                }
                ProgressBar { objectName: "cacheUsage"; Layout.fillWidth: true; value: Math.min(1, messenger.media.bytes / Math.max(1, messenger.media.limit)); Accessible.name: "Заполнение медиакэша" }
                Label { Layout.fillWidth: true; text: "Лимит: " + dialog.bytes(messenger.media.limit); color: theme.muted; font.pixelSize: 12 }
                Label { Layout.fillWidth: true; text: "Фотографии и файлы сохраняются для быстрого повторного просмотра. При достижении лимита старые копии удаляются автоматически."; color: theme.muted; wrapMode: Text.Wrap; font.pixelSize: 13 }
                ComboBox {
                    id: limitPicker
                    objectName: "cacheLimit"
                    Layout.fillWidth: true
                    Accessible.name: "Лимит медиакэша"
                    model: ["256 МиБ", "1 ГиБ", "5 ГиБ", "10 ГиБ"]
                    currentIndex: messenger.media.limit <= 268435456 ? 0 : messenger.media.limit <= 1073741824 ? 1 : messenger.media.limit <= 5368709120 ? 2 : 3
                    onActivated: messenger.media.limit = [268435456, 1073741824, 5368709120, 10737418240][currentIndex]
                    implicitHeight: 40
                    background: Rectangle { radius: 8; color: theme.surface; border.color: limitPicker.visualFocus ? theme.accent : theme.divider }
                    contentItem: Text { text: limitPicker.displayText; color: theme.ink; leftPadding: 12; rightPadding: 38; font: limitPicker.font; verticalAlignment: Text.AlignVCenter }
                    indicator: ActionIcon { x: limitPicker.width - width - 12; y: (limitPicker.height - height) / 2; width: 18; height: 18; kind: "chevronDown"; stroke: theme.muted }
                }
                CacheButton {
                    objectName: "clearMediaCache"
                    Layout.fillWidth: true
                    text: messenger.media.clearing ? "Очистка медиакэша…" : "Очистить медиа этого аккаунта"
                    enabled: !messenger.media.clearing
                    onClicked: messenger.media.clear()
                }
                BusyIndicator { objectName: "cacheClearing"; Layout.alignment: Qt.AlignHCenter; visible: messenger.media.clearing; running: visible && dialog.visible }
                Label { objectName: "cacheClearError"; Layout.fillWidth: true; visible: text.length > 0; text: messenger.media.clearError; textFormat: Text.PlainText; color: theme.errorText; wrapMode: Text.Wrap }
                Label { Layout.fillWidth: true; text: "После очистки нужные медиа снова загрузятся при просмотре. Файлы, сохранённые вами отдельно, останутся."; color: theme.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
            }
            Section {
                RowLayout {
                    Layout.fillWidth: true
                    ActionIcon { kind: "chat"; stroke: theme.accent; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                    Label { Layout.fillWidth: true; text: "Сохранённая история"; color: theme.ink; font.bold: true }
                    Label { objectName: "cacheHistorySize"; text: messenger.cacheStats.messagesBytes === undefined ? "—" : dialog.bytes((messenger.cacheStats.messagesBytes || 0) + (messenger.cacheStats.entitiesBytes || 0)); color: theme.ink; font.bold: true }
                }
                Label { objectName: "cacheMessageCount"; Layout.fillWidth: true; text: messenger.cacheStats.messagesCount === undefined ? "Подсчитываем сообщения…" : "Сообщений: " + messenger.cacheStats.messagesCount; color: theme.muted }
                Label { Layout.fillWidth: true; text: "История будет загружена из VK при подключении. Черновики и неотправленные сообщения сохранятся."; color: theme.muted; wrapMode: Text.Wrap; font.pixelSize: 13 }
                CacheButton { objectName: "clearHistoryCache"; Layout.fillWidth: true; text: "Очистить сохранённую историю"; enabled: messenger.connected && !messenger.demo && !messenger.sending; onClicked: messenger.clearHistoryCache() }
            }
            Section {
                RowLayout {
                    Layout.fillWidth: true
                    ActionIcon { kind: "send"; stroke: theme.accent; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                    Label { Layout.fillWidth: true; text: "Очередь отправки"; color: theme.ink; font.bold: true }
                    Label { objectName: "cacheOutgoingCount"; text: messenger.cacheStats.outgoingCount === undefined ? "—" : String(messenger.cacheStats.outgoingCount); color: theme.ink; font.bold: true }
                }
                Label { Layout.fillWidth: true; text: "Неотправленные сообщения и черновики хранятся отдельно от кэша."; color: theme.muted; wrapMode: Text.Wrap; font.pixelSize: 13 }
            }
        }
    }
}
