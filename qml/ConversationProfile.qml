import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    property string avatar: ""
    property int section: 0
    property string query: ""
    property Item returnFocus: null
    signal actionRequested(string kind)
    edge: Qt.RightEdge
    width: Math.min(400, parent ? parent.width - 20 : 400)
    height: parent ? parent.height : 700
    modal: false
    interactive: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: theme.surface; border.color: theme.divider; radius: 12 }
    enter: Transition { NumberAnimation { property: "position"; to: 1; duration: client.reduceMotion ? 0 : 180; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "position"; to: 0; duration: client.reduceMotion ? 0 : 150; easing.type: Easing.OutCubic } }
    function present(focusItem) {
        peer = client.selectedPeer
        peerTitle = client.title
        avatar = ""
        for (let i = 0; i < client.conversations.rowCount(); ++i) {
            const row = client.conversations.get(i)
            if (row.peer === peer) { avatar = row.avatar || ""; break }
        }
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
    onClosed: { controller.reset(); if (returnFocus) returnFocus.forceActiveFocus() }
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            ToolButton { text: "×"; Accessible.name: "Закрыть информацию"; onClicked: panel.close() }
            Label { text: "Информация"; color: theme.ink; Layout.fillWidth: true }
            ToolButton { text: "…"; Accessible.name: "Настройки беседы"; onClicked: panel.actionRequested("settings") }
        }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 88; height: 88; radius: 44
            color: theme.secondary
            Text { anchors.centerIn: parent; text: panel.peerTitle.substring(0, 1); color: theme.accent; font.pixelSize: 36 }
            Image { anchors.fill: parent; source: { client.media.revision; return client.media.source(panel.avatar) }
                fillMode: Image.PreserveAspectFit; asynchronous: true }
        }
        Label { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; text: panel.peerTitle; textFormat: Text.PlainText; color: theme.ink; font.pixelSize: 20; font.bold: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button { text: "Уведомления"; onClicked: panel.actionRequested("notifications") }
            Button { text: "Поиск в переписке"; onClicked: panel.actionRequested("search") }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12; Layout.rightMargin: 12
            visible: panel.chat
            Button { text: "Ссылка-приглашение"; enabled: !controller.busy; onClicked: controller.loadInvite(panel.peer) }
            ToolButton { text: "Копировать"; visible: controller.inviteLink.length > 0; onClicked: controller.copyText(controller.inviteLink) }
        }
        Label { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; visible: controller.inviteLink.length > 0; text: controller.inviteLink; textFormat: Text.PlainText; color: theme.accent; elide: Text.ElideMiddle }
        Flickable {
            Layout.fillWidth: true; Layout.preferredHeight: 40
            contentWidth: tabs.implicitWidth; clip: true; boundsBehavior: Flickable.StopAtBounds
            Row {
                id: tabs
                Repeater {
                    model: [{name:"Участники", section:0}, {name:"Фото", section:1}, {name:"Видео", section:2}, {name:"Файлы", section:3}, {name:"Ссылки", section:4}]
                    delegate: Button {
                        required property var modelData
                        visible: modelData.section !== 0 || panel.chat
                        width: visible ? implicitWidth : 0
                        text: modelData.name
                        highlighted: panel.section === modelData.section
                        onClicked: { panel.section = modelData.section; panel.query = ""; panel.loadSection() }
                    }
                }
            }
        }
        TextField { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; placeholderText: panel.section === 0 ? "Поиск участников" : "Поиск в загруженном"; text: panel.query; onTextEdited: panel.query = text.toLowerCase() }
        Button { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; visible: panel.chat && panel.section === 0; text: "Добавить участников"; onClicked: panel.actionRequested("add") }
        Label { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; visible: controller.error.length > 0; text: controller.error; textFormat: Text.PlainText; color: "#ff6b6b"; wrapMode: Text.Wrap }
        BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: controller.busy; running: visible }
        ListView {
            id: results
            objectName: "profileResults"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: controller.results
            spacing: 4
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                id: result
                required property var modelData
                readonly property bool matches: String(modelData.title || "").toLowerCase().indexOf(panel.query) >= 0
                width: results.width - 24
                x: 12
                visible: matches
                height: matches ? implicitHeight : 0
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: result.modelData.title || "Участник"; textFormat: Text.PlainText; color: theme.ink; elide: Text.ElideRight }
                    Label { visible: panel.section === 0 && !!result.modelData.is_admin; text: "Администратор"; color: theme.muted; font.pixelSize: 11 }
                    ToolButton { text:"…"; visible: panel.section === 0 && panel.manage && !result.modelData.is_owner; onClicked: memberMenu.popup() }
                    Menu {
                        id: memberMenu
                        MenuItem { text: "Управление участниками…"; onTriggered: panel.actionRequested("members") }
                    }
                }
                AttachmentView { Layout.fillWidth: true; visible: panel.section !== 0; attachments: result.modelData.attachments || []; animationsEnabled: panel.opened && !client.reduceMotion }
                Button { visible: panel.section !== 0 && !!result.modelData.cmid; text: "К сообщению"; onClicked: { client.openMessage(panel.peer, String(result.modelData.cmid)); panel.close() } }
            }
            footer: Button {
                width: results.width
                visible: controller.hasMore
                text: "Загрузить ещё"
                enabled: !controller.busy
                onClicked: { if (panel.section === 0) controller.loadMembers(panel.peer, true); else controller.loadAttachments(panel.peer, ["", "photo", "video", "doc", "link"][panel.section], true) }
            }
            Label { anchors.centerIn: parent; visible: !controller.busy && !controller.results.length && !controller.error.length; text: "Пока ничего нет"; color: theme.muted }
        }
    }
}
