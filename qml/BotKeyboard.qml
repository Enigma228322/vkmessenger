import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    Layout.fillHeight: false
    required property var theme
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
                    id: key
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    horizontalPadding: 10
                    verticalPadding: 6
                    implicitHeight: Math.max(32, implicitContentHeight + topPadding + bottomPadding)
                    hoverEnabled: true
                    text: modelData.action.label || (modelData.action.type === "location" ? "Отправить местоположение" : modelData.action.type)
                    enabled: !messenger.actions.busy && !messenger.demo
                    onClicked: root.click(modelData.action)
                    background: GlassSurface {
                        radius: 12
                        elevated: false
                        dark: root.theme.dark
                        color: key.down ? root.theme.pressed : key.hovered ? root.theme.glassSelection : root.theme.glassInput
                        borderColor: key.visualFocus ? root.theme.accent : root.theme.glassBorder
                        highlightColor: root.theme.glassHighlight
                        Behavior on color {
                            enabled: !messenger.reduceMotion
                            ColorAnimation { duration: root.theme.hoverDuration; easing.type: Easing.OutCubic }
                        }
                    }
                    contentItem: Text {
                        id: caption
                        text: key.text
                        textFormat: Text.PlainText
                        font: key.font
                        color: key.enabled ? root.theme.accent : root.theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    ToolTip.visible: caption.truncated && (key.hovered || key.visualFocus)
                    ToolTip.delay: 600
                    ToolTip.text: text
                }
            }
        }
    }
}
