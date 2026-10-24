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
    background: Rectangle { color: theme.surface; border.color: theme.divider; radius: 14 }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: popup.reduceMotion ? 0 : 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: popup.reduceMotion ? 0 : 100 } }
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
    onHeightChanged: if (opened) reposition()
    onClosed: { if (returnFocus) returnFocus.forceActiveFocus(); origin = null }
    readonly property var commands: {
        const m = message, entries = []
        if (!m.local) {
            entries.push({kind:"reply", title:"Ответить"}, {kind:"forward", title:"Переслать"},
                {kind:"important", title:m.important ? "Убрать из важных" : "Отметить как важное"})
        }
        if ((m.raw ? m.raw.text || "" : m.text || "").length) entries.push({kind:"copy", title:"Копировать текст"})
        if (!m.local && m.outgoing && !(m.raw && m.raw.action) && !(m.attachments || []).some(function(a) { return ["sticker", "gift", "call", "graffiti"].indexOf(a.type) >= 0 })) entries.push({kind:"edit", title:"Редактировать"})
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
                width: parent.width
                active: popup.opened && !popup.message.local
                sourceComponent: Column {
                    width: parent.width
                    spacing: 4
                    Flow {
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: popup.expanded ? actions.selectableReactions : actions.selectableReactions.slice(0, 7)
                            delegate: Button {
                                required property var modelData
                                objectName: "quickReaction"
                                width: 42; height: 44; padding: 6
                                Accessible.name: "Реакция " + modelData.reaction_id
                                highlighted: !!popup.message.raw && Number(popup.message.raw.reaction_id) === Number(modelData.reaction_id)
                                contentItem: ReactionImage { remoteSource: modelData.links.static }
                                onClicked: {
                                    popup.command("reaction:" + (highlighted ? 0 : modelData.reaction_id))
                                    popup.close()
                                }
                            }
                        }
                        ToolButton { text: popup.expanded ? "⌃" : "…"; width: 42; height: 44; Accessible.name: "Все реакции"; onClicked: { popup.expanded = !popup.expanded; actions.ensureReactionAssets() } }
                    }
                    Label { width: parent.width; visible: actions.selectableReactions.length === 0; text: "Каталог реакций пока недоступен"; color: theme.muted; wrapMode: Text.Wrap; font.pixelSize: 12 }
                    Rectangle { width: parent.width; height: 1; color: theme.divider }
                }
            }
            Repeater {
                model: popup.commands
                delegate: Button {
                    required property var modelData
                    objectName: "messageAction_" + modelData.kind
                    width: contents.width
                    height: 38
                    text: modelData.title
                    focusPolicy: Qt.StrongFocus
                    contentItem: RowLayout {
                        spacing: 12
                        ActionIcon { kind: modelData.kind; stroke: modelData.kind === "delete" || modelData.kind === "cancel" ? "#ff5c5c" : theme.muted; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                        Text { text: modelData.title; color: modelData.kind === "delete" || modelData.kind === "cancel" ? "#ff5c5c" : theme.ink; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    background: Rectangle { radius: 8; color: parent.hovered || parent.activeFocus ? theme.secondary : "transparent" }
                    onClicked: { popup.command(modelData.kind); popup.close() }
                }
            }
        }
    }
}
