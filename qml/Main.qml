import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    width: 1180
    height: 780
    minimumWidth: 780
    minimumHeight: 560
    visible: true
    onActiveChanged: messenger.refreshPreferences()
    title: "VK"
    color: canvasColor
    font.family: Qt.platform.os === "osx" ? ".AppleSystemUIFont" : "sans-serif"
    font.pixelSize: 14
    Theme { id: design; dark: window.darkTheme }
    property bool flatMessages: messenger.flatMessages
    property color ink: design.ink
    property color muted: design.muted
    property color accent: design.accent
    property bool restoringDraft: false
    property string editingPeer: ""
    property string editingMessage: ""
    property bool editingHasAttachments: false
    readonly property bool editing: editingMessage.length > 0
    property var submittedDrafts: ({})
    property string draftPeer: ""
    readonly property var draftAttachments: {
        const pending = messenger.transfers.pending, result = []
        for (let i = 0; i < pending.length; ++i)
            if (pending[i].peer === window.draftPeer) result.push(Object.assign({}, pending[i], {sourceIndex: i}))
        return result
    }
    property bool wasAtEnd: true
    property string search: ""
    property int conversationFilter: 0
    property bool unreadOnly: false
    property string savedAnchor: ""
    property real savedOffset: 0
    property string geometryAnchor: ""
    property real geometryOffset: 0
    property bool restoringGeometry: false
    property string previousLast: ""
    property int newMessages: 0
    property var selectedIds: []
    property bool darkTheme: messenger.darkTheme
    readonly property bool motionEnabled: window.active && !messenger.reduceMotion
    property color surface: design.surface
    property color secondarySurface: design.secondary
    property color canvasColor: design.canvas
    palette.window: surface
    palette.base: surface
    palette.text: ink
    palette.windowText: ink
    palette.buttonText: ink
    palette.button: design.secondary
    palette.light: design.secondary
    palette.midlight: design.divider
    palette.mid: design.divider
    palette.dark: design.divider
    palette.placeholderText: design.muted
    palette.highlight: accent
    palette.highlightedText: darkTheme ? "#ffffff" : "#172536"
    function messageActions(kind, item) {
        if (kind === "edit") {
            if (messenger.actions.busy || item.local || !item.outgoing) return
            editingPeer = messenger.selectedPeer
            editingMessage = String(item.id)
            editingHasAttachments = (item.attachments || []).length > 0
            restoringDraft = true
            composer.text = item.raw ? item.raw.text || "" : item.text || ""
            restoringDraft = false
            composer.forceActiveFocus()
            composer.selectAll()
            return
        }
        const ids = selectedIds.length && (kind === "forward" || kind === "delete") ? selectedIds : [String(item.id)]
        actionSheet.canDeleteForAll = !!item.outgoing || messenger.canManageChat
        actionSheet.show(kind, messenger.selectedPeer, ids, item.raw ? (kind === "edit" ? item.raw.text || "" : item.raw.text || item.text || "") : item.text || "")
    }
    function cancelEditing() {
        editingMessage = ""
        editingPeer = ""
        restoreDraft()
        composer.forceActiveFocus()
    }
    Binding { target: messenger.filteredConversations; property: "query"; value: window.search }
    Binding { target: messenger.filteredConversations; property: "category"; value: window.conversationFilter }
    Binding { target: messenger.filteredConversations; property: "unreadOnly"; value: window.unreadOnly }
    readonly property string pinnedBoundaryPeer: messenger.filteredConversations.pinnedBoundaryPeer
    readonly property bool conversationFiltersActive: search.trim().length > 0 || conversationFilter !== 0 || unreadOnly || messenger.folders.selected.length > 0
    function resetConversationFilters() {
        searchField.clear()
        window.search = ""
        window.conversationFilter = 0
        window.unreadOnly = false
        messenger.folders.selected = ""
    }
    function toggleSelection(id) {
        selectedIds = selectedIds.indexOf(id) >= 0 ? selectedIds.filter(function(x) { return x !== id }) : selectedIds.concat([id])
    }
    function deliveryText(item) {
        if (!item.local) return String(item.time || "").split(" · ").pop() + (item.edited ? " · изм." : "")
        return ({queued: "В очереди ◷", uncertain: "Доставка не подтверждена ◷", sending: "Отправляется…", sent: "Отправлено", failed: "Не отправлено · повторить"})[item.delivery] || "Сохраняется…"
    }
    function isNewerId(a, b) {
        if (!/^\d+$/.test(a) || !/^\d+$/.test(b)) return false
        return a.length !== b.length ? a.length > b.length : a > b
    }
    function goToEnd() {
        history.cancelFlick()
        window.wasAtEnd = true
        window.newMessages = 0
        history.forceLayout()
        history.positionViewAtEnd()
        history.contentY = Math.max(history.originY - history.topMargin, history.originY + history.contentHeight - history.height + history.bottomMargin)
        tailPosition.restart()
    }

    Timer {
        id: tailPosition
        interval: 16
        onTriggered: {
            if (window.wasAtEnd) {
                history.forceLayout()
                history.positionViewAtEnd()
                history.contentY = Math.max(history.originY - history.topMargin, history.originY + history.contentHeight - history.height + history.bottomMargin)
            }
        }
    }

    function visibleHistoryRows() {
        // ListView creates only the viewport and its small cache. Child order is
        // not model order after scrolling or reconciliation, so sort by position.
        return history.contentItem.children.filter(function(row) {
            return row.objectName === "messageRow" && row.visible && row.y + row.height > history.contentY && row.y < history.contentY + history.height
        }).sort(function(a, b) { return a.y - b.y })
    }

    function visibleReadCmid() {
        let cmid = ""
        for (const entry of visibleHistoryRows()) {
            if (entry.item.local || entry.item.gap) continue
            if (entry.height > history.height ? entry.y <= history.contentY : entry.y + entry.height <= history.contentY + history.height + 1) {
                const candidate = String(entry.item.cmid || "")
                if (window.isNewerId(candidate, cmid) || (!cmid.length && /^\d+$/.test(candidate))) cmid = candidate
            }
        }
        return cmid
    }

    function markVisibleMessagesRead() {
        if (!window.active || !history.visible || messenger.loading || messenger.demo) return
        const cmid = visibleReadCmid()
        if (cmid.length) messenger.markVisibleRead(cmid)
    }

    Timer { interval: 700; running: window.active && messenger.connected && !messenger.demo; repeat: true; onTriggered: window.markVisibleMessagesRead() }

    function showPerson(peer, name, avatar, focusItem) {
        profilePanel.presentPerson({peer: String(peer), title: name || "Профиль", avatar: avatar || ""}, focusItem)
    }

    function selectChat(peer, name) {
        if (draftPeer.length && !window.editing) messenger.setDraftText(draftPeer, composer.text)
        window.editingMessage = ""
        window.editingPeer = ""
        messenger.selectConversation(peer, name)
        draftPeer = peer
        restoreDraft()
        wasAtEnd = true
        newMessages = 0
        composer.forceActiveFocus()
    }

    function restoreDraft() {
        if (window.editing && window.editingPeer === messenger.selectedPeer) return
        restoringDraft = true
        composer.text = messenger.draftText
        restoringDraft = false
    }
    function focusComposer() { composer.forceActiveFocus() }

    component ActionButton: Button {
        id: action
        property bool primary: false
        property bool quiet: false
        implicitHeight: 40
        leftPadding: 16
        rightPadding: 16
        font.weight: Font.DemiBold
        scale: action.down && window.motionEnabled ? 0.97 : 1
        Behavior on scale { enabled: window.motionEnabled; NumberAnimation { duration: design.pressDuration; easing.type: Easing.OutCubic } }
        background: Rectangle {
            radius: 10
            color: !action.enabled ? design.secondary : action.primary
                ? (action.down ? "#0066db" : action.hovered ? "#1683ff" : window.accent)
                : (action.hovered ? design.hover : action.quiet ? "transparent" : window.palette.button)
            border.color: action.activeFocus ? window.accent : "transparent"
            Behavior on color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
        }
        contentItem: Text {
            text: action.text
            color: !action.enabled ? "#98a3af" : action.primary ? "white" : window.ink
            font: action.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component HeaderAction: ToolButton {
        id: headerAction
        property string kind: ""
        property string label: ""
        implicitWidth: 36
        implicitHeight: 36
        hoverEnabled: true
        Accessible.name: label
        ToolTip.visible: hovered
        ToolTip.text: label
        ToolTip.delay: 650
        scale: down && window.motionEnabled ? 0.94 : 1
        Behavior on scale { enabled: window.motionEnabled; NumberAnimation { duration: design.pressDuration; easing.type: Easing.OutCubic } }
        background: Rectangle {
            radius: 10
            color: headerAction.down ? design.pressed : headerAction.hovered ? design.hover : "transparent"
            border.width: 1
            border.color: headerAction.visualFocus ? window.accent : "transparent"
            Behavior on color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
        }
        contentItem: ActionIcon { opacity: headerAction.enabled ? 1 : 0.4; kind: headerAction.kind; stroke: headerAction.hovered || headerAction.visualFocus ? window.ink : window.muted; Behavior on stroke { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } } }
    }

    component FilterTab: Button {
        id: tab
        implicitHeight: 34
        leftPadding: 12; rightPadding: 12
        font.pixelSize: 13; font.weight: Font.DemiBold
        contentItem: Text {
            text: tab.text; font: tab.font
            color: tab.highlighted || tab.hovered ? window.ink : window.muted
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 9
            color: tab.highlighted ? design.selected : tab.hovered ? design.hover : "transparent"
            border.color: tab.visualFocus ? window.accent : "transparent"
            Behavior on color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
        }
    }

    component Input: TextField {
        id: input
        implicitHeight: 42
        color: window.ink
        selectByMouse: true
        leftPadding: 12
        placeholderTextColor: "#8c98a6"
        background: Rectangle {
            radius: 9
            color: window.secondarySurface
            border.color: input.activeFocus ? window.accent : design.divider
            Behavior on border.color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
        }
    }

    component TokenLogin: ColumnLayout {
        id: tokenLogin
        property string fieldName: "accessTokenInput"
        spacing: 10
        function submit() {
            if (!messenger.authorizing && accessToken.text.trim().length > 0 && (!community.checked || group.text.trim().length > 0))
                messenger.loginWithToken(accessToken.text, community.checked ? group.text.trim() : "")
        }
        Text {
            text: "access_token"
            color: window.ink
            font.weight: Font.DemiBold
        }
        Input {
            id: accessToken
            objectName: tokenLogin.fieldName
            Layout.fillWidth: true
            placeholderText: "Вставьте пользовательский access_token"
            echoMode: revealToken.checked ? TextInput.Normal : TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            enabled: !messenger.authorizing
            Accessible.name: "access_token"
            onAccepted: tokenLogin.submit()
        }
        CheckBox {
            id: revealToken
            text: "Показать токен"
        }
        CheckBox { id: community; text: "Сообщения сообщества" }
        Input { id: group; Layout.fillWidth: true; visible: community.checked; placeholderText: "ID сообщества без минуса" }
        ActionButton {
            objectName: tokenLogin.fieldName + "Connect"
            Layout.fillWidth: true
            text: messenger.authorizing ? "Подключение…" : "Подключиться"
            primary: true
            enabled: accessToken.text.trim().length > 0 && !messenger.authorizing && (!community.checked || group.text.trim().length > 0)
            onClicked: tokenLogin.submit()
        }
        Text {
            Layout.fillWidth: true
            text: "Токен сохраняется в Keychain этого Mac."
            color: window.muted
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        Connections {
            target: messenger
            function onChanged() {
                if (messenger.connected && !messenger.demo) {
                    accessToken.clear()
                    revealToken.checked = false
                }
            }
        }
    }

    component Avatar: Rectangle {
        id: avatar
        property string name: ""
        property string source: ""
        property int tint: 0
        property bool animateImage: false
        width: 46
        height: 46
        radius: width / 2
        color: ["#dceaff", "#e8e1ff", "#d9f0e8", "#ffe9da"][Math.abs(tint) % 4]
        Text {
            anchors.centerIn: parent
            text: avatar.name.substring(0, 1).toUpperCase()
            color: ["#3176c7", "#8263bf", "#3b8d76", "#b87542"][Math.abs(avatar.tint) % 4]
            font.pixelSize: 19
            font.weight: Font.DemiBold
        }
        CachedAvatar {
            objectName: "avatarImage"
            anchors.fill: parent
            remoteSource: avatar.source
            visible: status === Image.Ready
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { enabled: avatar.animateImage && window.motionEnabled; NumberAnimation { duration: design.motionDuration; easing.type: Easing.OutCubic } }
        }
    }

    Shortcut { sequences: [StandardKey.Refresh]; onActivated: messenger.reload() }
    Shortcut { sequences: [StandardKey.Find]; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settings.open() }

    Connections {
        target: messenger
        function onMessageSent(peer) {
            // Keep edits made while the previous text was being sent.
            const submitted = window.submittedDrafts[peer]
            if (peer === window.draftPeer) {
                if (composer.text === submitted) composer.clear()
            }
            delete window.submittedDrafts[peer]
            if (peer === messenger.selectedPeer) {
                window.wasAtEnd = true
                Qt.callLater(function() { history.positionViewAtEnd() })
            }
        }
        function onChanged() {
            if (messenger.browserLoginPending && (messenger.browserLoginUsesToken || messenger.redirectUri.indexOf("https://") === 0)) loginReturn.open()
            else if (!messenger.authorizing) loginReturn.close()
            if (messenger.selectedPeer !== window.draftPeer) {
                window.editingMessage = ""
                window.editingPeer = ""
                messagePopup.close()
                profilePanel.close()
                window.draftPeer = messenger.selectedPeer
                window.restoreDraft()
                window.newMessages = 0
                window.selectedIds = []
            }
            if (!messenger.connected) {
                window.submittedDrafts = ({})
                window.draftPeer = ""
                composer.clear()
            }
        }
        function onDraftChanged() { window.restoreDraft() }
        function onScrollToEnd() { Qt.callLater(window.goToEnd) }
        function onScrollToMessage(cmid) {
            window.wasAtEnd = false
            tailPosition.stop()
            Qt.callLater(function() { const i = messenger.messages.find(cmid); if (i >= 0) history.positionViewAtIndex(i, ListView.Center) })
        }
        function onHistoryAboutToChange() {
            messagePopup.close()
            if (window.draftPeer !== messenger.selectedPeer) {
                window.wasAtEnd = true; window.savedAnchor = ""; window.previousLast = ""; return
            }
            window.wasAtEnd = history.atYEnd && !messenger.hasNewer
            window.savedAnchor = ""
            window.previousLast = messenger.latestMessageCmid()
            const rows = window.visibleHistoryRows()
            if (rows.length) {
                window.savedAnchor = String(rows[0].item.id)
                window.savedOffset = history.contentY - rows[0].y
            }
        }
        function onHistoryChanged() {
            if (!window.wasAtEnd && window.previousLast.length)
                window.newMessages += messenger.newIncomingMessageCount(window.previousLast)
            const anchor = window.savedAnchor, offset = window.savedOffset, atEnd = window.wasAtEnd
            Qt.callLater(function() {
                if (atEnd) window.goToEnd()
                else if (anchor.length) {
                    const i = messenger.messages.find(anchor)
                    if (i >= 0) { history.positionViewAtIndex(i, ListView.Beginning); history.contentY += offset }
                }
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: errorRow.implicitHeight + 22
            visible: messenger.error.length > 0 || messenger.operationError.length > 0
            color: design.errorSurface
            RowLayout {
                id: errorRow
                anchors.fill: parent
                anchors.margins: 11
                Text {
                    Layout.fillWidth: true
                    text: messenger.error || messenger.operationError
                    color: design.errorText
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.pixelSize: 12
                }
                ActionButton { text: "Закрыть"; quiet: true; onClicked: { messenger.clearError(); messenger.operationError = "" } }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: Math.max(280, Math.min(330, window.width * 0.29))
                Layout.fillHeight: true
                color: window.surface
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            width: 36; height: 36; radius: 11; color: window.accent
                            Text { anchors.centerIn: parent; text: "VK"; color: "#ffffff"; font.pixelSize: 23; font.bold: true }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { text: "Мессенджер"; color: window.ink; font.pixelSize: 18; font.bold: true }

                        }
                        HeaderAction {
                            objectName: "navigationButton"
                            kind: "menu"; label: "Меню мессенджера"
                            onClicked: navigationMenu.popup()
                        }
                    }
                    Input {
                        id: searchField
                        objectName: "conversationSearch"
                        Layout.fillWidth: true
                        placeholderText: "Поиск переписок"
                        leftPadding: 40
                        rightPadding: text.length ? 40 : 12
                        Accessible.name: "Поиск переписок"
                        function applySearch() {
                            conversationSearchDelay.stop()
                            if (!inputMethodComposing) window.search = text.trim()
                        }
                        function focusResults(direction) {
                            if (inputMethodComposing) return
                            applySearch()
                            if (!chats.count) return
                            chats.currentIndex = direction > 0 ? 0 : chats.count - 1
                            chats.forceActiveFocus()
                            chats.positionViewAtIndex(chats.currentIndex, ListView.Contain)
                        }
                        onTextChanged: {
                            conversationSearchDelay.stop()
                            if (!text.length) applySearch()
                            else if (!inputMethodComposing) conversationSearchDelay.restart()
                        }
                        onInputMethodComposingChanged: {
                            conversationSearchDelay.stop()
                            if (!inputMethodComposing) conversationSearchDelay.restart()
                        }
                        Keys.onDownPressed: function(event) { event.accepted = !inputMethodComposing; if (event.accepted) focusResults(1) }
                        Keys.onUpPressed: function(event) { event.accepted = !inputMethodComposing; if (event.accepted) focusResults(-1) }
                        onAccepted: {
                            if (inputMethodComposing) return
                            applySearch()
                            chats.currentIndex = chats.count ? 0 : -1
                            chats.openCurrent()
                        }
                        Keys.onEscapePressed: function(event) { event.accepted = !inputMethodComposing && text.length > 0; if (event.accepted) clear() }
                        Timer { id: conversationSearchDelay; interval: 150; onTriggered: searchField.applySearch() }
                        ActionIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 20; height: 20; kind: "search"; stroke: window.muted }
                        HeaderAction {
                            objectName: "clearConversationSearch"
                            x: parent.width - width - 6; anchors.verticalCenter: parent.verticalCenter
                            width: 28; height: 28; visible: searchField.text.length > 0
                            kind: "close"; label: "Очистить поиск"; focusPolicy: Qt.NoFocus
                            onClicked: { searchField.clear(); searchField.forceActiveFocus() }
                        }
                    }
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        contentWidth: categoryTabs.implicitWidth
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        Row {
                            id: categoryTabs
                            spacing: 4
                            Repeater {
                                model: [{name:"Все", value:0}, {name:"Важные", value:2}, {name:"Архив", value:3}]
                                delegate: FilterTab {
                                    required property var modelData
                                    text: modelData.name
                                    highlighted: window.conversationFilter === modelData.value
                                    onClicked: window.conversationFilter = modelData.value
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: messenger.folders.items.length > 0
                        Flickable {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            clip: true
                            contentWidth: folderTabs.implicitWidth
                            boundsBehavior: Flickable.StopAtBounds
                            Row {
                                id: folderTabs
                                spacing: 4
                                Repeater {
                                    model: [{id:"", name:"Все папки"}].concat(messenger.folders.items)
                                    delegate: FilterTab {
                                        required property var modelData
                                        text: modelData.name
                                        highlighted: messenger.folders.selected === modelData.id
                                        onClicked: messenger.folders.selected = modelData.id
                                    }
                                }
                            }
                        }
                        ToolButton { text:"…"; Accessible.name:"Управление папками"; onClicked: folderSheet.show() }
                    }
                    ListView {
                        id: chats
                        objectName: "conversationList"
                        property Item hoveredChat: null
                        activeFocusOnTab: true
                        keyNavigationEnabled: true
                        keyNavigationWraps: false
                        currentIndex: -1
                        Accessible.name: "Переписки"
                        function openCurrent() {
                            if (currentIndex < 0 || currentIndex >= count) return
                            const row = messenger.filteredConversations.get(currentIndex)
                            window.selectChat(String(row.peer), row.title)
                        }
                        onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count) currentIndex = 0
                        Keys.onReturnPressed: openCurrent()
                        Keys.onEnterPressed: openCurrent()
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Home && count) { currentIndex = 0; positionViewAtBeginning(); event.accepted = true }
                            else if (event.key === Qt.Key_End && count) { currentIndex = count - 1; positionViewAtEnd(); event.accepted = true }
                        }
                        Keys.onEscapePressed: searchField.forceActiveFocus()
                        Connections {
                            target: messenger.filteredConversations
                            function onFilterChanged() { chats.currentIndex = -1 }
                        }
                        function hoverChat(row) {
                            if (moving) return
                            hoveredChat = row
                            chatHover.y = row.y + (row.startsUnpinned ? 12 : 0)
                        }
                        onMovementStarted: hoveredChat = null
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 0
                        model: messenger.filteredConversations
                        Rectangle {
                            id: chatHover
                            objectName: "conversationHover"
                            parent: chats.contentItem
                            z: -1
                            width: chats.width
                            height: 66
                            radius: 12
                            color: window.secondarySurface
                            opacity: chats.hoveredChat && chats.hoveredChat.visible && !chats.moving && window.active ? 1 : 0
                            Behavior on y {
                                enabled: window.motionEnabled && chatHover.opacity > 0.01 && !chats.moving
                                NumberAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic }
                            }
                            Behavior on opacity {
                                enabled: window.motionEnabled
                                NumberAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: chat
                            required property var item
                            required property int index
                            readonly property bool startsUnpinned: String(item.peer) === window.pinnedBoundaryPeer
                            width: ListView.view.width
                            height: 66 + (startsUnpinned ? 12 : 0)
                            radius: 12
                            border.width: chats.activeFocus && ListView.isCurrentItem ? 1 : 0
                            border.color: window.accent
                            Accessible.role: Accessible.ListItem
                            Accessible.name: item.title + (item.unread > 0 ? ", непрочитанных: " + item.unread : item.markedUnread ? ", не прочитано" : "")
                            Accessible.selected: messenger.selectedPeer === item.peer
                            Accessible.onPressAction: window.selectChat(chat.item.peer, chat.item.title)
                            color: messenger.selectedPeer === item.peer ? (window.darkTheme ? "#333333" : "#eaf3ff") : "transparent"
                            Behavior on color { enabled: window.motionEnabled && chat.visible && !chats.moving; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.verticalCenterOffset: chat.startsUnpinned ? 6 : 0
                                width: 3; height: 28; radius: 1.5
                                color: window.accent
                                opacity: messenger.selectedPeer === chat.item.peer ? 1 : 0
                                Behavior on opacity { enabled: window.motionEnabled; NumberAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
                            }
                            Rectangle {
                                objectName: "pinnedChatsDivider"
                                visible: chat.startsUnpinned
                                x: 10; y: 5
                                width: parent.width - 20; height: 1
                                color: design.divider
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                anchors.topMargin: 10 + (chat.startsUnpinned ? 12 : 0)
                                spacing: 11
                                Avatar { name: chat.item.title; source: chat.item.avatar || ""; tint: chat.index }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: chat.item.title; color: window.ink; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                                        ActionIcon { visible: !!chat.item.muted; kind: "muted"; stroke: window.muted; Layout.preferredWidth: 14; Layout.preferredHeight: 14 }
                                        Text { text: chat.item.time; color: window.muted; font.pixelSize: 10 }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: chat.item.preview.replace(/\n/g, " "); color: window.muted; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                                        Rectangle {
                                            visible: chat.item.unread > 0 || !!chat.item.markedUnread
                                            width: Math.max(20, unreadText.implicitWidth + 10); height: 20; radius: 10; color: chat.item.muted ? design.divider : window.accent
                                            Text { id: unreadText; anchors.centerIn: parent; text: chat.item.unread > 0 ? chat.item.unread : "•"; color: chat.item.muted ? window.ink : "#ffffff"; font.pixelSize: 10; font.bold: true }
                                        }
                                    }
                                }
                            }
                            MouseArea {
                                id: mouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: chats.hoverChat(chat)
                                onPositionChanged: if (containsMouse) chats.hoverChat(chat)
                                onExited: if (chats.hoveredChat === chat) chats.hoveredChat = null
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onPressed: function(event) {
                                    if (event.button === Qt.RightButton) {
                                        chats.currentIndex = chat.index
                                        window.selectChat(chat.item.peer, chat.item.title)
                                        conversationMenu.popup()
                                    }
                                }
                                onClicked: { chats.currentIndex = chat.index; window.selectChat(chat.item.peer, chat.item.title) }
                            }
                        }
                        Column {
                            objectName: "conversationEmptyState"
                            anchors.centerIn: parent
                            width: parent.width - 30
                            spacing: 12
                            visible: chats.count === 0
                            Text {
                                width: parent.width
                                text: window.conversationFiltersActive ? "Ничего не найдено" : messenger.connected ? "Здесь появятся ваши диалоги" : "Войдите, чтобы открыть переписку"
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                color: window.muted
                                lineHeight: 1.4
                            }
                            ActionButton {
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: window.conversationFiltersActive
                                text: "Сбросить фильтры"
                                quiet: true
                                onClicked: window.resetConversationFilters()
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 40; spacing: 10
                        ActionIcon { kind: "chat"; stroke: window.muted; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                        Label { text: "Только непрочитанные"; color: window.ink; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                        Switch {
                            id: unreadSwitch
                            objectName: "unreadConversationSwitch"
                            padding: 0
                            checked: window.unreadOnly
                            onToggled: window.unreadOnly = checked
                            Accessible.name: "Только непрочитанные"
                            indicator: Rectangle {
                                implicitWidth: 40; implicitHeight: 24; radius: 12
                                color: unreadSwitch.checked ? window.accent : design.divider
                                border.color: unreadSwitch.visualFocus ? window.accent : "transparent"
                                Behavior on color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration } }
                                Rectangle {
                                    x: unreadSwitch.checked ? 19 : 3; y: 3
                                    width: 18; height: 18; radius: 9
                                    color: unreadSwitch.checked ? "white" : window.muted
                                    Behavior on x { enabled: window.motionEnabled; NumberAnimation { duration: design.pressDuration; easing.type: Easing.OutCubic } }
                                }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: design.divider }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 7; height: 7; radius: 4; color: messenger.demo ? "#e4ae57" : messenger.connected ? "#57b894" : "#aab4c0" }
                        Text {
                            Layout.fillWidth: true
                            text: messenger.status || "Нет подключения"
                            color: window.muted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                        HeaderAction { kind: "retry"; label: "Обновить переписки"; implicitWidth: 28; implicitHeight: 28; onClicked: messenger.reload() }
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: design.divider }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.rightMargin: profilePanel.docked ? profilePanel.width : 0

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(420, parent.width - 70)
                    spacing: 18
                    visible: !messenger.connected || messenger.selectedPeer.length === 0
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 86; height: 86; radius: 27; color: "#e2eeff"
                        Text { anchors.centerIn: parent; text: "VK"; color: window.accent; font.pixelSize: 50; font.weight: Font.DemiBold }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: messenger.connected ? "С возвращением" : "Ваши разговоры.\nВ отдельном приложении."
                        font.pixelSize: 30; font.weight: Font.DemiBold; color: window.ink
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: messenger.connected ? "Выберите диалог слева, чтобы начать общение." : messenger.loginConfigured ? "Войдите через VK, чтобы открыть свои переписки." : "Вставьте access_token, чтобы открыть свои переписки, или попробуйте демоверсию."
                        color: window.muted; horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap; lineHeight: 1.4
                    }
                    ActionButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: !messenger.connected && messenger.loginConfigured
                        objectName: "vkIdLoginButton"
                        text: messenger.authorizing ? "Ожидаем вход…" : "Войти через VK ID"
                        primary: true; enabled: !messenger.authorizing
                        onClicked: messenger.login()
                    }
                    ActionButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: !messenger.connected && messenger.loginConfigured
                        objectName: "vkApiLoginButton"
                        text: "Войти через VK API"
                        enabled: !messenger.authorizing
                        onClicked: messenger.loginApi()
                    }
                    TokenLogin {
                        Layout.fillWidth: true
                        visible: !messenger.connected
                    }
                    ActionButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: !messenger.connected
                        text: "Настройки"
                        quiet: true
                        onClicked: settings.open()
                    }
                    ActionButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: !messenger.connected
                        text: "Попробовать демоверсию"
                        quiet: true
                        onClicked: { messenger.startDemo(); messagePopup.close()
                profilePanel.close()
                window.draftPeer = messenger.selectedPeer }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    visible: messenger.connected && messenger.selectedPeer.length > 0
                    objectName: "chatColumn"
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 22
                        Layout.rightMargin: 22
                        visible: messenger.restorable.length > 0
                        Label { Layout.fillWidth: true; text: "Удалено сообщений: " + messenger.restorable.length }
                        Button { text: "Восстановить последнее"; enabled: !messenger.operationBusy; onClicked: messenger.restoreLast() }
                        Button { text: "×"; onClicked: messenger.dismissRestore() }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 78
                        color: window.surface
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: design.divider }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 26
                            anchors.rightMargin: 26
                            spacing: 12
                            Avatar {
                                objectName: "conversationHeaderAvatar"
                                TapHandler { onTapped: profilePanel.present(profileButton) }
                                animateImage: true
                                name: messenger.title
                                width: 40; height: 40
                                source: {
                                    messenger.conversations.revision
                                    const row = messenger.conversations.find(messenger.selectedPeer)
                                    return row < 0 ? "" : messenger.conversations.get(row).avatar || ""
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { Layout.fillWidth: true; text: messenger.title; color: window.ink; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText
                                    TapHandler { onTapped: profilePanel.present(profileButton) }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: "Информация: " + messenger.title
                                    Accessible.onPressAction: profilePanel.present(profileButton)
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    BusyIndicator { objectName: "historyRefreshIndicator"; Layout.preferredWidth: 14; Layout.preferredHeight: 14; visible: messenger.loading && history.count > 0; running: visible && window.motionEnabled; Accessible.name: "Обновление переписки" }
                                    Text { Layout.fillWidth: true; text: messenger.peerActivity; color: window.muted; font.pixelSize: 12; elide: Text.ElideRight }
                                }
                            }
                            HeaderAction { id: profileButton; kind: "info"; label: "Информация о переписке"
                                onClicked: profilePanel.present(profileButton) }
                            HeaderAction { kind: "retry"; label: "Обновить"
                                onClicked: messenger.reload() }
                            HeaderAction { kind: "search"; label: "Поиск в переписке"
                                onClicked: actionSheet.show("search", messenger.selectedPeer, [], "") }
                            HeaderAction { kind: "more"; label: "Действия с перепиской"; onClicked: conversationMenu.popup() }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: design.divider }
                    }
                    PinnedMessageBar {
                        Layout.fillWidth: true
                        Layout.leftMargin: 22
                        Layout.rightMargin: 22
                        visible: !!messenger.pinnedMessage.conversation_message_id
                        messageText: messenger.pinnedMessage.text || ""
                        textColor: window.ink
                        secondaryColor: window.muted
                        busy: messenger.actions.busy
                        onOpenMessage: messenger.openMessage(messenger.selectedPeer, String(messenger.pinnedMessage.conversation_message_id))
                        onUnpin: messenger.actions.setPinnedMessage(messenger.selectedPeer, "", false)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        visible: window.selectedIds.length > 0
                        Label { text: "Выбрано: " + window.selectedIds.length; Layout.fillWidth: true }
                        Button { text: "Переслать"; onClicked: actionSheet.show("forward", messenger.selectedPeer, window.selectedIds, "") }
                        Button { text: "Удалить"; onClicked: { actionSheet.canDeleteForAll = messenger.canManageChat || window.selectedIds.every(function(id) { const i = messenger.messages.find(id); return i >= 0 && !!messenger.messages.get(i).outgoing }); actionSheet.show("delete", messenger.selectedPeer, window.selectedIds, "") } }
                        Button { text: "Отмена"; onClicked: window.selectedIds = [] }
                    }
                    Label { Layout.fillWidth: true; Layout.leftMargin: 24; visible: messenger.actions.error.length > 0; text: messenger.actions.error; color: "#c96654"; wrapMode: Text.Wrap }
                    Item {
                        objectName: "historyPane"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    ListView {
                        id: history
                        objectName: "historyList"
                        anchors.fill: parent
                        model: messenger.messages
                        clip: true
                        spacing: 8
                        topMargin: 16
                        bottomMargin: 16
                        contentWidth: width
                        flickableDirection: Flickable.VerticalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        onWidthChanged: { messagePopup.close(); window.restoreGeometryAnchor() }
                        ScrollBar.vertical: ScrollBar { id: historyScrollBar }
                        header: Item {
                            width: history.width
                            height: messenger.hasOlder && !messenger.demo ? 50 : 0
                            Button { anchors.centerIn: parent; visible: parent.height > 0; text: messenger.loading ? "Загрузка…" : "Загрузить предыдущие сообщения"; enabled: !messenger.loading; onClicked: messenger.loadOlder() }
                        }
                        onMovementEnded: window.wasAtEnd = atYEnd
                        onMovementStarted: { window.wasAtEnd = false; tailPosition.stop(); messagePopup.close() }
                        onContentHeightChanged: { if (window.wasAtEnd) tailPosition.restart(); geometrySnapshot.restart() }
                        onContentYChanged: {
                            geometrySnapshot.restart()
                            if (moving || dragging) window.wasAtEnd = atYEnd
                            if (atYEnd && !messenger.hasNewer) window.newMessages = 0
                        }
                        Connections {
                            target: messenger.messages
                            function onModelAboutToBeReset() { window.wasAtEnd = window.draftPeer !== messenger.selectedPeer || history.atYEnd || history.count === 0 }
                            function onModelReset() {
                                if (window.wasAtEnd) Qt.callLater(window.goToEnd)
                            }
                        }
                        delegate: Item {
                            id: entry
                            Component.onCompleted: if (item.local && !messenger.reduceMotion) messageAppear.start()
                            NumberAnimation { id: messageAppear; target: entry; property: "opacity"; from: 0; to: 1; duration: design.motionDuration; easing.type: Easing.OutCubic }
                            objectName: "messageRow"
                            required property var item
                            property real inset: window.selectedIds.length ? 44 : history.width < 520 ? 16 : 24
                            property string bodyText: item.raw && (item.raw.attachments || []).length ? (item.raw.text || "") : item.text || ""
                            property bool stickerOnly: (item.attachments || []).length === 1 && item.attachments[0].type === "sticker" && !bodyText.length && !(item.raw && (item.raw.reply_message || (item.raw.fwd_messages || []).length))
                            readonly property string senderPeer: String(item.senderPeer || (item.raw ? item.raw.from_id || "" : ""))
                            property bool inViewport: y + height > history.contentY && y < history.contentY + history.height
                            FontMetrics { id: reactionMetrics; font.family: window.font.family; font.pixelSize: 12 }
                            property real reactionsWidth: {
                                reactionMetrics.font
                                const reactions = item.raw ? item.raw.reactions || [] : []
                                return reactions.reduce(function(total, reaction) { return total + 43 + reactionMetrics.advanceWidth(String(reaction.count)) }, 0)
                            }
                            width: history.width
                            height: entry.item.gap ? 54 : bubble.y + bubble.height + (window.flatMessages ? 3 : 0)
                            readonly property bool groupStart: item.groupStart !== false
                            readonly property real headingHeight: window.flatMessages ? (dateHeading.visible ? 30 : 0) + (unreadHeading.visible ? 28 : 0) + (groupStart ? 14 : 0) : 0
                            Text { id: dateHeading; objectName: "dateSeparator"; width: parent.width; height: 30; visible: window.flatMessages && !!entry.item.dateLabel; text: entry.item.dateLabel || ""; color: window.muted; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                            Text { id: unreadHeading; width: parent.width; y: dateHeading.visible ? 30 : 0; height: 28; visible: window.flatMessages && !!entry.item.unreadStart; text: "Непрочитанные сообщения"; color: window.accent; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                            Avatar {
                                id: senderAvatar
                                objectName: "messageSenderAvatar"
                                property string peer: entry.senderPeer
                                visible: (window.flatMessages || messenger.isChat) && entry.groupStart && !entry.item.gap
                                x: !window.flatMessages && entry.item.outgoing ? entry.width - entry.inset - width : entry.inset
                                y: entry.headingHeight + 3
                                width: design.avatarSize; height: width
                                name: entry.item.sender || (entry.item.outgoing ? "Вы" : messenger.title)
                                source: entry.item.senderAvatar || ""
                                activeFocusOnTab: entry.senderPeer.length > 0
                                Accessible.role: Accessible.Button
                                Accessible.name: "Профиль: " + name
                                Accessible.onPressAction: window.showPerson(entry.senderPeer, name, source, senderAvatar)
                                TapHandler { onTapped: window.showPerson(entry.senderPeer, senderAvatar.name, senderAvatar.source, senderAvatar) }
                                Keys.onReturnPressed: window.showPerson(entry.senderPeer, name, source, senderAvatar)
                            }
                            CheckBox {
                                visible: window.selectedIds.length > 0 && !entry.item.gap && !entry.item.local
                                checked: window.selectedIds.indexOf(String(entry.item.id)) >= 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: 40
                                onToggled: window.toggleSelection(String(entry.item.id))
                            }
                            FontMetrics { id: bodyMetrics; font: messageText.font }
                            property real bodyWidth: { bodyMetrics.font; return bodyText.split("\n").reduce(function(width, line) { return Math.max(width, bodyMetrics.advanceWidth(line)) }, 0) }
                            TextMetrics {
                                id: footerMeasure
                                font: timestampText.font
                                text: window.deliveryText(entry.item)
                            }
                            Button {
                                anchors.centerIn: parent
                                visible: !!entry.item.gap
                                text: messenger.loading ? "Загрузка…" : "Загрузить пропущенные сообщения"
                                enabled: !messenger.loading
                                onClicked: messenger.loadGap(entry.item.first, entry.item.last)
                            }
                            Rectangle {
                                id: bubble
                                objectName: "messageBubble"
                                visible: !entry.item.gap
                                property real horizontalPadding: entry.stickerOnly ? 4 : 12
                                property real verticalPadding: entry.stickerOnly ? 2 : 9
                                y: entry.headingHeight
                                x: window.flatMessages ? entry.inset + design.avatarSize + 12 : entry.item.outgoing ? entry.width - entry.inset - width - (messenger.isChat ? design.avatarSize + 12 : 0) : entry.inset + (messenger.isChat ? design.avatarSize + 12 : 0)
                                width: window.flatMessages ? Math.max(1, Math.min(design.messageWidth, entry.width - x - entry.inset)) : Math.min(560, entry.width - 2 * entry.inset - (messenger.isChat ? design.avatarSize + 12 : 0), entry.width * 0.80,
                                    Math.max(84, entry.reactionsWidth + horizontalPadding * 2, Math.ceil(footerMeasure.advanceWidth) + 24 + horizontalPadding * 2, Math.ceil(entry.bodyWidth) + 2 + horizontalPadding * 2,
                                        (entry.item.attachments || []).some(function(a) { return a.type === "photo" }) ? Math.min(460, entry.width * 0.72) : (entry.item.attachments || []).length ? 240 : 0,
                                        entry.item.raw && entry.item.raw.reply_message ? 240 : 0,
                                        entry.item.raw && (entry.item.raw.fwd_messages || []).length ? 240 : 0,
                                        entry.item.raw && entry.item.raw.keyboard ? 280 : 0))
                                height: Math.ceil(bubbleLayout.implicitHeight) + verticalPadding * 2
                                radius: 15
                                color: window.flatMessages || entry.stickerOnly ? "transparent" : entry.item.outgoing ? (window.darkTheme ? "#234466" : "#dfedff") : window.surface
                                border.color: window.selectedIds.indexOf(String(entry.item.id)) >= 0 ? window.accent : window.flatMessages || entry.stickerOnly ? "transparent" : (window.darkTheme ? "#364558" : "#e8edf3")
                                HoverHandler { id: bubbleHover }
                                ToolButton {
                                    text: "···"; width: 24; height: 24; padding: 0
                                    x: window.flatMessages ? bubble.width - width : entry.item.outgoing ? -28 : bubble.width + 4
                                    y: 0
                                    opacity: bubbleHover.hovered || hovered || (messagePopup.opened && messagePopup.origin === bubble) || activeFocus ? 1 : 0
                                    visible: opacity > 0
                                    Behavior on opacity { enabled: window.motionEnabled; NumberAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
                                    onClicked: messagePopup.showAt(bubble, entry.item, messageText)
                                    Accessible.name: "Действия с сообщением"
                                }
                                MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton; onClicked: messagePopup.showAt(bubble, entry.item, messageText) }
                                Column {
                                    id: bubbleLayout
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.leftMargin: bubble.horizontalPadding
                                    anchors.rightMargin: bubble.horizontalPadding + (window.flatMessages ? Math.min(120, Math.ceil(footerMeasure.advanceWidth) + 30) : 0)
                                    anchors.topMargin: bubble.verticalPadding
                                    spacing: 5
                                    Text {
                                        width: bubbleLayout.width
                                        visible: window.flatMessages ? entry.groupStart : !entry.item.outgoing && !messenger.demo
                                        text: entry.item.sender || (entry.item.outgoing ? "Вы" : messenger.title)
                                        objectName: "messageSenderName"
                                        property string peer: entry.senderPeer
                                        TapHandler { onTapped: window.showPerson(entry.senderPeer, entry.item.sender, entry.item.senderAvatar, senderAvatar) }
                                        Accessible.role: Accessible.Button
                                        Accessible.name: "Профиль: " + text
                                        Accessible.onPressAction: window.showPerson(entry.senderPeer, entry.item.sender, entry.item.senderAvatar, senderAvatar)
                                        elide: Text.ElideRight
                                        color: window.accent; font.pixelSize: 14; font.weight: Font.DemiBold
                                    }
                                    ReplyPreview {
                                        objectName: "messageReplyPreview"
                                        width: bubbleLayout.width
                                        visible: !!entry.item.raw && !!entry.item.raw.reply_message
                                        message: entry.item.raw ? entry.item.raw.reply_message || {} : ({})
                                        author: entry.item.replyAuthor || "Сообщение"
                                        loadImages: entry.inViewport
                                        clickable: !!(message.conversation_message_id || message.cmid)
                                        onActivated: messenger.openMessage(messenger.selectedPeer, String(message.conversation_message_id || message.cmid))
                                    }
                                    TextEdit {
                                        id: messageText
                                        objectName: "messageText"
                                        Layout.fillWidth: true
                                        Layout.fillHeight: false
                                        width: bubbleLayout.width
                                        height: Math.ceil(contentHeight)
                                        visible: entry.bodyText.length > 0
                                        text: entry.bodyText
                                        textFormat: TextEdit.PlainText
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        color: window.ink
                                        font.pixelSize: design.bodySize
                                        selectionColor: "#adcfff"
                                    }
                                    AttachmentView { width: bubbleLayout.width; loadImages: entry.y + entry.height > history.contentY - history.height && entry.y < history.contentY + 2 * history.height; photoBackground: window.flatMessages ? window.surface : bubble.color; attachments: entry.item.attachments || []; animationsEnabled: entry.inViewport && window.active && !messenger.reduceMotion }
                                    BotKeyboard {
                                        width: bubbleLayout.width
                                        keyboard: entry.item.raw ? entry.item.raw.keyboard || {} : ({})
                                        peer: messenger.selectedPeer
                                        cmid: entry.item.id
                                        author: entry.item.raw ? String(entry.item.raw.from_id) : ""
                                        onLocationRequested: locationDialog.open()
                                    }
                                    Repeater {
                                        model: entry.item.raw ? entry.item.raw.fwd_messages || [] : []
                                        delegate: ColumnLayout {
                                            required property var modelData
                                            width: bubbleLayout.width
                                            Label { Layout.fillWidth: true; text: "↪ " + modelData.text; wrapMode: Text.Wrap; color: "#6485a8"; textFormat: Text.PlainText }
                                            AttachmentView { Layout.fillWidth: true; loadImages: entry.y + entry.height > history.contentY - history.height && entry.y < history.contentY + 2 * history.height; photoBackground: window.flatMessages ? window.surface : bubble.color; attachments: modelData.attachments || []; animationsEnabled: entry.inViewport && window.active && !messenger.reduceMotion }
                                        }
                                    }
                                    Flow {
                                        id: reactionsFlow
                                        objectName: "messageReactions"
                                        width: bubbleLayout.width
                                        visible: reactionRepeater.count > 0
                                        spacing: 4
                                        Repeater {
                                            id: reactionRepeater
                                            model: entry.item.raw ? entry.item.raw.reactions || [] : []
                                            delegate: Button {
                                                id: reactionChip
                                                objectName: "reactionChip"
                                                required property var modelData
                                                readonly property bool selectedReaction: !!entry.item.raw && Number(entry.item.raw.reaction_id || 0) === Number(modelData.reaction_id)
                                                implicitWidth: reactionContents.implicitWidth + 14
                                                implicitHeight: 30
                                                leftPadding: 7; rightPadding: 7; topPadding: 5; bottomPadding: 5
                                                Component.onCompleted: messenger.actions.ensureReactionAssets()
                                                background: Rectangle { radius: 15; color: reactionChip.hovered || reactionChip.selectedReaction ? (window.darkTheme ? "#365775" : "#c9e2fd") : (window.darkTheme ? "#2c4b68" : "#eaf3fe"); border.color: window.darkTheme ? "#517092" : "#c3d9f0"; Behavior on color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } } }
                                                contentItem: Row {
                                                    id: reactionContents
                                                    spacing: 5
                                                    ReactionImage { width: 20; height: 20; anchors.verticalCenter: parent.verticalCenter; remoteSource: { messenger.actions.reactionAssets; return messenger.actions.reactionImage(reactionChip.modelData.reaction_id) } }
                                                    Text { text: String(reactionChip.modelData.count); font.pixelSize: 12; color: window.ink; anchors.verticalCenter: parent.verticalCenter }
                                                }
                                                Accessible.name: "Реакция, " + modelData.count
                                                onClicked: messenger.actions.setReaction(messenger.selectedPeer, String(entry.item.id), selectedReaction ? 0 : modelData.reaction_id)
                                                TapHandler { acceptedButtons: Qt.RightButton; onTapped: actionSheet.show("reacted", messenger.selectedPeer, [String(entry.item.id)], "") }
                                            }
                                        }
                                    }
                                    RowLayout {
                                        id: footerRow
                                        objectName: "messageFooter"
                                        parent: window.flatMessages ? bubble : bubbleLayout
                                        width: window.flatMessages ? Math.min(120, Math.ceil(footerMeasure.advanceWidth) + 26) : bubbleLayout.width
                                        x: window.flatMessages ? bubble.width - width - bubble.horizontalPadding : 0
                                        y: window.flatMessages ? bubble.height - height - bubble.verticalPadding : 0
                                        Layout.fillHeight: false
                                        spacing: 4
                                        Item { Layout.fillWidth: true }
                                        ReadIndicator {
                                            visible: window.flatMessages && !!entry.item.outgoing && (!entry.item.local || entry.item.delivery === "sent")
                                            read: !!entry.item.read
                                            Layout.preferredWidth: 18; Layout.preferredHeight: 14
                                        }
                                        Text {
                                            id: timestampText
                                            objectName: "messageTimestamp"
                                            Layout.minimumWidth: 0
                                            Layout.preferredWidth: Math.min(implicitWidth, Math.max(1, footerRow.width - 26))
                                            horizontalAlignment: Text.AlignRight
                                            elide: Text.ElideRight
                                            text: window.deliveryText(entry.item)
                                            color: entry.item.delivery === "failed" ? "#d17a6b" : "#8295a9"
                                            font.pixelSize: 11
                                            MouseArea { anchors.fill: parent; enabled: !!entry.item.local && entry.item.delivery === "failed"; cursorShape: Qt.PointingHandCursor; onClicked: messenger.retryOutgoing(entry.item.id) }
                                        }
                                        ReadIndicator {
                                            visible: !window.flatMessages && !!entry.item.outgoing && (!entry.item.local || entry.item.delivery === "sent")
                                            read: !!entry.item.read
                                            Layout.preferredWidth: 18
                                            Layout.preferredHeight: 14
                                        }
                                    }

                                }
                            }
                        }
                        Column {
                            parent: history.parent
                            anchors.centerIn: parent
                            width: Math.min(360, parent.width - 48)
                            visible: history.count === 0 && !messenger.loading && !messenger.historyError.length
                            spacing: 12
                            ActionIcon { anchors.horizontalCenter: parent.horizontalCenter; width: 44; height: 44; kind: "chat"; stroke: window.muted; opacity: 0.65 }
                            Text { width: parent.width; text: "Начните разговор"; color: window.ink; font.pixelSize: 18; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter }
                            Text { width: parent.width; text: "Напишите сообщение или прикрепите файл"; color: window.muted; font.pixelSize: 13; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
                        }
                        BusyIndicator { objectName: "emptyHistoryLoading"; parent: history.parent; anchors.centerIn: parent; visible: messenger.loading && history.count === 0; running: visible }
                        Column {
                            parent: history.parent
                            anchors.centerIn: parent
                            width: Math.min(420, parent.width - 70)
                            visible: messenger.historyError.length > 0 && !messenger.loading
                            spacing: 8
                            Text { width: parent.width; text: messenger.historyError; color: design.errorText; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                            Button { text: "Повторить загрузку"; onClicked: messenger.jumpToEnd() }
                        }
                    }
                    ScrollDateBadge {
                        id: scrollDate
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        view: history
                        sourceModel: messenger.messages
                        scrollBar: historyScrollBar
                        theme: design
                        peer: messenger.selectedPeer
                        reducedMotion: messenger.reduceMotion
                    }
                    ToolButton {
                        id: jumpToLatest
                        objectName: "jumpToEndButton"
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 20
                        width: 48; height: 48; padding: 12
                        text: "К последним сообщениям"
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: text + (window.newMessages > 0 ? ". Новых сообщений: " + window.newMessages : "")
                        ToolTip.visible: hovered || activeFocus
                        ToolTip.delay: hovered ? 500 : 0
                        ToolTip.text: Accessible.name
                        background: Rectangle {
                            radius: 24
                            color: jumpToLatest.down ? design.pressed : jumpToLatest.hovered ? design.hover : window.surface
                            border.color: jumpToLatest.visualFocus ? window.accent : design.divider
                            border.width: jumpToLatest.visualFocus ? 2 : 1
                        }
                        contentItem: ActionIcon { kind: "chevronDown"; stroke: window.accent }
                        Rectangle {
                            objectName: "historyNewMessageBadge"
                            anchors.right: parent.right; anchors.top: parent.top
                            anchors.rightMargin: -4; anchors.topMargin: -6
                            width: Math.max(24, newMessageCount.implicitWidth + 12); height: 24; radius: 12
                            visible: window.newMessages > 0
                            color: window.accent; border.width: 2; border.color: window.surface
                            Text {
                                id: newMessageCount
                                objectName: "historyNewMessageCount"
                                anchors.centerIn: parent
                                text: window.newMessages > 99 ? "99+" : String(window.newMessages)
                                color: window.darkTheme ? "#172536" : "white"; font.pixelSize: 11; font.weight: Font.DemiBold
                            }
                        }
                        visible: history.count > 0 && (!history.atYEnd || messenger.hasNewer)
                        onClicked: messenger.jumpToEnd()
                    }
                    }
                    ReplyPreview {
                        objectName: "composerReplyPreview"
                        Layout.fillWidth: true
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 6
                        visible: !window.editing && Object.keys(messenger.reply).length > 0
                        message: messenger.reply.raw || messenger.reply
                        author: messenger.reply.sender || "Сообщение"
                        dismissible: true; clickable: false
                        onDismissed: { messenger.clearReply(); composer.forceActiveFocus() }
                    }
                    RowLayout {
                        objectName: "editMessageBanner"
                        Layout.fillWidth: true
                        Layout.leftMargin: 24; Layout.rightMargin: 24
                        visible: window.editing
                        ActionIcon { kind: "edit"; stroke: window.accent }
                        Label { Layout.fillWidth: true; text: "Редактирование сообщения" + (window.editingHasAttachments ? " · вложения сохраняются" : ""); color: window.accent; elide: Text.ElideRight }
                        ToolButton { text: "×"; Accessible.name: "Отменить редактирование"; onClicked: window.cancelEditing() }
                    }
                    BotKeyboard {
                        objectName: "currentBotKeyboard"
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        Layout.leftMargin: 22
                        Layout.rightMargin: 22
                        keyboard: messenger.currentKeyboard
                        visible: (messenger.currentKeyboard.buttons || []).length > 0
                        peer: messenger.selectedPeer
                        onLocationRequested: locationDialog.open()
                    }
                    Flickable {
                        id: draftFiles
                        objectName: "draftAttachmentStrip"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 6
                        visible: !window.editing && window.draftAttachments.length > 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.HorizontalFlick
                        contentWidth: attachmentChips.width
                        contentHeight: height
                        function revealChip(chip) {
                            const left = chip.x, right = left + chip.width
                            if (left < contentX) contentX = left
                            else if (right > contentX + width) contentX = right - width
                            contentX = Math.max(0, Math.min(contentX, Math.max(0, contentWidth - width)))
                        }
                        function revealFocusedChip() {
                            for (let i = 0; i < attachmentRepeater.count; ++i) {
                                const chip = attachmentRepeater.itemAt(i)
                                if (chip && chip.removeFocused) { revealChip(chip); return }
                            }
                        }
                        onContentWidthChanged: Qt.callLater(revealFocusedChip)
                        onWidthChanged: Qt.callLater(revealFocusedChip)
                        Accessible.name: "Вложения: " + window.draftAttachments.length
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                        Row {
                            id: attachmentChips
                            spacing: 8
                            Repeater {
                                id: attachmentRepeater
                                model: window.draftAttachments
                                delegate: Rectangle {
                                    id: chip
                                    objectName: "draftAttachmentChip"
                                    required property var modelData
                                    readonly property bool removeFocused: removeFileButton.activeFocus
                                    width: Math.min(250, Math.max(170, attachmentName.implicitWidth + 82))
                                    height: 50; radius: 12
                                    color: window.secondarySurface; border.color: design.divider
                                    Accessible.name: modelData.name || "Вложение"
                                    ActionIcon {
                                        x: 12; anchors.verticalCenter: parent.verticalCenter; width: 22; height: 22
                                        kind: String(chip.modelData.attachment).indexOf("photo") === 0 ? "photo" : String(chip.modelData.attachment).indexOf("video") === 0 ? "video" : "document"
                                        stroke: window.accent
                                    }
                                    Column {
                                        x: 44; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 82; spacing: 3
                                        Text { id: attachmentName; width: parent.width; text: chip.modelData.name || "Вложение"; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: window.ink; font.pixelSize: 13 }
                                        Text { width: parent.width; text: "Готово к отправке"; color: window.muted; font.pixelSize: 11; elide: Text.ElideRight }
                                    }
                                    HeaderAction {
                                        id: removeFileButton
                                        objectName: "removeDraftAttachment"
                                        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                                        width: 30; height: 30; kind: "close"; label: "Убрать вложение: " + (chip.modelData.name || "Вложение")
                                        onActiveFocusChanged: if (activeFocus) Qt.callLater(draftFiles.revealFocusedChip)
                                        onClicked: { window.focusComposer(); messenger.transfers.remove(chip.modelData.sourceIndex) }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 22
                        Layout.rightMargin: 22
                        visible: messenger.transfers.busy || messenger.transfers.error.length > 0 || messenger.transfers.downloading
                        Label { objectName: "transferStatus"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: messenger.transfers.error || (messenger.transfers.downloading ? "Скачивание файла…" : (messenger.transfers.status.length ? messenger.transfers.status + " · " : "") + messenger.transfers.filename); textFormat: Text.PlainText; wrapMode: messenger.transfers.error.length ? Text.Wrap : Text.NoWrap; elide: messenger.transfers.error.length ? Text.ElideNone : Text.ElideRight; font.pixelSize: 12; color: messenger.transfers.error.length ? design.errorText : window.muted; ToolTip.visible: transferHover.hovered && text.length > 0; ToolTip.text: text; HoverHandler { id: transferHover } }
                        ProgressBar { value: messenger.transfers.progress; indeterminate: value <= 0 || value >= 1; visible: messenger.transfers.busy; Layout.preferredWidth: 100 }
                        Button { text: "Повторить"; visible: messenger.transfers.error.length > 0 && messenger.transfers.canRetry; onClicked: messenger.transfers.retry() }
                        Button { text: "Отмена"; onClicked: { messenger.transfers.cancel(); messenger.transfers.cancelDownload() } }
                    }
                    Rectangle {
                        objectName: "composerPanel"
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.bottomMargin: 8
                        Layout.topMargin: 0
                        implicitHeight: composerRow.implicitHeight + 22
                        radius: 16
                        color: window.secondarySurface
                        border.color: composer.activeFocus ? design.focusBorder : design.divider
                        Behavior on border.color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } }
                        RowLayout {
                            id: composerRow
                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 8
                            HeaderAction { kind: "add"; label: "Прикрепить вложение"; enabled: !messenger.demo && !window.editing; onClicked: uploadMenu.popup() }
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(130, Math.max(40, composer.implicitHeight))
                                TextArea {
                                    id: composer
                                    objectName: "composer"
                                    Accessible.name: window.editing ? "Редактирование сообщения" : "Сообщение"
                                    placeholderText: messenger.canWrite ? "Написать сообщение…" : "Отправка сообщений в этот диалог недоступна"
                                    readOnly: !messenger.canWrite
                                    placeholderTextColor: "#9aa6b5"
                                    color: window.ink
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    background: Item {}
                                    onTextChanged: {
                                        if (!window.restoringDraft && !window.editing) messenger.setDraftText(messenger.selectedPeer, text)
                                        if (activeFocus && text.length && !window.editing && !window.restoringDraft && !inputMethodComposing) messenger.typing()
                                    }
                                    function submit() { submitWithMode(false) }
                                    function submitWithMode(silent) {
                                        if (inputMethodComposing) return
                                        if (window.editing) {
                                            if (!messenger.actions.busy) messenger.actions.editMessage(window.editingPeer, window.editingMessage, text)
                                            return
                                        }
                                        if (messenger.sending) return
                                        window.submittedDrafts[messenger.selectedPeer] = text
                                        messenger.sendWithOptions(text, silent === true)
                                    }
                                    Keys.onReturnPressed: function(event) {
                                        if (inputMethodComposing || event.modifiers & Qt.ShiftModifier) event.accepted = false
                                        else { submit(); event.accepted = true }
                                    }
                                    Keys.onEnterPressed: function(event) {
                                        if (inputMethodComposing || event.modifiers & Qt.ShiftModifier) event.accepted = false
                                        else { submit(); event.accepted = true }
                                    }
                                }
                            }
                            HeaderAction {
                                id: sendButton
                                objectName: "sendMessageButton"
                                property bool busy: window.editing ? messenger.actions.busy : messenger.sending
                                implicitWidth: 38; implicitHeight: 38
                                kind: window.editing ? "select" : "send"
                                label: window.editing ? (busy ? "Сохранение…" : "Сохранить изменения") : busy ? "Отправка…" : "Отправить сообщение"
                                background: Rectangle {
                                    radius: 19
                                    color: sendButton.enabled ? window.accent : design.divider
                                    border.color: sendButton.visualFocus ? window.ink : "transparent"
                                    Behavior on color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration } }
                                }
                                contentItem: Item {
                                    ActionIcon { anchors.centerIn: parent; width: 22; height: 22; kind: sendButton.kind; stroke: sendButton.enabled ? "white" : window.muted; visible: !sendButton.busy }
                                    BusyIndicator { anchors.fill: parent; running: sendButton.busy; visible: running }
                                }
                                enabled: (window.editing ? (composer.text.trim().length > 0 || window.editingHasAttachments) : (composer.text.trim().length > 0 || window.draftAttachments.length > 0)) && !composer.inputMethodComposing && !messenger.sending && (!window.editing || !messenger.actions.busy) && !messenger.authorizing && messenger.canWrite
                                onClicked: composer.submit()
                            }
                            HeaderAction {
                                kind: "chevronDown"; label: "Способ отправки"; implicitWidth: 24; implicitHeight: 32
                                enabled: sendButton.enabled
                                visible: !window.editing
                                onClicked: sendOptions.popup()
                                AppMenu { id: sendOptions; AppMenuItem { kind: "muted"; text: "Отправить без звука"; onTriggered: composer.submitWithMode(true) } }
                            }
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 8
                        text: "Enter — отправить · Shift + Enter — новая строка"
                        opacity: composer.activeFocus ? 0.8 : 0
                        Behavior on opacity { enabled: window.motionEnabled; NumberAnimation { duration: design.hoverDuration } }
                        color: "#9aa6b4"
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    ConversationProfile {
        id: profilePanel
        parent: Overlay.overlay
        theme: design
        client: messenger
        controller: messenger.profileActions
        onWriteRequested: function(peer, title) { profilePanel.close(); window.selectChat(peer, title) }
        onActionRequested: function(kind) {
            if (kind === "settings" || kind === "notifications") conversationMenu.popup()
            else actionSheet.show(kind, profilePanel.peer, [], "")
        }
    }
    FolderSheet { id: folderSheet; parent: Overlay.overlay; reducedMotion: messenger.reduceMotion }

    Loader {
        id: inspector
        active: messenger.apiInspectorEnabled
        sourceComponent: Component { MethodBrowser { objectName: "methodBrowser"; parent: Overlay.overlay } }
    }
    Loader { id: mediaViewerLoader; active: false; sourceComponent: Component { MediaViewer {} } }
    function presentMedia(source, type, title) {
        mediaViewerLoader.active = true
        mediaViewerLoader.item.present(source, type, title)
    }
    Connections {
        target: messenger.actions
        function onMediaRequested(source, type, title) { window.presentMedia(source, type, title) }
        function onPhotoGalleryRequested(photos, selectedIndex) {
            mediaViewerLoader.active = true
            mediaViewerLoader.item.presentGallery(photos, selectedIndex)
        }
        function onMessageEdited(peer, cmid) {
            if (window.editingPeer === peer && window.editingMessage === cmid) window.cancelEditing()
        }
    }
    ActionSheet { id: actionSheet; parent: Overlay.overlay; reducedMotion: messenger.reduceMotion }
    Shortcut { sequence: "Escape"; enabled: window.editing && !messagePopup.opened && !profilePanel.opened && (!mediaViewerLoader.item || !mediaViewerLoader.item.visible); onActivated: window.cancelEditing() }
    Shortcut { sequence: "Escape"; enabled: !window.editing && window.selectedIds.length > 0 && (!mediaViewerLoader.item || !mediaViewerLoader.item.visible); onActivated: window.selectedIds = [] }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 20
        width: Math.min(500, parent.width - 60)
        height: toastRow.implicitHeight + 20
        visible: messenger.toast.length > 0
        color: "#e1eeff"
        border.color: "#b5d4ff"
        radius: 12
        z: 10
        RowLayout {
            id: toastRow
            anchors.fill: parent
            anchors.margins: 10
            Label { Layout.fillWidth: true; text: messenger.toast; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            Button { text: "Открыть"; visible: messenger.toastUrl.toString().length > 0; onClicked: Qt.openUrlExternally(messenger.toastUrl) }
        }
    }
    MessageActionPopup {
        id: messagePopup
        parent: Overlay.overlay
        theme: design
        actions: messenger.actions
        canManage: messenger.canManageChat
        reduceMotion: messenger.reduceMotion
        onCommand: function(kind) {
            const item = messagePopup.message, id = String(item.id)
            if (kind.indexOf("reaction:") === 0) messenger.actions.setReaction(messenger.selectedPeer, id, Number(kind.split(":")[1]))
            else if (kind === "reply") messenger.setReply(id)
            else if (kind === "copy") messenger.actions.copyText(item.raw ? item.raw.text || "" : item.text || "")
            else if (kind === "important") messenger.actions.setImportant(messenger.selectedPeer, id, !item.important)
            else if (kind === "select") window.toggleSelection(id)
            else if (kind === "pin") messenger.actions.setPinnedMessage(messenger.selectedPeer, id, true)
            else if (kind === "link") messenger.actions.copyMessageLink(messenger.selectedPeer, id)
            else if (kind === "reacted") actionSheet.show("reacted", messenger.selectedPeer, [id], "")
            else if (kind === "retry") messenger.retryOutgoing(id)
            else if (kind === "cancel") messenger.cancelOutgoing(id)
            else window.messageActions(kind, item)
        }
    }
    function rememberGeometryAnchor() {
        if (!history || window.restoringGeometry) return
        const rows = visibleHistoryRows()
        if (rows.length) {
            window.geometryAnchor = String(rows[0].item.id)
            window.geometryOffset = history.contentY - rows[0].y
        }
    }
    Timer { id: geometrySnapshot; interval: 40; onTriggered: window.rememberGeometryAnchor() }
    function restoreGeometryAnchor() {
        if (!history || !messenger.connected || window.restoringGeometry) return
        // Width-change signals can arrive after delegates have already reflowed.
        // Use the last settled viewport, not partially updated row geometry.
        const atEnd = window.wasAtEnd
        const anchor = window.geometryAnchor, offset = window.geometryOffset
        window.restoringGeometry = true
        Qt.callLater(function() {
            if (atEnd) window.goToEnd()
            else if (anchor.length) {
                history.forceLayout()
                const i = messenger.messages.find(anchor)
                if (i >= 0) { history.positionViewAtIndex(i, ListView.Beginning); history.contentY += offset }
            }
            window.restoringGeometry = false
            geometrySnapshot.restart()
        })
    }
    onFlatMessagesChanged: restoreGeometryAnchor()
    onWidthChanged: { messagePopup.close(); restoreGeometryAnchor() }
    onHeightChanged: { messagePopup.close(); restoreGeometryAnchor() }
    Dialog {
        id: reactionPicker
        objectName: "reactionPicker"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, window.width - 60)
        height: 300
        modal: true
        title: "Выберите реакцию"
        padding: 20
        background: Rectangle { radius: 18; color: window.surface; border.color: window.darkTheme ? "#364558" : "#e2e8f0" }
        standardButtons: Dialog.Close
        property string peer: ""
        property string cmid: ""
        property var assets: []
        onOpened: { messenger.actions.reset(); messenger.actions.loadReactions() }
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: messenger.actions.error; visible: text.length > 0; wrapMode: Text.Wrap; color: "#b3432a" }
            BusyIndicator { running: messenger.actions.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
            GridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                objectName: "reactionGrid"
                cellWidth: 70
                cellHeight: 70
                model: reactionPicker.visible ? messenger.actions.results : []
                delegate: Button {
                    required property var modelData
                    width: 62; height: 62
                    padding: 12
                    contentItem: ReactionImage { remoteSource: modelData.links.static }
                    background: Rectangle { radius: 16; color: parent.hovered ? (window.darkTheme ? "#365775" : "#e2efff") : "transparent"; Behavior on color { enabled: window.motionEnabled; ColorAnimation { duration: design.hoverDuration; easing.type: Easing.OutCubic } } }
                    enabled: !messenger.actions.busy
                    onClicked: { messenger.actions.setReaction(reactionPicker.peer, reactionPicker.cmid, modelData.reaction_id); reactionPicker.close() }
                }
            }
        }

    }
    AppMenu {
        id: navigationMenu
        objectName: "navigationMenu"
        AppMenuItem { kind: "add"; text: "Создать беседу"; onTriggered: actionSheet.show("create", "", [], "") }
        AppMenuItem { kind: "person"; text: "Новый диалог"; onTriggered: actionSheet.show("peers", "", [], "") }
        AppMenuItem { kind: "important"; text: "Важные сообщения"; onTriggered: actionSheet.show("important", "", [], "") }
        AppMenuItem { kind: "folder"; text: "Управление папками"; onTriggered: folderSheet.show() }
        AppMenuItem { kind: "settings"; text: "Настройки"; onTriggered: settings.open() }
    }
    AppMenu {
        id: conversationMenu
        AppMenuItem { kind: "people"; text: "Участники"; visible: messenger.isChat; onTriggered: actionSheet.show("members", messenger.selectedPeer, [], "") }
        AppMenuItem { kind: "add"; text: "Добавить участников"; visible: messenger.isChat; onTriggered: actionSheet.show("add", messenger.selectedPeer, [], "") }
        AppMenuItem { kind: "edit"; text: "Переименовать беседу"; visible: messenger.isChat; onTriggered: actionSheet.show("rename", messenger.selectedPeer, [], messenger.title) }
        AppMenuItem { kind: "photo"; text: "Фотография беседы"; visible: messenger.isChat; onTriggered: uploadFiles.choose("chat_photo") }
        AppMenuItem { kind: "delete"; text: "Удалить фотографию"; visible: messenger.isChat; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "removePhoto") }
        AppMenuItem { kind: "link"; text: "Ссылка-приглашение"; visible: messenger.isChat; onTriggered: actionSheet.show("invite", messenger.selectedPeer, [], "") }
        AppMenuItem { kind: "link"; text: "Вступить по приглашению"; onTriggered: actionSheet.show("join", "", [], "") }
        AppMenuItem { kind: "leave"; destructive: true; text: "Выйти из беседы…"; visible: messenger.isChat; onTriggered: confirmConversation.show("leave", "Выйти из беседы?") }
        AppMenuSeparator {}
        AppMenuItem { kind: "select"; text: "Отметить прочитанным"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "read") }
        AppMenuItem { kind: "select"; text: "Отметить непрочитанным"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "unread") }
        AppMenuItem { kind: "important"; text: "Добавить в важные"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "important") }
        AppMenuItem { kind: "important"; text: "Убрать из важных"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "unimportant") }
        AppMenuItem { kind: "pin"; text: "Закрепить диалог"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "pin") }
        AppMenuItem { kind: "pin"; text: "Открепить диалог"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "unpin") }
        AppMenuItem { kind: "archive"; text: "Архивировать"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "archive") }
        AppMenuItem { kind: "archive"; text: "Вернуть из архива"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "unarchive") }
        AppMenuSeparator {}
        AppMenuItem { kind: "muted"; text: "Выключить уведомления"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "mute") }
        AppMenuItem { kind: "notifications"; text: "Включить уведомления"; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "unmute") }
        AppMenuItem { kind: "muted"; text: "Выключить упоминания"; visible: messenger.isChat; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "muteMentions") }
        AppMenuItem { kind: "notifications"; text: "Включить упоминания"; visible: messenger.isChat; onTriggered: messenger.actions.conversationAction(messenger.selectedPeer, "unmuteMentions") }
        AppMenuItem { kind: "photo"; text: "Вложения"; onTriggered: actionSheet.show("attachments", messenger.selectedPeer, [], "") }
        AppMenuItem { kind: "pin"; text: "Открепить сообщение"; onTriggered: messenger.actions.setPinnedMessage(messenger.selectedPeer, "", false) }
        AppMenuItem { kind: "delete"; destructive: true; text: "Удалить переписку…"; onTriggered: confirmConversation.show("delete", "Удалить переписку? Это действие нельзя отменить.") }
        AppMenuItem { kind: "settings"; text: "Инструменты разработчика"; visible: messenger.apiInspectorEnabled; onTriggered: inspector.item.openMethod("messages.getConversationsById", {}) }
    }
    Dialog {
        id: confirmConversation
        parent: Overlay.overlay; anchors.centerIn: parent
        modal: true; standardButtons: Dialog.Cancel | Dialog.Ok
        property string action: ""
        property string peer: ""
        function show(kind, question) { action = kind; peer = messenger.selectedPeer; title = question; open() }
        onAccepted: messenger.actions.conversationAction(peer, action)
    }
    AppMenu {
        id: uploadMenu
        AppMenuItem { kind: "photo"; text: "Фотографии"; onTriggered: uploadFiles.choose("photo") }
        AppMenuItem { kind: "document"; text: "Документы / файлы"; onTriggered: uploadFiles.choose("doc") }
        AppMenuItem { kind: "video"; text: "Видео"; onTriggered: uploadFiles.choose("video") }
        AppMenuItem { kind: "microphone"; text: "Записать голосовое сообщение"; onTriggered: { voiceDialog.peer = messenger.selectedPeer; voiceDialog.open(); messenger.recorder.start() } }
        AppMenuItem { kind: "microphone"; text: "Голосовое сообщение из файла"; onTriggered: uploadFiles.choose("audio_message") }
        AppMenuItem { kind: "smile"; text: "Стикеры"; onTriggered: actionSheet.show("stickers", messenger.selectedPeer, [], "") }
    }
    FileDialog {
        id: uploadFiles
        property string kind: "doc"
        property string peer: ""
        fileMode: FileDialog.OpenFiles
        title: "Добавить вложения"
        function choose(type) { kind = type; peer = messenger.selectedPeer; open() }
        onAccepted: { for (const file of selectedFiles) messenger.transfers.upload(file, peer, kind) }
    }
    Dialog {
        id: eventDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(700, window.width - 60)
        height: Math.min(600, window.height - 60)
        modal: true
        title: "События Long Poll"
        standardButtons: Dialog.Close
        contentItem: ListView {
            clip: true
            model: messenger.eventHistory
            delegate: ColumnLayout {
                required property var modelData
                width: ListView.view.width
                Label { text: modelData.time + " · событие " + modelData.type }
                TextEdit { Layout.fillWidth: true; text: JSON.stringify(modelData.payload, null, 2); textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere }
                Button {
                    text: "Ответить на callback"
                    visible: modelData.type === 119 && !!modelData.payload[1] && !!modelData.payload[1].user_id
                    onClicked: {
                        const data = modelData.payload[1] || {}
                        inspector.item.openMethod("messages.sendMessageEventAnswer", {event_id: data.event_id, peer_id: String(data.peer_id || ""), user_id: String(data.user_id || data.owner_id || "")})
                    }
                }
            }
        }
    }

    Dialog {
        id: loginReturn
        objectName: "vkIdReturnDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(500, window.width - 40)
        title: messenger.browserLoginUsesToken ? "Завершение входа VK API" : "Завершение входа VK ID"
        modal: true
        padding: 24
        standardButtons: Dialog.Cancel
        onRejected: messenger.logout()
        onClosed: { loginReturnUrl.clear(); manualLoginReturn.checked = false }
        background: Rectangle { color: window.surface; radius: 16; border.color: design.divider }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: messenger.browserLoginPending
                    ? (messenger.browserLoginUsesToken
                        ? "Подтвердите вход в браузере и вставьте полный адрес страницы oauth.vk.com/blank.html, включая #access_token и остальные параметры."
                        : "Подтвердите вход в браузере. На странице возврата разрешите открыть VK или нажмите «Вернуться в приложение».")
                    : "Получен ответ браузера. Проверяем вход…"
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            CheckBox {
                id: manualLoginReturn
                objectName: "vkIdManualReturn"
                text: "Вставить ссылку вручную"
                visible: messenger.browserLoginPending && !messenger.browserLoginUsesToken
            }
            Label {
                Layout.fillWidth: true
                visible: messenger.browserLoginPending && (messenger.browserLoginUsesToken || manualLoginReturn.checked)
                text: messenger.browserLoginUsesToken
                    ? "Если VK показывает Security Error, этот способ входа отклонён: ссылку с ошибкой вставлять не нужно. Закройте это окно."
                    : "Если браузер не вернулся в приложение, вставьте полный адрес страницы " + messenger.redirectUri + "."
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Input {
                id: loginReturnUrl
                objectName: "vkIdReturnInput"
                Layout.fillWidth: true
                visible: messenger.browserLoginPending && (messenger.browserLoginUsesToken || manualLoginReturn.checked)
                placeholderText: "Адрес страницы после входа"
                echoMode: TextInput.Password
                enabled: messenger.browserLoginPending
                onAccepted: if (loginReturnUrl.text.trim().length && messenger.browserLoginPending) { messenger.completeLogin(loginReturnUrl.text); loginReturnUrl.clear() }
            }
            ActionButton {
                objectName: "vkIdReturnSubmit"
                Layout.fillWidth: true
                visible: messenger.browserLoginPending && (messenger.browserLoginUsesToken || manualLoginReturn.checked)
                text: messenger.browserLoginPending ? "Завершить вход" : "Проверяем вход…"
                primary: true
                enabled: messenger.browserLoginPending && loginReturnUrl.text.trim().length > 0
                onClicked: { messenger.completeLogin(loginReturnUrl.text); loginReturnUrl.clear() }
            }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: messenger.authorizing && !messenger.browserLoginPending; running: visible }
            Label {
                Layout.fillWidth: true
                visible: messenger.error.length > 0
                text: messenger.error
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: "#b3432a"
            }
        }
    }

    Dialog {
        id: settings
        anchors.centerIn: parent
        width: Math.min(540, window.width - 60)
        title: "Настройки"
        modal: true
        padding: 24
        standardButtons: Dialog.Close
        onOpened: {
            appId.text = messenger.clientId
            redirect.text = messenger.redirectUri
            scopes.text = messenger.scope
        }
        background: Rectangle { color: window.surface; radius: 16; border.color: "#e2e8f0" }
        contentItem: ColumnLayout {
            spacing: 12
            CheckBox { text: "Тёмная тема"; checked: window.darkTheme; onToggled: messenger.darkTheme = checked }
            CheckBox { text: "Сообщения без пузырей"; checked: window.flatMessages; onToggled: messenger.flatMessages = checked }
            CheckBox { text: "Уменьшить анимацию"; checked: messenger.reduceMotion; onToggled: messenger.reduceMotion = checked }
            ActionButton { text: "Данные и память"; Layout.fillWidth: true; enabled: messenger.connected && !messenger.demo; onClicked: { messenger.refreshCacheStats(); cacheDialog.open() } }
            ActionButton {
                objectName: "settingsVkApiLoginButton"
                text: "Войти через VK API"
                Layout.fillWidth: true
                visible: messenger.loginConfigured
                enabled: !messenger.authorizing
                onClicked: { settings.close(); messenger.loginApi() }
            }
            TokenLogin {
                Layout.fillWidth: true
                fieldName: "settingsAccessTokenInput"
                visible: !showVkId.checked
            }
            CheckBox {
                id: showVkId
                text: "Настройки VK ID для разработчика"
                visible: messenger.apiInspectorEnabled
            }
            Text {
                visible: showVkId.checked
                Layout.fillWidth: true
                text: "Укажите параметры приложения из кабинета VK ID. Вход откроется в системном браузере."
                wrapMode: Text.Wrap; color: window.muted; lineHeight: 1.3
            }
            Text { visible: showVkId.checked; text: "ID приложения"; color: window.ink; font.weight: Font.DemiBold }
            Input { id: appId; visible: showVkId.checked; Layout.fillWidth: true; placeholderText: "Например, 12345678" }
            Text { visible: showVkId.checked; text: "Redirect URI"; color: window.ink; font.weight: Font.DemiBold }
            Input { id: redirect; visible: showVkId.checked; Layout.fillWidth: true; placeholderText: "Зарегистрированный адрес возврата" }
            Text { visible: showVkId.checked; text: "Разрешения (scope)"; color: window.ink; font.weight: Font.DemiBold }
            Input { id: scopes; visible: showVkId.checked; Layout.fillWidth: true; placeholderText: "Только доступные вашему приложению" }
            Text {
                visible: showVkId.checked
                Layout.fillWidth: true
                text: "Доступ к личным сообщениям выдаётся отдельно. Вход через VK ID сам по себе его не предоставляет."
                wrapMode: Text.Wrap; color: window.muted; font.pixelSize: 12
            }
            ActionButton {
                visible: showVkId.checked
                text: messenger.authorizing ? "Начать вход заново" : "Продолжить в браузере"
                primary: true
                enabled: appId.text.trim().length > 0 && redirect.text.trim().length > 0
                Layout.fillWidth: true
                onClicked: {
                    messenger.clientId = appId.text
                    messenger.redirectUri = redirect.text
                    messenger.scope = scopes.text
                    callback.clear()
                    messenger.login()
                }
            }
            Text {
                visible: showVkId.checked
                Layout.fillWidth: true
                text: messenger.authorizing ? "Ожидаем ответ браузера. Если используется HTTPS redirect, вставьте полный адрес страницы после входа:"
                                           : "Для локального callback VK ID должен разрешать указанный loopback URI. Для HTTPS доступен ручной возврат ссылки."
                wrapMode: Text.Wrap; color: window.muted; font.pixelSize: 12
            }
            Input { id: callback; Layout.fillWidth: true; placeholderText: "https://…?code=…&device_id=…&state=…"; visible: showVkId.checked && messenger.authorizing }
            ActionButton {
                text: "Завершить вход"
                visible: showVkId.checked && messenger.authorizing
                enabled: callback.text.trim().length > 0
                Layout.fillWidth: true
                onClicked: { messenger.completeLogin(callback.text); callback.clear() }
            }
            Text {
                visible: messenger.error.length > 0
                Layout.fillWidth: true
                text: messenger.error
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: "#b44c30"
                font.pixelSize: 12
            }
            ActionButton {
                text: "Выйти из аккаунта на этом устройстве"
                visible: messenger.connected || messenger.authorizing
                Layout.fillWidth: true
                onClicked: { if (messenger.outgoingCount > 0) logoutDialog.open(); else { messenger.logout(); settings.close() } }
            }
        }

    }
    Dialog {
        id: cacheDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        title: "Данные и память"; modal: true; width: 460; standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: "Сохранено сообщений: " + (messenger.cacheStats.messagesCount || 0) }
            Label { text: "История: " + Math.ceil(((messenger.cacheStats.messagesBytes || 0) + (messenger.cacheStats.entitiesBytes || 0)) / 1048576) + " МиБ" }
            Label { text: "В очереди отправки: " + (messenger.cacheStats.outgoingCount || 0) }
            Label { Layout.fillWidth: true; text: "Очистка истории сохранит черновики и неотправленные сообщения. История будет снова загружена из VK при подключении."; wrapMode: Text.Wrap; color: window.muted }
            Label { text: "Медиа: " + Math.ceil(messenger.media.bytes / 1048576) + " МиБ" }
            ComboBox { model: ["256 МиБ", "1 ГиБ", "5 ГиБ", "10 ГиБ"]; currentIndex: messenger.media.limit <= 268435456 ? 0 : messenger.media.limit <= 1073741824 ? 1 : messenger.media.limit <= 5368709120 ? 2 : 3; onActivated: messenger.media.limit = [268435456, 1073741824, 5368709120, 10737418240][currentIndex] }
            Button { text: "Очистить медиакэш"; onClicked: messenger.media.clear() }
            Button { text: "Очистить сохранённую историю"; onClicked: messenger.clearHistoryCache() }
        }
    }
    Dialog {
        id: locationDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        title: "Местоположение"; modal: true; standardButtons: Dialog.Close
        Label { text: "Выбор места на карте пока недоступен. Можно отправить ссылку на место из приложения карт."; width: 350; wrapMode: Text.Wrap }
    }

    Dialog {
        id: voiceDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        title: "Голосовое сообщение"; modal: true; width: 440
        standardButtons: Dialog.Close
        property string peer: ""
        onClosed: messenger.recorder.discard()
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: messenger.recorder.requestingPermission ? "Ожидаем разрешение микрофона…" : messenger.recorder.finalizing ? "Обрабатываем звук…" : messenger.recorder.recording ? "● Запись" : messenger.recorder.ready ? "Запись готова" : "Микрофон"; color: messenger.recorder.recording ? "#df6256" : window.ink }
            Label { text: Math.floor(messenger.recorder.duration / 60000) + ":" + String(Math.floor(messenger.recorder.duration / 1000) % 60).padStart(2, "0"); font.pixelSize: 32 }
            Label { Layout.fillWidth: true; text: messenger.recorder.error || messenger.transfers.error; visible: text.length > 0; wrapMode: Text.Wrap; color: "#c96654" }
            Button { text: "Остановить"; visible: messenger.recorder.recording; onClicked: messenger.recorder.stop() }
            Button { text: "Прослушать"; visible: messenger.recorder.ready; onClicked: window.presentMedia(messenger.recorder.file, "audio_message", "Голосовое сообщение") }
            Button { text: "Добавить к сообщению"; visible: messenger.recorder.ready; onClicked: { if (messenger.attachRecording(voiceDialog.peer)) voiceDialog.close() } }
            Button { text: "Записать заново"; visible: !messenger.recorder.recording && !messenger.recorder.requestingPermission && !messenger.recorder.finalizing; onClicked: messenger.recorder.start() }
        }
    }

    Dialog {
        id: logoutDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        title: "Выйти из аккаунта?"; modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { width: 380; wrapMode: Text.Wrap; text: "Неотправленные сообщения и черновики сохранятся на этом устройстве. Очередь продолжит отправку после следующего входа в этот аккаунт." }
        onAccepted: { messenger.logout(); settings.close() }
    }

}
