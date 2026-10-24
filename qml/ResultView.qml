import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: view
    property var result: null
    property string peer: ""
    signal methodRequested(string method, var values)
    clip: true
    spacing: 10
    ScrollBar.vertical: ScrollBar {}
    function name(id) {
        const profiles = (result && result.profiles) || []
        for (const p of profiles) if (String(p.id) === String(id)) return p.first_name + " " + p.last_name
        return String(id || "")
    }
    function build(value) {
        if (value === null || value === undefined) return []
        let values = Array.isArray(value) ? value : (value.items || value.reactions || value.assets || (value.groups && value.groups.length ? value.groups : value.profiles) || [])
        if (!Array.isArray(values)) values = []
        let cards = []
        for (const item of values) {
            if (typeof item !== "object" || item === null) { cards.push({raw: item, title: String(item), body: "", peer: "", cmid: "", member: "", attachments: []}); continue }
            if (item.messages) { cards = cards.concat(build({items: item.messages})); continue }
            const message = item.last_message || item
            const conversation = item.conversation || item
            const peerId = String(message.peer_id || (conversation.peer && conversation.peer.id) || "")
            const member = item.member_id || item.user_id ? String(item.member_id || item.user_id) : ""
            cards.push({raw: item, title: member ? name(member) + (item.is_admin ? " · администратор" : "") + (item.reaction_id ? " · реакция " + item.reaction_id : "")
                : (conversation.chat_settings && conversation.chat_settings.title) || item.name || (item.first_name ? item.first_name + " " + (item.last_name || "") : "") || name(message.from_id) || peerId,
                body: message.text || "", peer: peerId, cmid: message.conversation_message_id ? String(message.conversation_message_id) : "", member: member,
                attachments: message.attachments || (item.attachment ? [item.attachment] : [])})
        }
        if (!cards.length) cards.push({raw: value, title: typeof value === "object" ? "Результат" : String(value), body: "", peer: "", cmid: "", member: "", attachments: []})
        return cards
    }
    model: build(result)
    delegate: Rectangle {
        id: card
        required property var modelData
        width: ListView.view.width
        height: content.implicitHeight + 24
        color: "#f3f6fa"
        radius: 10
        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Label { Layout.fillWidth: true; text: card.modelData.title; font.bold: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            Image { Layout.preferredWidth: 48; Layout.preferredHeight: visible ? 48 : 0; visible: !!card.modelData.raw && !!card.modelData.raw.links; source: visible ? card.modelData.raw.links.static : ""; fillMode: Image.PreserveAspectFit; asynchronous: true }
            TextEdit { Layout.fillWidth: true; visible: text.length > 0; text: card.modelData.body; wrapMode: TextEdit.Wrap; readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText }
            AttachmentView { Layout.fillWidth: true; attachments: card.modelData.attachments }
            RowLayout {
                visible: card.modelData.peer.length > 0 || card.modelData.member.length > 0
                Button {
                    visible: card.modelData.peer.length > 0
                    text: card.modelData.cmid.length ? "Перейти к сообщению" : "Открыть диалог"
                    onClicked: {
                        if (card.modelData.cmid.length) messenger.openMessage(card.modelData.peer, card.modelData.cmid)
                        else messenger.selectConversation(card.modelData.peer, card.modelData.title)
                    }
                }
                Button {
                    visible: card.modelData.member.length > 0
                    text: "Действия с участником"
                    onClicked: memberMenu.popup()
                    Menu {
                        id: memberMenu
                        MenuItem { text: "Назначить администратором"; onTriggered: view.methodRequested("messages.setMemberRole", {peer_id: view.peer, member_id: card.modelData.member, role: "admin"}) }
                        MenuItem { text: "Снять права администратора"; onTriggered: view.methodRequested("messages.setMemberRole", {peer_id: view.peer, member_id: card.modelData.member, role: "member"}) }
                        MenuItem { text: "Передать владение"; onTriggered: view.methodRequested("messages.setMemberRole", {peer_id: view.peer, member_id: card.modelData.member, role: "owner"}) }
                        MenuItem { text: "Ограничить отправку сообщений"; onTriggered: view.methodRequested("messages.changeConversationMemberRestrictions", {peer_id: view.peer, member_ids_long: card.modelData.member, action: "ro", "for": "3600"}) }
                        MenuItem { text: "Разрешить отправку сообщений"; onTriggered: view.methodRequested("messages.changeConversationMemberRestrictions", {peer_id: view.peer, member_ids_long: card.modelData.member, action: "rw"}) }
                        MenuItem { text: "Удалить из чата"; onTriggered: view.methodRequested("messages.removeChatUser", {chat_id: String(Number(view.peer) - 2000000000), member_id: card.modelData.member}) }
                    }
                }
            }
            Button {
                text: details.visible ? "Скрыть подробности" : "Все данные"
                onClicked: details.visible = !details.visible
            }
            TextEdit {
                id: details
                Layout.fillWidth: true
                visible: !card.modelData.body && !card.modelData.peer && !card.modelData.member
                text: JSON.stringify(card.modelData.raw, null, 2)
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                textFormat: TextEdit.PlainText
                font.pixelSize: 12
            }
        }
    }
}
