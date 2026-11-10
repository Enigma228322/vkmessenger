import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "PhotoLayout.js" as PhotoLayout

Dialog {
    id: sheet
    objectName: "actionSheet"
    modal: true
    anchors.centerIn: parent
    width: Math.min(650, parent.width - 48)
    height: Math.min(650, parent.height - 48)
    padding: 24
    property string mode: ""
    property string peer: ""
    property var cmids: []
    property var selectedPeers: []
    property string sourceText: ""
    property bool withinChat: true
    property bool canDeleteForAll: false
    property string attachmentType: "photo"
    property bool reducedMotion: false
    onClosed: {
        searchDelay.stop()
        if (picker || nativeMessageResults) controller.reset()
    }
    readonly property bool picker: ["forward", "create", "add", "peers"].indexOf(mode) >= 0
    readonly property bool peoplePicker: mode === "create" || mode === "add"
    readonly property bool localChoices: picker && !query.text.trim().length
    readonly property bool messageResults: ["search", "important", "attachments"].indexOf(mode) >= 0
    readonly property bool nativeMessageResults: mode === "search" || mode === "important"
    readonly property var remoteRows: {
        if (!visible || nativeMessageResults || localChoices || !(picker || messageResults || mode === "members" || mode === "reacted")) return []
        return peoplePicker ? controller.results.filter(function(row) { return messenger.isPerson(String(row.peer)) }) : controller.results
    }
    readonly property var controller: messenger.actions
    Theme { id: design; dark: messenger.darkTheme }
    Binding { target: messenger.conversationChoices; property: "peopleOnly"; value: sheet.peoplePicker }
    title: ({edit: "Редактирование сообщения", delete: "Удаление сообщений", forward: "Переслать сообщения", create: "Новая беседа", add: "Добавить участников", members: "Участники беседы", rename: "Название беседы", invite: "Приглашение в беседу", join: "Вступить по приглашению", search: "Поиск сообщений", peers: "Новый диалог", important: "Важные сообщения", attachments: "Вложения переписки", reacted: "Реакции", stickers: "Стикеры"})[mode] || "Беседа"
    background: Rectangle { radius: 18; color: sheet.palette.window; border.color: sheet.palette.mid }
    header: Label {
        text: sheet.title; color: design.ink
        leftPadding: 24; rightPadding: 24; topPadding: 20; bottomPadding: 8
        font.pixelSize: 18; font.weight: Font.DemiBold; wrapMode: Text.Wrap
    }
    footer: Item {
        implicitHeight: 68
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.topMargin: 8
            spacing: 10
            Item { Layout.fillWidth: true }
            Button {
                text: "Закрыть"; implicitHeight: 40
                background: Rectangle { radius: 8; color: parent.hovered ? design.hover : design.secondary; border.color: parent.visualFocus ? design.accent : "transparent" }
                onClicked: sheet.close()
            }
            Button {
                objectName: "actionSubmit"
                implicitHeight: 40
                visible: ["edit", "delete", "forward", "create", "add", "rename", "join"].indexOf(sheet.mode) >= 0
                enabled: !sheet.controller.busy && (["forward", "create", "add"].indexOf(sheet.mode) < 0 || sheet.selectedPeers.length > 0)
                text: ({edit: "Сохранить", delete: "Удалить", forward: "Переслать", create: "Создать беседу", add: "Добавить", rename: "Сохранить", join: "Вступить"})[sheet.mode] || "Сохранить"
                contentItem: Text { text: parent.text; color: parent.enabled ? "white" : design.muted; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 8; color: parent.enabled ? design.accent : design.secondary; border.color: parent.visualFocus ? design.ink : "transparent" }
                onClicked: sheet.submit()
            }
        }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: sheet.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: sheet.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic } }
    function show(kind, contextPeer, ids, text) {
        mode = kind; peer = contextPeer || ""; cmids = ids || []; sourceText = text || ""
        selectedPeers = []; controller.reset()
        query.text = ""; editor.text = kind === "edit" || kind === "rename" ? sourceText : ""
        removeForEveryone.checked = false
        results.currentIndex = -1
        open()
        if (mode === "members") controller.loadMembers(peer)
        else if (mode === "invite") controller.loadInvite(peer)
        else if (mode === "important") controller.loadImportant()
        else if (mode === "reacted") controller.loadReactedPeers(peer, cmids[0] || "", 0)
        else if (mode === "attachments") controller.loadAttachments(peer, attachmentType)
        else if (mode === "stickers") controller.loadStickers(false)
        if (picker || mode === "search") query.forceActiveFocus()
        else editor.forceActiveFocus()
    }
    function choose(person) {
        const id = String(person.peer)
        if (mode === "peers") { messenger.selectConversation(id, person.title || "Диалог"); close(); return }
        if (selectedPeers.indexOf(id) >= 0) selectedPeers = selectedPeers.filter(function(p) { return p !== id })
        else selectedPeers = selectedPeers.concat([id])
    }
    function activateResult(row) {
        if (!row) return
        if (picker) choose(row)
        else if (messageResults && row.cmid && row.peer) { messenger.openMessage(String(row.peer), String(row.cmid)); close() }
    }
    function activateCurrent() {
        if (results.currentIndex < 0 || results.currentIndex >= results.count) return
        activateResult(localChoices ? messenger.conversationChoices.get(results.currentIndex) : nativeMessageResults ? controller.messageModel.get(results.currentIndex) : remoteRows[results.currentIndex])
    }
    function focusResults(direction) {
        if (query.inputMethodComposing || !results.count) return
        results.currentIndex = direction > 0 ? 0 : results.count - 1
        results.forceActiveFocus()
        results.positionViewAtIndex(results.currentIndex, ListView.Contain)
    }
    function scheduleSearch() {
        searchDelay.stop()
        results.currentIndex = -1
        controller.reset()
        if (query.text.trim().length && !query.inputMethodComposing) searchDelay.restart()
    }
    function runSearch() {
        searchDelay.stop()
        if (!query.text.trim().length || query.inputMethodComposing) return
        if (mode === "search") controller.search(query.text, withinChat ? peer : "", false)
        else if (mode === "forward") controller.searchPeers(query.text)
        else controller.searchPeople(query.text)
    }
    function submit() {
        if (controller.busy) return
        if (mode === "edit") controller.editMessage(peer, cmids[0], editor.text)
        else if (mode === "delete") controller.deleteMessages(peer, cmids, removeForEveryone.checked)
        else if (mode === "forward") { for (const destination of selectedPeers) controller.forwardMessages(peer, cmids, destination, editor.text) }
        else if (mode === "create") controller.createChat(editor.text, selectedPeers)
        else if (mode === "add") controller.addMembers(peer, selectedPeers)
        else if (mode === "rename") controller.renameChat(peer, editor.text)
        else if (mode === "join") controller.joinInvite(editor.text)
    }
    component ResultCard: ItemDelegate {
        id: card
        objectName: "actionResultRow"
        required property var row
        readonly property bool nearViewport: sheet.visible && ListView.view && y + height > ListView.view.contentY - ListView.view.height && y < ListView.view.contentY + 2 * ListView.view.height
        width: ListView.view ? ListView.view.width : 0
        implicitHeight: contentItem.implicitHeight + 20
        highlighted: sheet.selectedPeers.indexOf(String(row.peer)) >= 0
        padding: 10
        Accessible.role: Accessible.ListItem
        Accessible.name: (row.title || "Сообщение") + (highlighted ? ", выбрано" : "")
        Accessible.selected: highlighted
        Accessible.onPressAction: sheet.activateResult(row)
        background: Rectangle {
            radius: 12
            color: card.highlighted ? design.selected : card.hovered ? design.hover : sheet.nativeMessageResults ? design.secondary : "transparent"
            border.color: card.ListView.view && card.ListView.view.activeFocus && card.ListView.isCurrentItem ? design.accent : card.highlighted ? design.focusBorder : "transparent"
        }
        contentItem: ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Rectangle {
                    Layout.preferredWidth: 40; Layout.preferredHeight: 40; radius: 20; color: design.secondary
                    visible: sheet.picker || sheet.mode === "members" || sheet.mode === "reacted"
                    Text { anchors.centerIn: parent; text: String(card.row.title || "?").substring(0, 1).toUpperCase(); color: design.accent; font.pixelSize: 17 }
                    CachedAvatar { anchors.fill: parent; remoteSource: card.row.avatar || "" }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Text { Layout.fillWidth: true; text: card.row.title || "Сообщение"; color: design.ink; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Text {
                            objectName: "actionResultDate"
                            visible: sheet.nativeMessageResults && Number(card.row.date || 0) > 0
                            text: visible ? Qt.formatDateTime(new Date(Number(card.row.date) * 1000), "dd.MM.yyyy · HH:mm") : ""
                            color: design.muted; font.pixelSize: 11
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: card.row.text || (sheet.picker ? card.row.preview || "" : sheet.mode === "members" ? (card.row.is_owner ? "Создатель" : card.row.is_admin ? "Администратор" : "Участник") : "")
                        visible: text.length > 0; color: design.muted; font.pixelSize: 12
                        wrapMode: sheet.picker ? Text.NoWrap : Text.Wrap; maximumLineCount: sheet.picker ? 1 : 4; elide: Text.ElideRight; textFormat: Text.PlainText
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 22; Layout.preferredHeight: 22; radius: 11
                    visible: sheet.picker && sheet.mode !== "peers"
                    color: card.highlighted ? design.accent : "transparent"; border.color: card.highlighted ? design.accent : design.muted
                    ActionIcon { anchors.centerIn: parent; width: 17; height: 17; visible: card.highlighted; kind: "select"; stroke: "white" }
                }
            }
            Loader {
                Layout.fillWidth: true
                active: sheet.messageResults && (card.row.attachments || []).length > 0
                visible: active
                Layout.preferredHeight: item ? item.implicitHeight : 0
                sourceComponent: Component { AttachmentView { attachments: card.row.attachments || []; loadImages: card.nearViewport; animationsEnabled: card.nearViewport && !sheet.reducedMotion } }
            }
            Loader {
                Layout.fillWidth: true
                active: sheet.mode === "members" && messenger.canManageChat && !card.row.is_owner
                visible: active
                sourceComponent: RowLayout {
                    Button { text: card.row.is_admin ? "Снять администратора" : "Назначить администратором"; enabled: !sheet.controller.busy; onClicked: sheet.controller.setMemberRole(sheet.peer, String(card.row.peer), !card.row.is_admin) }
                    Button { text: card.row.is_restricted_to_write ? "Разрешить писать" : "Ограничить на час"; enabled: !sheet.controller.busy; onClicked: sheet.controller.restrictMember(sheet.peer, String(card.row.peer), !card.row.is_restricted_to_write) }
                    Button { text: "Удалить"; enabled: !sheet.controller.busy && !!card.row.can_kick; onClicked: sheet.controller.removeMember(sheet.peer, String(card.row.peer)) }
                }
            }
        }
        onClicked: sheet.activateResult(row)
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            visible: sheet.mode === "delete"
            text: "Удалить выбранные сообщения (" + sheet.cmids.length + ")? Восстановление доступно только там, где его допускает VK."
            wrapMode: Text.Wrap
        }
        CheckBox { id: removeForEveryone; text: "Удалить у всех"; visible: sheet.mode === "delete" && sheet.canDeleteForAll }
        Label { Layout.fillWidth: true; visible: sheet.mode === "forward"; text: sheet.sourceText || ("Сообщений: " + sheet.cmids.length); elide: Text.ElideRight; textFormat: Text.PlainText; color: "#788697" }
        TextField {
            id: query
            objectName: "actionSearch"
            Layout.fillWidth: true
            visible: sheet.picker || sheet.mode === "search"
            placeholderText: sheet.mode === "search" ? "Найти в сообщениях" : "Поиск людей и диалогов"
            Accessible.name: placeholderText
            leftPadding: 38; rightPadding: text.length ? 38 : 12
            color: design.ink; placeholderTextColor: design.muted; implicitHeight: 40
            background: Rectangle { radius: 10; color: design.secondary; border.color: query.activeFocus ? design.accent : design.divider }
            onTextEdited: sheet.scheduleSearch()
            onInputMethodComposingChanged: sheet.scheduleSearch()
            onAccepted: { sheet.controller.reset(); sheet.runSearch() }
            Keys.onDownPressed: function(event) { event.accepted = !inputMethodComposing; if (event.accepted) sheet.focusResults(1) }
            Keys.onUpPressed: function(event) { event.accepted = !inputMethodComposing; if (event.accepted) sheet.focusResults(-1) }
            Keys.onEscapePressed: function(event) { event.accepted = !inputMethodComposing && text.length > 0; if (event.accepted) { clear(); sheet.scheduleSearch() } }
            ActionIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; kind: "search"; stroke: design.muted }
            ToolButton {
                objectName: "clearActionSearch"
                anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                width: 28; height: 28; visible: query.text.length > 0; focusPolicy: Qt.NoFocus; Accessible.name: "Очистить поиск"
                contentItem: ActionIcon { kind: "close"; stroke: design.muted }
                background: Rectangle { radius: 7; color: parent.hovered ? design.divider : "transparent" }
                onClicked: { query.clear(); sheet.scheduleSearch(); query.forceActiveFocus() }
            }
        }
        CheckBox { objectName: "searchWithinChat"; visible: sheet.mode === "search" && sheet.peer.length > 0; text: "Только в этой переписке"; checked: sheet.withinChat; onToggled: { sheet.withinChat = checked; sheet.scheduleSearch() } }
        Label { visible: sheet.mode === "search" && sheet.controller.cachedResults; text: "Результаты из сохранённой истории"; color: "#788697" }
        RowLayout {
            visible: sheet.mode === "attachments"
            ComboBox { model: ["Фотографии", "Видео", "Аудио", "Файлы", "Ссылки"]; onActivated: { sheet.attachmentType = ["photo", "video", "audio", "doc", "link"][currentIndex]; sheet.controller.reset(); sheet.controller.loadAttachments(sheet.peer, sheet.attachmentType) } }
        }
        RowLayout {
            visible: sheet.mode === "stickers"
            Button { text: "Мои наборы"; onClicked: { sheet.controller.reset(); sheet.controller.loadStickers(false) } }
            Button { text: "Избранные"; onClicked: { sheet.controller.reset(); sheet.controller.loadStickers(true) } }
        }
        TextArea {
            id: editor
            objectName: "actionEditor"
            Layout.fillWidth: true
            Layout.preferredHeight: sheet.mode === "edit" ? 140 : 72
            visible: ["edit", "forward", "create", "rename", "join"].indexOf(sheet.mode) >= 0
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            placeholderText: sheet.mode === "create" || sheet.mode === "rename" ? "Название беседы" : sheet.mode === "join" ? "Ссылка-приглашение" : sheet.mode === "forward" ? "Добавить комментарий" : "Сообщение"
        }
        RowLayout {
            visible: sheet.mode === "invite"
            TextField { Layout.fillWidth: true; text: sheet.controller.inviteLink; readOnly: true; selectByMouse: true; placeholderText: "Получаем приглашение…" }
            Button { text: "Новая ссылка"; enabled: !sheet.controller.busy; onClicked: sheet.controller.loadInvite(sheet.peer, true) }
        }
        RowLayout {
            Layout.fillWidth: true; visible: sheet.picker && sheet.selectedPeers.length > 0
            Label { objectName: "recipientSelectionCount"; Layout.fillWidth: true; text: "Выбрано: " + sheet.selectedPeers.length; color: design.accent }
            ToolButton { objectName: "clearRecipientSelection"; text: "Сбросить"; Accessible.name: "Сбросить выбор получателей"; onClicked: sheet.selectedPeers = [] }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: sheet.nativeMessageResults && results.count > 0
            Text { objectName: "messageResultCount"; text: "Загружено: " + results.count; color: design.muted; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Text { text: "↑ ↓ — выбор · Enter — открыть"; color: design.muted; font.pixelSize: 11; visible: sheet.width >= 500 }
        }
        ListView {
            id: results
            objectName: "actionResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: sheet.picker || sheet.messageResults || sheet.mode === "members" || sheet.mode === "reacted"
            clip: true
            spacing: 4
            model: !sheet.visible ? null : sheet.localChoices ? messenger.conversationChoices : sheet.nativeMessageResults ? sheet.controller.messageModel : sheet.remoteRows
            delegate: ResultCard { required property var modelData; row: modelData }
            activeFocusOnTab: true
            keyNavigationEnabled: true
            keyNavigationWraps: false
            currentIndex: -1
            onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count) currentIndex = 0
            Keys.onReturnPressed: sheet.activateCurrent()
            Keys.onEnterPressed: sheet.activateCurrent()
            Keys.onSpacePressed: if (sheet.picker) sheet.activateCurrent()
            Keys.onEscapePressed: function(event) { event.accepted = query.visible; if (event.accepted) query.forceActiveFocus() }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home && count) { currentIndex = 0; positionViewAtBeginning(); event.accepted = true }
                else if (event.key === Qt.Key_End && count) { currentIndex = count - 1; positionViewAtEnd(); event.accepted = true }
            }
            ScrollBar.vertical: ScrollBar {}
            footer: Button {
                visible: (sheet.messageResults || sheet.mode === "members") && sheet.controller.hasMore
                height: visible ? implicitHeight : 0
                objectName: "actionSearchMore"
                text: "Показать ещё"; enabled: !sheet.controller.busy && !searchDelay.running
                onClicked: {
                    if (sheet.mode === "members") sheet.controller.loadMembers(sheet.peer, true)
                    else if (sheet.mode === "search") sheet.controller.search(query.text, sheet.withinChat ? sheet.peer : "", true)
                    else if (sheet.mode === "important") sheet.controller.loadImportant(true)
                    else sheet.controller.loadAttachments(sheet.peer, sheet.attachmentType, true)
                }
            }
            Column {
                objectName: "actionEmptyState"
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 32); spacing: 12
                visible: results.count === 0 && !sheet.controller.busy && !searchDelay.running && !query.inputMethodComposing && !sheet.controller.error.length
                ActionIcon { anchors.horizontalCenter: parent.horizontalCenter; width: 36; height: 36; kind: sheet.mode === "important" ? "important" : "search"; stroke: design.muted }
                Text { width: parent.width; text: query.text.trim().length ? "Ничего не найдено" : sheet.mode === "search" ? "Найти сообщение" : sheet.mode === "important" ? "Нет важных сообщений" : "Пока ничего нет"; color: design.ink; font.pixelSize: 16; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
                Text { width: parent.width; visible: sheet.mode === "search"; text: query.text.trim().length ? "Попробуйте другое слово или измените область поиска." : "Введите слово или фразу, чтобы найти сообщение."; color: design.muted; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
            }
        }
        GridView {
            id: stickers
            objectName: "actionStickers"
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: sheet.mode === "stickers"
            clip: true; cellWidth: 100; cellHeight: 115
            model: sheet.visible && sheet.mode === "stickers" ? sheet.controller.results : []
            delegate: Item {
                id: sticker
                required property var modelData
                width: 100; height: 115
                CachedImage {
                    objectName: "pickerStickerImage"
                    anchors.fill: parent; anchors.margins: 12
                    remoteSource: PhotoLayout.thumbnail({type: "photo", photo: {sizes: sticker.modelData.images || []}}, 200, 200)
                    active: sheet.visible && sticker.y + sticker.height > stickers.contentY - stickers.height && sticker.y < stickers.contentY + 2 * stickers.height
                    sourceSize.width: 200; sourceSize.height: 200; fillMode: Image.PreserveAspectFit
                }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; cursorShape: Qt.PointingHandCursor; onClicked: function(mouse) { if (mouse.button === Qt.RightButton) sheet.controller.favoriteSticker(modelData.sticker_id, true); else sheet.controller.sendSticker(sheet.peer, modelData.sticker_id, modelData) } }
            }
        }
        BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: sheet.controller.busy; visible: running }
        Label { Layout.fillWidth: true; visible: sheet.controller.error.length > 0; text: sheet.controller.error; color: "#b3432a"; wrapMode: Text.Wrap; textFormat: Text.PlainText }
    }
    Timer {
        id: searchDelay; interval: 350
        onTriggered: sheet.runSearch()
    }
    Connections {
        target: messenger.actions
        function onCompleted() {
            if (!sheet.visible) return
            if (sheet.mode === "members") sheet.controller.loadMembers(sheet.peer)
            else if (!sheet.controller.error.length) sheet.close()
        }
    }
}
