import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    Layout.fillHeight: false
    property var keyboard: ({})
    property string peer: ""
    property string cmid: ""
    property string author: ""
    signal locationRequested()
    function click(action) {
        if (action.type === "open_link") Qt.openUrlExternally(action.link)
        else if (action.type === "open_app") Qt.openUrlExternally("https://vk.ru/app" + action.app_id + "#" + (action.hash || ""))
        else if (action.type === "vkpay") Qt.openUrlExternally("https://vk.ru/vkpay#" + (action.hash || ""))
        else if (action.type === "callback") messenger.actions.clickBotCallback(peer, cmid, String(keyboard.author_id || author), action.payload || "{}")
        else if (action.type === "text") messenger.actions.sendBotText(peer, action.label || "", action.payload || "")
        else if (action.type === "location") root.locationRequested()
    }
    Repeater {
        model: root.keyboard.buttons || []
        delegate: RowLayout {
            id: buttonRow
            required property var modelData
            Layout.fillWidth: true
            Repeater {
                model: buttonRow.modelData
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.action.label || (modelData.action.type === "location" ? "Отправить местоположение" : modelData.action.type)
                    enabled: !messenger.actions.busy && !messenger.demo
                    onClicked: root.click(modelData.action)
                }
            }
        }
    }
}
