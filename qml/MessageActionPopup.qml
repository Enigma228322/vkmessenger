import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popup
    objectName: "messageActionPopup"
    required property var theme
    required property var actions
    property var message: ({})
    property bool canManage: false
    property bool reduceMotion: false
    property bool expanded: false
    property Item origin: null
    property Item returnFocus: null
    signal command(string kind)
    padding: 8
    width: Math.min(380, parent ? parent.width - 24 : 380)
    height: Math.min(implicitHeight, parent ? parent.height - 24 : implicitHeight)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: MenuSurface { color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 0.96); borderColor: theme.glassBorder }
    transformOrigin: Item.TopRight
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: popup.reduceMotion ? 0 : 180; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: popup.reduceMotion ? 0 : 220; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: popup.reduceMotion ? 0 : theme.hoverDuration; easing.type: Easing.OutCubic } }
    function showAt(item, value, focusItem) {
        origin = item
        message = value
        returnFocus = focusItem
        expanded = false
        actions.ensureReactionAssets()
        open()
        Qt.callLater(reposition)
    }
    function reposition() {
        if (!origin || !parent) { close(); return }
        const point = origin.mapToItem(parent, 0, 0)
        x = Math.max(12, Math.min(parent.width - width - 12, point.x + origin.width - width))
        y = Math.max(12, Math.min(parent.height - height - 12, point.y + origin.height))
    }
    function navigationItems() {
        const items = reactionLoader.item ? reactionLoader.item.navigationItems() : []
        for (let i = 0; i < commandRepeater.count; ++i) {
            const item = commandRepeater.itemAt(i)
            if (item && item.visible && item.enabled) items.push(item)
        }
        return items
    }
    function reveal(item) {
        if (!item || !opened) return
        const point = item.mapToItem(scroller.contentItem, 0, 0)
        let next = scroller.contentY
        if (point.y < next) next = point.y
        else if (point.y + item.height > next + scroller.height) next = point.y + item.height - scroller.height
        scroller.contentY = Math.max(0, Math.min(Math.max(0, scroller.contentHeight - scroller.height), next))
    }
    function handleKey(event, current) {
        const items = navigationItems()
        if (!items.length) return
        const index = items.indexOf(current)
        let target = -1
        if (event.key === Qt.Key_Home) target = 0
        else if (event.key === Qt.Key_End) target = items.length - 1
        else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))
                 || event.key === Qt.Key_Up || event.key === Qt.Key_Left) target = (index - 1 + items.length) % items.length
        else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Down || event.key === Qt.Key_Right) target = (index + 1) % items.length
        else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && index >= 0) {
            event.accepted = true
            current.clicked()
            return
        }
        if (target >= 0) {
            event.accepted = true
            items[target].forceActiveFocus(Qt.TabFocusReason)
            reveal(items[target])
        }
    }
    onOpened: {
        scroller.contentY = 0
        Qt.callLater(function() {
            if (!popup.opened) return
            const first = commandRepeater.itemAt(0) || popup.navigationItems()[0]
            if (first) { first.forceActiveFocus(Qt.PopupFocusReason); popup.reveal(first) }
        })
    }
    onHeightChanged: if (opened) reposition()
    onClosed: { if (returnFocus) returnFocus.forceActiveFocus(); origin = null }
    function canEdit(m) {
        return !!m.outgoing && !m.local && !m.gap && !(m.raw && m.raw.action)
            && !(m.attachments || []).some(function(a) { return ["sticker", "gift", "call", "graffiti"].indexOf(a.type) >= 0 })
    }
    readonly property var commands: {
        const m = message, entries = []
        if (!m.local) {
            entries.push({kind:"reply", title:"Ответить"}, {kind:"forward", title:"Переслать"},
                {kind:"important", title:m.important ? "Убрать из важных" : "Отметить как важное"})
        }
        if ((m.raw ? m.raw.text || "" : m.text || "").length) entries.push({kind:"copy", title:"Копировать текст"})
        if (canEdit(m)) entries.push({kind:"edit", title:"Редактировать"})
        if (!m.local) {
            entries.push({kind:"delete", title:"Удалить…"}, {kind:"select", title:"Выбрать"})
            if (canManage) entries.push({kind:"pin", title:"Закрепить"})
            entries.push({kind:"link", title:"Копировать ссылку"})
            if (m.raw && (m.raw.reactions || []).length) entries.push({kind:"reacted", title:"Кто отреагировал"})
        } else if (m.delivery !== "sent" && m.delivery !== "sending") {
            entries.push({kind:"retry", title:"Повторить отправку"}, {kind:"cancel", title:"Убрать из очереди"})
        }
        return entries
    }
    contentItem: Flickable {
        id: scroller
        objectName: "messageActionScroll"
        implicitHeight: contents.implicitHeight
        contentHeight: contents.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: contents
            width: parent.width
            spacing: 4
            Loader {
                id: reactionLoader
                width: parent.width
                active: popup.opened && !popup.message.local
                sourceComponent: Column {
                    width: parent.width
                    spacing: 4
                    function navigationItems() {
                        const items = []
                        for (let i = 0; i < reactionRepeater.count; ++i) {
                            const item = reactionRepeater.itemAt(i)
                            if (item && item.visible && item.enabled) items.push(item)
                        }
                        if (moreReactions.visible) items.push(moreReactions)
                        return items
                    }
                    Flow {
                        width: parent.width
                        spacing: 2
                        Repeater {
                            id: reactionRepeater
                            model: popup.expanded ? actions.selectableReactions : actions.selectableReactions.slice(0, 7)
                            delegate: Button {
                                id: reactionButton
                                required property var modelData
                                objectName: "quickReaction"
                                width: 42; height: 44; padding: 6
                                focusPolicy: Qt.StrongFocus
                                Accessible.name: (highlighted ? "Убрать реакцию " : "Поставить реакцию ") + modelData.reaction_id
                                Accessible.checkable: true
                                Accessible.checked: highlighted
                                highlighted: !!popup.message.raw && Number(popup.message.raw.reaction_id) === Number(modelData.reaction_id)
                                contentItem: ReactionImage { remoteSource: modelData.links.static }
                                background: Rectangle {
                                    radius: 12
                                    color: reactionButton.down ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.24) : reactionButton.highlighted || reactionButton.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.15) : "transparent"
                                    border.color: reactionButton.activeFocus ? theme.accent : reactionButton.highlighted ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.4) : "transparent"
                                }
                                Keys.onPressed: function(event) { popup.handleKey(event, reactionButton) }
                                onActiveFocusChanged: if (activeFocus) popup.reveal(reactionButton)
                                onClicked: {
                                    popup.command("reaction:" + (highlighted ? 0 : modelData.reaction_id))
                                    popup.close()
                                }
                            }
                        }
                        ToolButton {
                            id: moreReactions
                            objectName: "messageActionMoreReactions"
                            text: popup.expanded ? "⌃" : "…"; width: 42; height: 44
                            visible: actions.selectableReactions.length > 7
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: popup.expanded ? "Свернуть реакции" : "Все реакции"
                            Accessible.description: popup.expanded ? "Показан полный каталог реакций" : "Показаны первые семь реакций"
                            background: Rectangle {
                                radius: 12
                                color: moreReactions.down || moreReactions.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.15) : "transparent"
                                border.color: moreReactions.activeFocus ? theme.accent : "transparent"
                            }
                            Keys.onPressed: function(event) { popup.handleKey(event, moreReactions) }
                            onActiveFocusChanged: if (activeFocus) popup.reveal(moreReactions)
                            onClicked: {
                                popup.expanded = !popup.expanded
                                actions.ensureReactionAssets()
                                Qt.callLater(function() { if (moreReactions.activeFocus) popup.reveal(moreReactions) })
                            }
                        }
                    }
                    Label { width: parent.width; visible: actions.selectableReactions.length === 0; text: "Каталог реакций пока недоступен"; color: theme.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
                    Rectangle { width: parent.width; height: 1; color: theme.divider }
                }
            }
            Repeater {
                id: commandRepeater
                model: popup.commands
                delegate: Button {
                    id: commandButton
                    required property var modelData
                    objectName: "messageAction_" + modelData.kind
                    width: contents.width
                    height: 38
                    text: modelData.title
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: modelData.title
                    Keys.onPressed: function(event) { popup.handleKey(event, commandButton) }
                    onActiveFocusChanged: if (activeFocus) popup.reveal(commandButton)
                    contentItem: RowLayout {
                        spacing: 12
                        ActionIcon { kind: modelData.kind; stroke: modelData.kind === "delete" || modelData.kind === "cancel" ? "#ff5c5c" : theme.muted; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                        Text { text: modelData.title; color: modelData.kind === "delete" || modelData.kind === "cancel" ? "#ff5c5c" : theme.ink; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    background: Rectangle {
                        radius: 10
                        color: commandButton.down ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.24) : commandButton.hovered || commandButton.activeFocus ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent"
                        border.color: commandButton.activeFocus ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.65) : "transparent"
                        Behavior on color { enabled: !popup.reduceMotion; ColorAnimation { duration: theme.hoverDuration; easing.type: Easing.OutCubic } }
                    }
                    onClicked: { popup.command(modelData.kind); popup.close() }
                }
            }
        }
    }
}
