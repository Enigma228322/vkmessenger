import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "PhotoLayout.js" as PhotoLayout

Drawer {
    id: panel
    objectName: "conversationProfile"
    required property var theme
    required property var controller
    required property var client
    property string peer: ""
    property string peerTitle: ""
    property bool person: false
    readonly property bool chat: !person && Number(peer) > 2000000000
    readonly property bool manage: chat && peer === client.selectedPeer && client.canManageChat
    property var personSeed: ({})
    readonly property var info: {
        client.title; client.conversations.revision
        return person ? Object.assign({}, personSeed, client.peerProfile(peer)) : client.conversationProfile
    }
    readonly property string displayTitle: person ? info.title || peerTitle : peerTitle
    property int loadedMemberCount: 0
    readonly property int memberCount: controller.memberCount || loadedMemberCount || info.members_count || 0
    property string avatar: ""
    property int section: 0
    property string query: ""
    property Item searchInput: null
    readonly property string normalizedQuery: query.trim().toLowerCase()
    readonly property bool docked: visible && parent && parent.width >= 1320
    readonly property var filteredResults: {
        const rows = controller.results
        if (!normalizedQuery.length) return rows
        return rows.filter(function(row) { return String(row.title || "").toLowerCase().indexOf(panel.normalizedQuery) >= 0 })
    }
    property Item returnFocus: null
    signal actionRequested(string kind)
    signal writeRequested(string peer, string title)
    edge: Qt.RightEdge
    width: Math.min(docked ? 400 : 460, parent ? parent.width : 460)
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
        person = false
        personSeed = ({})
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
    function presentPerson(value, focusItem) {
        if (!value.peer || Number(value.peer) === 0 || Math.abs(Number(value.peer)) >= 2000000000) return
        personSeed = value
        person = true
        loadedMemberCount = 0
        peer = String(value.peer)
        peerTitle = value.title || "Профиль"
        avatar = value.avatar || ""
        section = 1; query = ""
        returnFocus = focusItem
        client.loadPeerProfile(peer)
        loadSection()
        open()
    }
    function loadSection() {
        profileSearchDelay.stop()
        query = ""
        if (searchInput) searchInput.clear()
        results.currentIndex = -1
        controller.reset()
        if (section === 0 && chat) controller.loadMembers(peer)
        else controller.loadAttachments(peer, ["", "photo", "video", "doc", "link"][section])
    }
    function activity(person) {
        if (person.online) return "В сети"
        const time = person.last_seen ? person.last_seen.time : 0
        return time ? "Был(а) " + Qt.formatDateTime(new Date(time * 1000), "dd.MM.yyyy в HH:mm") : ""
    }
    function focusMembers(direction) {
        if (section !== 0 || !searchInput || searchInput.inputMethodComposing) return
        searchInput.applySearch()
        if (!results.count) return
        results.currentIndex = direction > 0 ? 0 : results.count - 1
        memberNavigation.forceActiveFocus()
        results.positionViewAtIndex(results.currentIndex, ListView.Contain)
    }
    function openCurrentMember() {
        if (section !== 0 || results.currentIndex < 0 || results.currentIndex >= filteredResults.length) return
        presentPerson(filteredResults[results.currentIndex], returnFocus)
    }
    Connections {
        target: controller
        function onChanged() { if (panel.chat && controller.memberCount > 0) panel.loadedMemberCount = controller.memberCount }
    }
    onClosed: { profileSearchDelay.stop(); query = ""; if (searchInput) searchInput.clear(); controller.reset(); if (returnFocus) returnFocus.forceActiveFocus() }
    onQueryChanged: results.currentIndex = -1

    component ProfileAvatar: Rectangle {
        id: portrait
        property string source: ""
        property string name: ""
        property bool active: true
        implicitWidth: 48; implicitHeight: 48
        radius: width / 2
        color: theme.secondary
        Text { anchors.centerIn: parent; text: portrait.name.substring(0, 1).toUpperCase(); color: theme.accent; font.pixelSize: portrait.width * 0.35 }
        CachedAvatar { anchors.fill: parent; remoteSource: portrait.source; active: portrait.active }
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

    AppMenu {
        id: personMenu
        AppMenuItem { kind: "chat"; text: "Написать сообщение"; onTriggered: panel.writeRequested(panel.peer, panel.displayTitle) }
        AppMenuItem { kind: "search"; text: "Поиск в переписке"; onTriggered: panel.actionRequested("search") }
        AppMenuItem { kind: "link"; text: "Копировать ссылку на профиль"; onTriggered: controller.copyText("https://vk.ru/" + (Number(panel.peer) < 0 ? "club" + Math.abs(Number(panel.peer)) : "id" + panel.peer)) }
        AppMenuItem { kind: "person"; text: "Открыть во ВКонтакте"; onTriggered: Qt.openUrlExternally("https://vk.ru/" + (Number(panel.peer) < 0 ? "club" + Math.abs(Number(panel.peer)) : "id" + panel.peer)) }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            ToolButton { visible: panel.person && client.isChat; text: "‹"; font.pixelSize: 28; Accessible.name: "Вернуться к беседе"; onClicked: panel.present(panel.returnFocus) }
            ToolButton { text: "×"; font.pixelSize: 28; Accessible.name: "Закрыть информацию"; onClicked: panel.close() }
            Item { Layout.fillWidth: true }
            ToolButton { visible: panel.chat; text: "⚙"; font.pixelSize: 24; Accessible.name: "Настройки беседы"; onClicked: panel.actionRequested("settings") }
        }
        // The header search field and delegates belong to ListView's focus scope.
        // Keep navigation outside it so changing the current row retains focus.
        Item {
            id: memberNavigation
            objectName: "profileMemberNavigation"
            Layout.preferredWidth: 0; Layout.preferredHeight: 0
            visible: panel.section === 0
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: results.currentIndex >= 0 && results.currentIndex < panel.filteredResults.length ? panel.filteredResults[results.currentIndex].title || "Участник" : "Участники беседы"
            onActiveFocusChanged: if (activeFocus && panel.section === 0 && results.currentIndex < 0 && results.count) results.currentIndex = 0
            Keys.onReturnPressed: panel.openCurrentMember()
            Keys.onEnterPressed: panel.openCurrentMember()
            Keys.onSpacePressed: panel.openCurrentMember()
            Keys.onEscapePressed: if (panel.searchInput) panel.searchInput.forceActiveFocus()
            Keys.onPressed: function(event) {
                if (panel.section !== 0 || !results.count) return
                if (event.key === Qt.Key_Home) { results.currentIndex = 0; results.positionViewAtBeginning(); event.accepted = true }
                else if (event.key === Qt.Key_End) { results.currentIndex = results.count - 1; results.positionViewAtEnd(); event.accepted = true }
                else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                    results.currentIndex = Math.max(0, Math.min(results.count - 1, results.currentIndex + (event.key === Qt.Key_Down ? 1 : -1)))
                    results.positionViewAtIndex(results.currentIndex, ListView.Contain)
                    event.accepted = true
                }
            }
        }
        ListView {
            id: results
            objectName: "profileResults"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: panel.section === 1 ? photoRows : panel.filteredResults
            readonly property var photoRows: {
                if (panel.section !== 1) return []
                const rows = panel.filteredResults
                const groups = []
                for (let i = 0; i < rows.length; i += 3) groups.push(rows.slice(i, i + 3))
                return groups
            }
            spacing: 4
            keyNavigationEnabled: false
            currentIndex: -1
            ScrollBar.vertical: ScrollBar {}
            header: ColumnLayout {
                width: results.width
                spacing: 12
                ProfileAvatar { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 96; Layout.preferredHeight: 96; source: panel.info.avatar || panel.avatar; name: panel.displayTitle; active: panel.visible }
                Label {
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    text: panel.displayTitle; textFormat: Text.PlainText; color: theme.ink
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
                    Tile { objectName: "personWriteButton"; visible: panel.person; iconKind: "chat"; text: "Написать"; onClicked: panel.writeRequested(panel.peer, panel.displayTitle) }
                    Tile { iconKind: "search"; text: "Поиск"; Accessible.name: "Поиск в переписке"; onClicked: panel.actionRequested("search") }
                    Tile {
                        iconKind: panel.info.muted ? "muted" : "notifications"
                        text: panel.info.muted ? "Отключено" : "Включено"
                        Accessible.name: "Уведомления: " + text
                        onClicked: client.actions.conversationAction(panel.peer, panel.info.muted ? "unmute" : "mute")
                    }
                    Tile { iconKind: "more"; text: "Ещё"; onClicked: panel.person ? personMenu.popup() : panel.actionRequested("settings") }
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
                    id: profileSearch
                    objectName: "profileSearch"
                    Component.onCompleted: panel.searchInput = profileSearch
                    Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                    placeholderText: panel.section === 0 ? "Поиск участников" : "Поиск в загруженном"
                    text: panel.query
                    function applySearch() { profileSearchDelay.stop(); if (!inputMethodComposing) panel.query = text }
                    onTextEdited: {
                        profileSearchDelay.stop()
                        if (!text.length) applySearch()
                        else if (!inputMethodComposing) profileSearchDelay.restart()
                    }
                    onInputMethodComposingChanged: {
                        profileSearchDelay.stop()
                        if (panel.visible && !inputMethodComposing) profileSearchDelay.restart()
                    }
                    onAccepted: { applySearch(); panel.focusMembers(1) }
                    Keys.onDownPressed: function(event) { event.accepted = panel.section === 0 && !inputMethodComposing; if (event.accepted) panel.focusMembers(1) }
                    Keys.onUpPressed: function(event) { event.accepted = panel.section === 0 && !inputMethodComposing; if (event.accepted) panel.focusMembers(-1) }
                    Accessible.name: placeholderText
                    color: theme.ink; placeholderTextColor: theme.muted
                    leftPadding: 14; rightPadding: text.length ? 40 : 14; implicitHeight: 40
                    Keys.onEscapePressed: function(event) {
                        event.accepted = !inputMethodComposing && text.length > 0
                        if (event.accepted) { profileSearchDelay.stop(); panel.query = ""; clear() }
                    }
                    ToolButton {
                        objectName: "clearProfileSearch"
                        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; visible: profileSearch.text.length > 0
                        focusPolicy: Qt.NoFocus
                        Accessible.name: "Очистить поиск"
                        contentItem: ActionIcon { kind: "close"; stroke: theme.muted }
                        background: Rectangle { radius: 8; color: parent.hovered ? theme.divider : "transparent" }
                        onClicked: { profileSearchDelay.stop(); panel.query = ""; profileSearch.clear(); profileSearch.forceActiveFocus() }
                    }
                    background: Rectangle { color: theme.secondary; radius: 10; border.color: parent.activeFocus ? theme.accent : "transparent" }
                }
                InfoRow { Layout.leftMargin: 16; Layout.rightMargin: 16; visible: panel.chat && panel.section === 0; symbol: "+"; text: "Добавить участников"; onClicked: panel.actionRequested("add") }
                Label { Layout.fillWidth: true; Layout.margins: 16; visible: controller.error.length > 0; text: controller.error; textFormat: Text.PlainText; color: "#ff6b6b"; wrapMode: Text.Wrap }
                BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: controller.busy; running: visible }
                Label {
                    objectName: "profileEmptyState"
                    Layout.fillWidth: true; Layout.margins: 20
                    visible: !controller.busy && !profileSearchDelay.running && !profileSearch.inputMethodComposing && !panel.filteredResults.length && !controller.error.length
                    text: panel.normalizedQuery.length ? "Ничего не найдено в загруженном списке" : panel.section === 0 ? "Нет участников" : "Пока ничего нет"
                    color: theme.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                }
            }
            delegate: Loader {
                id: result
                objectName: "profileResultRow"
                required property var modelData
                readonly property bool nearViewport: panel.visible && y + height > results.contentY - results.height && y < results.contentY + 2 * results.height
                readonly property bool current: ListView.isCurrentItem && memberNavigation.activeFocus
                width: results.width
                height: item ? item.implicitHeight : 0
                sourceComponent: panel.section === 0 ? memberComponent : panel.section === 1 ? photosComponent : attachmentComponent
                Component {
                    id: memberComponent
                    ColumnLayout {
                        ItemDelegate {
                            id: member
                            objectName: "profileMemberButton"
                            Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                            implicitHeight: 72; padding: 12
                            focusPolicy: Qt.NoFocus
                            Accessible.name: result.modelData.title || "Участник"
                            Accessible.role: Accessible.Button
                            Accessible.onPressAction: clicked()
                            onClicked: panel.presentPerson(result.modelData, panel.returnFocus)
                            background: Rectangle {
                                radius: 12
                                color: member.down ? theme.pressed : member.hovered || result.current ? theme.secondary : "transparent"
                                border.color: result.current ? theme.accent : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 14
                                ProfileAvatar { objectName: "profileMemberAvatar"; source: result.modelData.avatar || ""; name: result.modelData.title || ""; active: result.nearViewport }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 4
                                    Label { Layout.fillWidth: true; text: result.modelData.title || "Участник"; textFormat: Text.PlainText; color: theme.ink; font.pixelSize: 16; elide: Text.ElideRight }
                                    Label {
                                        Layout.fillWidth: true
                                        text: result.modelData.is_owner ? "Создатель беседы" : result.modelData.is_admin ? "Администратор" : panel.activity(result.modelData)
                                        visible: text.length > 0; color: result.modelData.online ? theme.accent : theme.muted; font.pixelSize: 13; elide: Text.ElideRight
                                    }
                                }
                                ToolButton {
                                    objectName: "profileMemberMenuButton"
                                    Accessible.name: "Действия с участником"
                                    implicitWidth: 32; implicitHeight: 32
                                    contentItem: ActionIcon { kind: "more"; stroke: theme.muted }
                                    background: Rectangle { radius: 8; color: parent.hovered ? theme.divider : "transparent"; border.color: parent.visualFocus ? theme.accent : "transparent" }
                                    onClicked: memberMenu.popup()
                                }
                            }
                            AppMenu {
                                id: memberMenu
                                AppMenuItem { kind: "person"; text: "Открыть профиль"; onTriggered: panel.presentPerson(result.modelData, panel.returnFocus) }
                                AppMenuItem { kind: "people"; text: "Управление участниками…"; visible: panel.manage && !result.modelData.is_owner; onTriggered: panel.actionRequested("members") }
                            }
                        }
                    }
                }
                Component {
                    id: photosComponent
                    RowLayout {
                        spacing: 3
                        Repeater {
                            model: result.modelData
                            delegate: ItemDelegate {
                                id: thumbnail
                                objectName: "profilePhotoButton"
                                required property var modelData
                                readonly property var attachment: (modelData.attachments || [])[0] || {}
                                Layout.preferredWidth: (results.width - 6) / 3
                                Layout.preferredHeight: Layout.preferredWidth
                                padding: 0
                                Accessible.name: "Открыть фотографию"
                                contentItem: CachedImage {
                                    objectName: "profilePhotoThumbnail"
                                    remoteSource: PhotoLayout.thumbnail(thumbnail.attachment, 360, 360)
                                    active: result.nearViewport
                                    sourceSize.width: 360; sourceSize.height: 360
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                                background: Rectangle { color: theme.secondary; border.color: thumbnail.visualFocus ? theme.accent : "transparent"; border.width: 2 }
                                onClicked: client.actions.openAttachment(attachment, controller.results.map(function(row) { return (row.attachments || [])[0] }).filter(function(item) { return !!item }))
                            }
                        }
                        Item { Layout.fillWidth: true; visible: result.modelData.length < 3 }
                    }
                }
                Component {
                    id: attachmentComponent
                    ColumnLayout {
                        objectName: "profileAttachmentContent"
                        AttachmentView { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; attachments: result.modelData.attachments || []; loadImages: result.nearViewport; animationsEnabled: result.nearViewport && panel.opened && !client.reduceMotion }
                        Button { Layout.leftMargin: 16; visible: !!result.modelData.cmid; text: "К сообщению"; onClicked: { client.openMessage(panel.peer, String(result.modelData.cmid)); panel.close() } }
                    }
                }
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
    Timer { id: profileSearchDelay; interval: 150; onTriggered: if (panel.visible && panel.searchInput) panel.searchInput.applySearch() }
}
