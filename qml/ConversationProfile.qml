import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Drawer {
    id: panel
    objectName: "conversationProfile"
    required property var theme
    required property var controller
    required property var client
    property string peer: ""
    property string peerTitle: ""
    property bool chat: client.isChat
    property bool manage: client.canManageChat
    readonly property var info: client.conversationProfile
    property int loadedMemberCount: 0
    readonly property int memberCount: controller.memberCount || loadedMemberCount || info.members_count || 0
    property string avatar: ""
    property int section: 0
    property string query: ""
    property Item returnFocus: null
    signal actionRequested(string kind)
    edge: Qt.RightEdge
    width: Math.min(460, parent ? parent.width : 460)
    height: parent ? parent.height : 700
    padding: 0
    modal: false
    interactive: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: theme.surface; border.color: theme.divider; radius: 12 }
    enter: Transition { NumberAnimation { property: "position"; to: 1; duration: client.reduceMotion ? 0 : theme.panelDuration; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "position"; to: 0; duration: client.reduceMotion ? 0 : theme.motionDuration; easing.type: Easing.OutCubic } }
    function present(focusItem) {
        loadedMemberCount = 0
        peer = client.selectedPeer
        peerTitle = client.title
        avatar = info.avatar || ""
        section = chat ? 0 : 1
        query = ""
        returnFocus = focusItem
        loadSection()
        open()
    }
    function loadSection() {
        controller.reset()
        if (section === 0 && chat) controller.loadMembers(peer)
        else controller.loadAttachments(peer, ["", "photo", "video", "doc", "link"][section])
    }
    function activity(person) {
        if (person.online) return "В сети"
        const time = person.last_seen ? person.last_seen.time : 0
        return time ? "Был(а) " + Qt.formatDateTime(new Date(time * 1000), "dd.MM.yyyy в HH:mm") : ""
    }
    Connections {
        target: controller
        function onChanged() { if (panel.chat && controller.memberCount > 0) panel.loadedMemberCount = controller.memberCount }
    }
    onClosed: { controller.reset(); if (returnFocus) returnFocus.forceActiveFocus() }

    component ProfileAvatar: Rectangle {
        id: portrait
        property string source: ""
        property string name: ""
        implicitWidth: 48; implicitHeight: 48
        radius: width / 2
        color: theme.secondary
        Text { anchors.centerIn: parent; text: portrait.name.substring(0, 1).toUpperCase(); color: theme.accent; font.pixelSize: portrait.width * 0.35 }
        Image {
            anchors.fill: parent
            source: { client.media.revision; return client.media.source(portrait.source) }
            sourceSize.width: 256; sourceSize.height: 256
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            layer.enabled: true
            layer.effect: MultiEffect { maskEnabled: true; maskSource: avatarMask }
        }
        Rectangle { id: avatarMask; anchors.fill: parent; radius: width / 2; layer.enabled: true; visible: false; color: "white" }
    }
    component Divider: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.divider
    }
    component Tile: Button {
        id: tile
        property string iconKind: ""
        Layout.fillWidth: true
        implicitHeight: 70
        contentItem: Column {
            spacing: 6
            ActionIcon { anchors.horizontalCenter: parent.horizontalCenter; kind: tile.iconKind; stroke: theme.accent; width: 24; height: 24 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: tile.text; color: theme.accent; font.pixelSize: 14 }
        }
        background: Rectangle { radius: 12; color: tile.down || tile.hovered ? theme.divider : theme.secondary; Behavior on color { enabled: !client.reduceMotion; ColorAnimation { duration: theme.hoverDuration; easing.type: Easing.OutCubic } } }
    }
    component InfoRow: ItemDelegate {
        id: infoRow
        property string symbol: ""
        property bool copyable: false
        Layout.fillWidth: true
        implicitHeight: 48
        contentItem: RowLayout {
            spacing: 16
            Text { text: infoRow.symbol; color: theme.muted; font.pixelSize: 22; Layout.preferredWidth: 24; horizontalAlignment: Text.AlignHCenter }
            Text { text: infoRow.text; textFormat: Text.PlainText; color: theme.accent; font.pixelSize: 16; Layout.fillWidth: true; elide: Text.ElideMiddle }
            ActionIcon { visible: infoRow.copyable; kind: "copy"; stroke: theme.accent; width: 22; height: 22 }
        }
        background: Rectangle { color: infoRow.hovered ? theme.secondary : "transparent"; radius: 8; Behavior on color { enabled: !client.reduceMotion; ColorAnimation { duration: theme.hoverDuration; easing.type: Easing.OutCubic } } }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            ToolButton { text: "×"; font.pixelSize: 28; Accessible.name: "Закрыть информацию"; onClicked: panel.close() }
            Item { Layout.fillWidth: true }
            ToolButton { visible: panel.chat; text: "⚙"; font.pixelSize: 24; Accessible.name: "Настройки беседы"; onClicked: panel.actionRequested("settings") }
        }
        ListView {
            id: results
            objectName: "profileResults"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: panel.section === 1 ? photoRows : controller.results
            readonly property var photoRows: {
                const rows = controller.results.filter(function(row) { return String(row.title || "").toLowerCase().indexOf(panel.query) >= 0 })
                const groups = []
                for (let i = 0; i < rows.length; i += 3) groups.push(rows.slice(i, i + 3))
                return groups
            }
            spacing: 4
            ScrollBar.vertical: ScrollBar {}
            header: ColumnLayout {
                width: results.width
                spacing: 12
                ProfileAvatar { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 96; Layout.preferredHeight: 96; source: panel.avatar; name: panel.peerTitle }
                Label {
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    text: panel.peerTitle; textFormat: Text.PlainText; color: theme.ink
                    font.pixelSize: 23; font.bold: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    text: panel.chat ? (panel.memberCount > 0 ? "Участники: " + panel.memberCount : "Беседа") : panel.activity(panel.info)
                    visible: text.length > 0
                    color: theme.muted; font.pixelSize: 15; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.margins: 20
                    spacing: 6
                    Tile { iconKind: "search"; text: "Поиск"; Accessible.name: "Поиск в переписке"; onClicked: panel.actionRequested("search") }
                    Tile {
                        iconKind: panel.info.muted ? "muted" : "notifications"
                        text: panel.info.muted ? "Отключено" : "Включено"
                        Accessible.name: "Уведомления: " + text
                        onClicked: client.actions.conversationAction(panel.peer, panel.info.muted ? "unmute" : "mute")
                    }
                    Tile { iconKind: "more"; text: "Ещё"; onClicked: panel.actionRequested("settings") }
                }
                Divider {}
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                    visible: !panel.chat
                    spacing: 0
                    InfoRow { symbol: "@"; text: panel.info.domain || (Number(panel.peer) < 0 ? "club" + Math.abs(Number(panel.peer)) : "id" + panel.peer); copyable: true; onClicked: controller.copyText(text) }
                    InfoRow { symbol: "♙"; text: panel.peer; copyable: true; onClicked: controller.copyText(text) }
                }
                Divider { visible: !panel.chat }
                InfoRow {
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    visible: !panel.chat
                    symbol: "↗"; text: "Открыть профиль ВКонтакте"
                    onClicked: Qt.openUrlExternally("https://vk.ru/" + (Number(panel.peer) < 0 ? "club" + Math.abs(Number(panel.peer)) : "id" + panel.peer))
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                    visible: panel.chat
                    Button { visible: !controller.inviteLink; text: "Ссылка-приглашение"; enabled: !controller.busy; onClicked: controller.loadInvite(panel.peer) }
                    InfoRow { visible: !!controller.inviteLink; symbol: "↗"; text: controller.inviteLink; copyable: true; onClicked: controller.copyText(controller.inviteLink) }
                }
                Divider {}
                Flickable {
                    Layout.fillWidth: true; Layout.preferredHeight: 42
                    contentWidth: tabs.implicitWidth + 32; clip: true; boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: tabs
                        x: 16; spacing: 4
                        Repeater {
                            model: [{name:"Участники", section:0}, {name:"Фотографии", section:1}, {name:"Видео", section:2}, {name:"Файлы", section:3}, {name:"Ссылки", section:4}]
                            delegate: Button {
                                required property var modelData
                                visible: modelData.section !== 0 || panel.chat
                                width: visible ? implicitWidth : 0
                                text: modelData.name + (modelData.section === 0 && panel.memberCount > 0 ? " " + panel.memberCount : "")
                                contentItem: Text { text: parent.text; color: panel.section === modelData.section ? theme.ink : theme.muted; font.pixelSize: 15; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 10; color: panel.section === modelData.section ? theme.divider : parent.hovered ? theme.secondary : "transparent"; Behavior on color { enabled: !client.reduceMotion; ColorAnimation { duration: theme.hoverDuration; easing.type: Easing.OutCubic } } }
                                onClicked: { panel.section = modelData.section; panel.query = ""; panel.loadSection() }
                            }
                        }
                    }
                }
                TextField {
                    objectName: "profileSearch"
                    Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                    placeholderText: panel.section === 0 ? "Поиск участников" : "Поиск в загруженном"
                    text: panel.query; onTextEdited: panel.query = text.toLowerCase()
                    color: theme.ink; placeholderTextColor: theme.muted
                    leftPadding: 14; implicitHeight: 40
                    background: Rectangle { color: theme.secondary; radius: 10; border.color: parent.activeFocus ? theme.accent : "transparent" }
                }
                InfoRow { Layout.leftMargin: 16; Layout.rightMargin: 16; visible: panel.chat && panel.section === 0; symbol: "+"; text: "Добавить участников"; onClicked: panel.actionRequested("add") }
                Label { Layout.fillWidth: true; Layout.margins: 16; visible: controller.error.length > 0; text: controller.error; textFormat: Text.PlainText; color: "#ff6b6b"; wrapMode: Text.Wrap }
                BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: controller.busy; running: visible }
                Label { Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 20; visible: !controller.busy && !controller.results.length && !controller.error.length; text: panel.section === 0 ? "Нет участников" : "Пока ничего нет"; color: theme.muted }
            }
            delegate: ColumnLayout {
                id: result
                required property var modelData
                readonly property bool matches: panel.section === 1 || String(modelData.title || "").toLowerCase().indexOf(panel.query) >= 0
                width: results.width
                visible: matches
                height: matches ? implicitHeight : 0
                RowLayout {
                    visible: panel.section === 0
                    Layout.fillWidth: true; Layout.minimumHeight: 64; Layout.leftMargin: 16; Layout.rightMargin: 16
                    spacing: 14
                    ProfileAvatar { source: result.modelData.avatar || ""; name: result.modelData.title || "" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { Layout.fillWidth: true; text: result.modelData.title || "Участник"; textFormat: Text.PlainText; color: theme.ink; font.pixelSize: 16; elide: Text.ElideRight }
                        Label {
                            Layout.fillWidth: true
                            text: result.modelData.is_owner ? "Создатель беседы" : result.modelData.is_admin ? "Администратор" : panel.activity(result.modelData)
                            visible: text.length > 0; color: result.modelData.online ? theme.accent : theme.muted; font.pixelSize: 13; elide: Text.ElideRight
                        }
                    }
                    ToolButton { text:"···"; Accessible.name: "Действия с участником"; onClicked: memberMenu.popup() }
                    AppMenu {
                        id: memberMenu
                        AppMenuItem { kind: "person"; text: "Открыть профиль"; onTriggered: Qt.openUrlExternally("https://vk.ru/" + (Number(result.modelData.peer) < 0 ? "club" + Math.abs(Number(result.modelData.peer)) : "id" + result.modelData.peer)) }
                        AppMenuItem { kind: "people"; text: "Управление участниками…"; visible: panel.manage && !result.modelData.is_owner; onTriggered: panel.actionRequested("members") }
                    }
                }
                RowLayout {
                    visible: panel.section === 1
                    Layout.fillWidth: true
                    spacing: 3
                    Repeater {
                        model: panel.section === 1 ? result.modelData : []
                        delegate: ItemDelegate {
                            id: thumbnail
                            required property var modelData
                            readonly property var attachment: (modelData.attachments || [])[0] || {}
                            Layout.preferredWidth: (results.width - 6) / 3
                            Layout.preferredHeight: Layout.preferredWidth
                            padding: 0
                            Accessible.name: "Открыть фотографию"
                            contentItem: Image {
                                source: {
                                    client.media.revision
                                    const sizes = (thumbnail.attachment.photo || {}).sizes || []
                                    return sizes.length ? client.media.source(sizes[sizes.length - 1].url) : ""
                                }
                                sourceSize.width: 360; sourceSize.height: 360
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            background: Rectangle { color: theme.secondary }
                            onClicked: client.actions.openAttachment(attachment, controller.results.map(function(row) { return (row.attachments || [])[0] }).filter(function(item) { return !!item }))
                        }
                    }
                    Item { Layout.fillWidth: true; visible: panel.section === 1 && result.modelData.length < 3 }
                }
                AttachmentView { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; visible: panel.section !== 0 && panel.section !== 1; attachments: result.modelData.attachments || []; animationsEnabled: panel.opened && !client.reduceMotion }
                Button { Layout.leftMargin: 16; visible: panel.section !== 0 && panel.section !== 1 && !!result.modelData.cmid; text: "К сообщению"; onClicked: { client.openMessage(panel.peer, String(result.modelData.cmid)); panel.close() } }
            }
            footer: Button {
                width: results.width
                visible: controller.hasMore
                text: "Загрузить ещё"
                enabled: !controller.busy
                onClicked: { if (panel.section === 0) controller.loadMembers(panel.peer, true); else controller.loadAttachments(panel.peer, ["", "photo", "video", "doc", "link"][panel.section], true) }
            }
        }
    }
}
