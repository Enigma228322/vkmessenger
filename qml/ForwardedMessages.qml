import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Column {
    id: forwards
    objectName: "forwardedMessages"
    property var messages: []
    property string messageKey: ""
    property bool loadImages: true
    property bool animationsEnabled: true
    property color photoBackground: design.secondary
    property real viewportTop: 0
    property real viewportHeight: 0
    property int shown: 3
    readonly property int visibleCount: Math.min(shown, messages.length)
    readonly property int remaining: Math.max(0, messages.length - visibleCount)
    spacing: 8
    onMessageKeyChanged: shown = 3
    Theme { id: design; dark: messenger.darkTheme }
    function collapse() {
        shown = 3
        more.forceActiveFocus(Qt.TabFocusReason)
    }
    function settleCards() {
        // New delegates start at y=0. Position them before allowing downloads.
        for (const child of children)
            if (child.objectName === "forwardedMessageCard") child.prepareGeometry()
        forceLayout()
        for (const child of children)
            if (child.objectName === "forwardedMessageCard") child.positioned = true
    }
    RowLayout {
        width: parent.width; spacing: 6
        ActionIcon { Layout.preferredWidth: 16; Layout.preferredHeight: 16; kind: "forward"; stroke: design.muted }
        Text { Layout.fillWidth: true; text: "Пересланные сообщения"; color: design.muted; font.pixelSize: 12; elide: Text.ElideRight }
        Text { text: String(forwards.messages.length); color: design.muted; font.pixelSize: 12 }
    }
    Repeater {
        model: forwards.visibleCount
        delegate: Rectangle {
            id: card
            objectName: "forwardedMessageCard"
            required property int index
            readonly property var message: forwards.messages[index] || ({})
            readonly property var attachments: message.attachments || []
            property bool positioned: false
            Component.onCompleted: Qt.callLater(forwards.settleCards)
            function prepareGeometry() { contents.forceLayout() }
            readonly property string author: messenger.peerProfile(String(message.from_id || "")).title || "Пересланное сообщение"
            readonly property real viewportY: forwards.viewportTop + y
            readonly property bool nearViewport: !forwards.viewportHeight || (viewportY + height > -forwards.viewportHeight && viewportY < 2 * forwards.viewportHeight)
            readonly property bool inViewport: !forwards.viewportHeight || (viewportY + height > 0 && viewportY < forwards.viewportHeight)
            width: forwards.width
            height: contents.implicitHeight + 20
            radius: 9; color: design.secondary
            Rectangle { x: 0; y: 8; width: 3; height: parent.height - 16; radius: 1.5; color: design.accent; opacity: 0.7 }
            Column {
                id: contents
                x: 12; y: 10; width: Math.max(1, parent.width - 24); spacing: 6
                RowLayout {
                    width: parent.width; spacing: 8
                    Text { objectName: "forwardedMessageAuthor"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: card.author; color: design.accent; font.pixelSize: 12; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                    Text {
                        text: Number(card.message.date || 0) > 0 ? Qt.formatDateTime(new Date(Number(card.message.date) * 1000), "dd.MM.yyyy · HH:mm") : ""
                        visible: text.length > 0; color: design.muted; font.pixelSize: 10
                    }
                }
                TextEdit {
                    objectName: "forwardedMessageText"
                    width: parent.width; height: Math.ceil(contentHeight)
                    text: card.message.text || ""; visible: text.length > 0
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText
                    color: design.ink; font.pixelSize: 14
                    selectionColor: design.accent; selectedTextColor: "white"
                }
                Text {
                    width: parent.width; visible: !card.message.text && !card.attachments.length
                    text: (card.message.fwd_messages || []).length ? "Вложенных сообщений: " + card.message.fwd_messages.length : "Сообщение без текста"
                    color: design.muted; font.pixelSize: 12; wrapMode: Text.Wrap
                }
                Loader {
                    width: parent.width
                    active: card.attachments.length > 0
                    visible: active
                    sourceComponent: AttachmentView {
                        objectName: "forwardedMessageAttachments"
                        attachments: card.attachments
                        loadImages: card.positioned && forwards.loadImages && card.nearViewport
                        animationsEnabled: forwards.animationsEnabled && card.inViewport
                        photoBackground: forwards.photoBackground
                    }
                }
            }
        }
    }
    RowLayout {
        width: parent.width; spacing: 8
        visible: forwards.remaining > 0 || forwards.visibleCount > 3
        Button {
            id: more
            objectName: "showMoreForwarded"
            Layout.fillWidth: true
            visible: forwards.remaining > 0
            text: "Показать ещё: " + forwards.remaining
            Accessible.name: "Показать следующие пересланные сообщения. Осталось: " + forwards.remaining
            onClicked: forwards.shown += Math.min(5, forwards.remaining)
        }
        ToolButton {
            objectName: "collapseForwarded"
            visible: forwards.visibleCount > 3
            text: "Свернуть"
            Accessible.name: "Свернуть пересланные сообщения"
            onClicked: forwards.collapse()
        }
    }
}
