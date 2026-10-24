import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: bar
    objectName: "pinnedMessageBar"
    property string messageText: ""
    property color textColor: "#e1e3e6"
    property color secondaryColor: "#939393"
    property bool busy: false
    signal openMessage()
    signal unpin()
    implicitHeight: 56
    Layout.minimumHeight: 56
    Layout.preferredHeight: 56
    Layout.maximumHeight: 56
    spacing: 12
    clip: true
    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: 3
        Label { text: "Закреплённое сообщение"; color: bar.secondaryColor; font.pixelSize: 11 }
        Label {
            objectName: "pinnedMessagePreview"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: bar.messageText.replace(/[\r\n\u2028\u2029]+/g, " ") || "Вложение"
            textFormat: Text.PlainText
            color: bar.textColor
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: Text.ElideRight
        }
    }
    Button { text: "Перейти"; enabled: !bar.busy; onClicked: bar.openMessage() }
    Button { text: "Открепить"; enabled: !bar.busy; onClicked: bar.unpin() }
}
