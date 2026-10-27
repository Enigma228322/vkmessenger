import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: sheet
    objectName: "actionSheet"
    modal: true
    anchors.centerIn: parent
    width: Math.min(650, parent.width - 48)
    height: Math.min(650, parent.height - 48)
    padding: 24
    standardButtons: Dialog.Close
    property string mode: ""
    property string peer: ""
    property var cmids: []
    property var selectedPeers: []
    property var localPeers: []
    property string sourceText: ""
    property bool withinChat: true
    property bool canDeleteForAll: false
    property string attachmentType: "photo"
    property bool reducedMotion: false
    onClosed: {
        searchDelay.stop()
        if (picker || mode === "search") controller.reset()
    }
    readonly property bool picker: ["forward", "create", "add", "peers"].indexOf(mode) >= 0
    readonly property bool messageResults: ["search", "important", "attachments"].indexOf(mode) >= 0
    readonly property var controller: messenger.actions
    title: ({edit: "Редактирование сообщения", delete: "Удаление сообщений", forward: "Переслать сообщения", create: "Новая беседа", add: "Добавить участников", members: "Участники беседы", rename: "Название беседы", invite: "Приглашение в беседу", join: "Вступить по приглашению", search: "Поиск сообщений", peers: "Новый диалог", important: "Важные сообщения", attachments: "Вложения переписки", reacted: "Реакции", stickers: "Стикеры"})[mode] || "Беседа"
    background: Rectangle { radius: 18; color: sheet.palette.window; border.color: sheet.palette.mid }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: sheet.reducedMotion ? 0 : 150 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: sheet.reducedMotion ? 0 : 100 } }
    function show(kind, contextPeer, ids, text) {
        mode = kind; peer = contextPeer || ""; cmids = ids || []; sourceText = text || ""
        selectedPeers = []; localPeers = []; controller.reset()
        query.text = ""; editor.text = kind === "edit" || kind === "rename" ? sourceText : ""
        removeForEveryone.checked = false
        for (let i = 0; i < messenger.conversations.rowCount(); ++i) localPeers.push(messenger.conversations.get(i))
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
    function scheduleSearch() {
        searchDelay.stop()
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
            onTextEdited: sheet.scheduleSearch()
            onInputMethodComposingChanged: sheet.scheduleSearch()
            onAccepted: { sheet.controller.reset(); sheet.runSearch() }
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
        Label { visible: sheet.selectedPeers.length > 0; text: "Выбрано: " + sheet.selectedPeers.length; color: "#0077ff" }
        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: sheet.picker || sheet.messageResults || sheet.mode === "members" || sheet.mode === "reacted"
            clip: true
            spacing: 8
            model: sheet.picker && !query.text.length ? sheet.localPeers : sheet.controller.results
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: resultRow
                required property var modelData
                width: ListView.view.width
                highlighted: sheet.selectedPeers.indexOf(String(modelData.peer)) >= 0
                visible: !(sheet.mode === "create" || sheet.mode === "add") || messenger.isPerson(String(modelData.peer))
                height: visible ? contentItem.implicitHeight + 20 : 0
                contentItem: ColumnLayout {
                    Label { Layout.fillWidth: true; text: resultRow.modelData.title || "Сообщение"; font.bold: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                    Label { Layout.fillWidth: true; text: resultRow.modelData.text || (sheet.mode === "members" ? (resultRow.modelData.is_owner ? "Создатель" : resultRow.modelData.is_admin ? "Администратор" : "Участник") : ""); visible: text.length > 0; wrapMode: Text.Wrap; maximumLineCount: 4; textFormat: Text.PlainText; color: "#64748b" }
                    AttachmentView { Layout.fillWidth: true; attachments: resultRow.modelData.attachments || []; animationsEnabled: sheet.visible && !sheet.reducedMotion }
                    RowLayout {
                        visible: sheet.mode === "members" && messenger.canManageChat && !resultRow.modelData.is_owner
                        Button { text: resultRow.modelData.is_admin ? "Снять администратора" : "Назначить администратором"; enabled: !sheet.controller.busy; onClicked: sheet.controller.setMemberRole(sheet.peer, String(resultRow.modelData.peer), !resultRow.modelData.is_admin) }
                        Button { text: resultRow.modelData.is_restricted_to_write ? "Разрешить писать" : "Ограничить на час"; enabled: !sheet.controller.busy; onClicked: sheet.controller.restrictMember(sheet.peer, String(resultRow.modelData.peer), !resultRow.modelData.is_restricted_to_write) }
                        Button { text: "Удалить"; enabled: !sheet.controller.busy && !!resultRow.modelData.can_kick; onClicked: sheet.controller.removeMember(sheet.peer, String(resultRow.modelData.peer)) }
                    }
                }
                onClicked: {
                    if (sheet.picker) sheet.choose(modelData)
                    else if (sheet.messageResults && modelData.cmid && modelData.peer) { messenger.openMessage(String(modelData.peer), String(modelData.cmid)); sheet.close() }
                }
            }
            footer: Button {
                visible: (sheet.messageResults || sheet.mode === "members") && sheet.controller.hasMore
                objectName: "actionSearchMore"
                text: "Показать ещё"; enabled: !sheet.controller.busy && !searchDelay.running
                onClicked: {
                    if (sheet.mode === "members") sheet.controller.loadMembers(sheet.peer, true)
                    else if (sheet.mode === "search") sheet.controller.search(query.text, sheet.withinChat ? sheet.peer : "", true)
                    else if (sheet.mode === "important") sheet.controller.loadImportant(true)
                    else sheet.controller.loadAttachments(sheet.peer, sheet.attachmentType, true)
                }
            }
            Label { anchors.centerIn: parent; visible: results.count === 0 && !sheet.controller.busy && !searchDelay.running && !query.inputMethodComposing && !sheet.controller.error.length; text: query.text.trim().length ? "Ничего не найдено" : sheet.mode === "search" ? "Введите поисковый запрос" : "Пока ничего нет"; color: "#788697" }
        }
        GridView {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: sheet.mode === "stickers"
            clip: true; cellWidth: 100; cellHeight: 115
            model: sheet.mode === "stickers" ? sheet.controller.results : []
            delegate: Item {
                required property var modelData
                width: 100; height: 115
                Image { anchors.fill: parent; anchors.margins: 12; source: { messenger.media.revision; return (parent.modelData.images || []).length ? messenger.media.source(parent.modelData.images[parent.modelData.images.length - 1].url) : "" } asynchronous: true; fillMode: Image.PreserveAspectFit }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; cursorShape: Qt.PointingHandCursor; onClicked: function(mouse) { if (mouse.button === Qt.RightButton) sheet.controller.favoriteSticker(modelData.sticker_id, true); else sheet.controller.sendSticker(sheet.peer, modelData.sticker_id, modelData) } }
            }
        }
        BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: sheet.controller.busy; visible: running }
        Label { Layout.fillWidth: true; visible: sheet.controller.error.length > 0; text: sheet.controller.error; color: "#b3432a"; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        Button {
            objectName: "actionSubmit"
            Layout.alignment: Qt.AlignRight
            visible: ["edit", "delete", "forward", "create", "add", "rename", "join"].indexOf(sheet.mode) >= 0
            enabled: !sheet.controller.busy && (["forward", "create", "add"].indexOf(sheet.mode) < 0 || sheet.selectedPeers.length > 0)
            text: ({edit: "Сохранить", delete: "Удалить", forward: "Переслать", create: "Создать беседу", add: "Добавить", rename: "Сохранить", join: "Вступить"})[sheet.mode] || "Сохранить"
            onClicked: sheet.submit()
        }
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
