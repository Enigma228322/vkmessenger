import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: browser
    width: Math.min(1040, parent.width - 50)
    height: Math.min(740, parent.height - 40)
    anchors.centerIn: parent
    modal: true
    title: "Действия и инструменты VK Мессенджера"
    standardButtons: Dialog.Close
    property var selected: ({})
    property var values: ({})
    property string query: ""
    property string cmid: ""
    property bool resultsVisible: false
    function choose(method, overrides) {
        for (const spec of messenger.methods) {
            if (spec.name === method) {
                selected = spec
                const defaults = messenger.methodDefaults(cmid)
                if (method === "messages.send" || method === "messages.setActivity") {
                    delete defaults.user_id
                    delete defaults.chat_id
                    if (method === "messages.send") delete defaults.attachment
                }
                values = Object.assign({}, defaults, overrides || {})
                fields.model = []
                fields.model = spec.parameters
                resultsVisible = false
                return
            }
        }
    }
    function openMethod(method, overrides, messageCmid) {
        cmid = messageCmid || ""
        choose(method, overrides)
        open()
    }
    function run() {
        let parameters = {}
        for (const field of selected.parameters || []) {
            const value = values[field.name]
            if (value !== undefined && String(value).length) parameters[field.name] = String(value)
        }
        messenger.execute(selected.name, parameters)
        resultsVisible = true
    }
    background: Rectangle { color: "white"; radius: 12; border.color: "#dfe6ef" }
    contentItem: RowLayout {
        spacing: 18
        ColumnLayout {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            TextField { Layout.fillWidth: true; placeholderText: "Найти действие / метод"; onTextChanged: browser.query = text.toLowerCase() }
            ListView {
                Layout.fillHeight: true
                Layout.fillWidth: true
                clip: true
                spacing: 4
                model: messenger.methods
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    property bool matches: (modelData.name + " " + modelData.title + " " + modelData.category).toLowerCase().indexOf(browser.query) >= 0
                    width: ListView.view.width
                    height: matches ? 66 : 0
                    visible: matches
                    highlighted: modelData.name === browser.selected.name
                    contentItem: Column {
                        Label { width: parent.width; text: modelData.title; elide: Text.ElideRight }
                        Label { width: parent.width; text: modelData.name; font.pixelSize: 10; color: "#788697"; elide: Text.ElideRight }
                    }
                    onClicked: browser.choose(modelData.name, {})
                }
            }
        }
        Rectangle { width: 1; Layout.fillHeight: true; color: "#e2e8f0" }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label { Layout.fillWidth: true; text: browser.selected.title || "Выберите действие"; font.pixelSize: 19; font.bold: true; wrapMode: Text.Wrap }
            Label { text: browser.selected.name || ""; color: "#788697"; font.pixelSize: 11 }
            RowLayout {
                Button { text: "Параметры"; onClicked: browser.resultsVisible = false }
                Button { text: "Результат"; onClicked: browser.resultsVisible = true }
                Item { Layout.fillWidth: true }
                Button { text: messenger.operationBusy ? "Выполняется…" : "Выполнить"; enabled: !messenger.operationBusy && !!browser.selected.name; onClicked: browser.run() }
            }
            RowLayout {
                visible: browser.resultsVisible && (browser.selected.name === "messages.search" || browser.selected.name === "messages.getConversationMembers" || browser.selected.name === "messages.getImportantMessages" || browser.selected.name === "messages.getConversations")
                Button { text: "← Предыдущая страница"; enabled: !messenger.operationBusy && Number(browser.values.offset || 0) > 0; onClicked: { browser.values.offset = String(Math.max(0, Number(browser.values.offset || 0) - Number(browser.values.count || 50))); browser.run() } }
                Button { text: "Следующая страница →"; enabled: !messenger.operationBusy && !!messenger.operationResult && (messenger.operationResult.items || []).length >= Number(browser.values.count || 50); onClicked: { browser.values.offset = String(Number(browser.values.offset || 0) + Number(browser.values.count || 50)); browser.run() } }
            }
            Label { Layout.fillWidth: true; visible: messenger.operationError.length > 0; text: messenger.operationError; color: "#b3432a"; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            ScrollView {
                visible: !browser.resultsVisible
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Repeater {
                        id: fields
                        delegate: ColumnLayout {
                            id: fieldRow
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: fieldRow.modelData.label + (fieldRow.modelData.required ? " *" : ""); font.bold: true }
                            TextField {
                                Layout.fillWidth: true
                                visible: fieldRow.modelData.type !== "text" && fieldRow.modelData.type !== "json" && fieldRow.modelData.type !== "bool"
                                text: String(browser.values[fieldRow.modelData.name] || "")
                                placeholderText: fieldRow.modelData.name + (fieldRow.modelData.type === "ids" ? " · через запятую" : "")
                                selectByMouse: true
                                onTextEdited: browser.values[fieldRow.modelData.name] = text
                            }
                            TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 100
                                visible: fieldRow.modelData.type === "text" || fieldRow.modelData.type === "json"
                                text: String(browser.values[fieldRow.modelData.name] || "")
                                wrapMode: TextEdit.WrapAnywhere
                                selectByMouse: true
                                placeholderText: fieldRow.modelData.name
                                onTextChanged: if (visible) browser.values[fieldRow.modelData.name] = text
                            }
                            ComboBox {
                                Layout.fillWidth: true
                                visible: fieldRow.modelData.type === "bool"
                                model: ["Не передавать", "Да", "Нет"]
                                currentIndex: String(browser.values[fieldRow.modelData.name]) === "1" ? 1 : String(browser.values[fieldRow.modelData.name]) === "0" ? 2 : 0
                                onActivated: browser.values[fieldRow.modelData.name] = currentIndex === 0 ? "" : currentIndex === 1 ? "1" : "0"
                            }
                            Label { text: fieldRow.modelData.name; color: "#8a96a6"; font.pixelSize: 10 }
                        }
                    }
                }
            }
            ResultView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: browser.resultsVisible
                result: messenger.operationResult
                peer: browser.values.peer_id || messenger.selectedPeer
                onMethodRequested: function(method, values) { browser.choose(method, values) }
            }
        }
    }
}
