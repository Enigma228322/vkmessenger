import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "autoReplyDialog"
    required property var theme
    property string peer: ""
    property string feedback: ""
    property var config: ({})
    property bool checkSucceeded: false
    property bool tokenSaved: false
    readonly property bool customModel: modelPicker.count > 0 && modelPicker.currentIndex === modelPicker.count - 1
    readonly property string modelId: customModel ? customId.text.trim() : modelPicker.currentText
    readonly property bool running: config.enabled === true

    function resetCheck() {
        messenger.autoReply.cancelModelCheck()
        checkSucceeded = false
        feedback = ""
    }
    function selectModel(savedId) {
        const choices = Array.from(messenger.autoReply.modelChoices())
        if (savedId && choices.indexOf(savedId) < 0) choices.push(savedId)
        modelPicker.model = choices.concat(["Другая модель…"])
        modelPicker.currentIndex = savedId ? choices.indexOf(savedId) : 0
        customId.clear()
        resetCheck()
    }
    function present() {
        peer = messenger.selectedPeer
        config = messenger.autoReply.settings(peer)
        selectModel(config.model)
        notes.text = config.notes || ""
        feedback = ""
        tokenSaved = messenger.autoReply.hasSavedToken()
        open()
    }
    function storeNotes() {
        if (peer.length && peer === messenger.selectedPeer)
            messenger.autoReply.saveNotes(peer, notes.text)
    }
    function saveEnteredToken() {
        const error = messenger.autoReply.saveEditedToken()
        if (error) {
            checkSucceeded = false
            feedback = error
        }
        return !error
    }
    onAboutToHide: {
        resetCheck()
        if (saveEnteredToken()) apiKey.clear()
    }
    Connections {
        target: messenger.autoReply
        function onModelChecked(error, sample) {
            if (!error.length) {
                dialog.tokenSaved = true
                apiKey.clear()
            }
            dialog.checkSucceeded = !error.length
            dialog.feedback = error || (sample ? "Пробный ответ: " + sample : "Polza.ai подтвердила токен.")
        }
        function onAccountChanged() {
            apiKey.clear()
            dialog.tokenSaved = false
            if (dialog.visible) dialog.close()
        }
        function onSettingsChanged() { dialog.config = messenger.autoReply.settings(dialog.peer) }
    }
    Connections {
        target: messenger
        function onChanged() {
            if (dialog.visible && (!messenger.connected || messenger.selectedPeer !== dialog.peer)) dialog.close()
        }
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(464, parent.width - 32)
    height: Math.min(implicitHeight, parent.height - 32)
    modal: true
    padding: 20
    title: "Автоответы"
    header: ColumnLayout {
        spacing: 3
        Label {
            Layout.topMargin: 18; Layout.leftMargin: 20
            text: dialog.title; font.pixelSize: 20; font.weight: Font.DemiBold; color: theme.ink
        }
        Label {
            Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
            text: messenger.title; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.muted
        }
    }
    background: Rectangle { color: theme.surface; border.color: theme.divider; radius: 16 }
    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        implicitHeight: form.implicitHeight
        ColumnLayout {
            id: form
            width: scroll.availableWidth
            spacing: 8
            Switch {
                id: activeSwitch
                objectName: "autoReplyToggle"
                text: "Отвечать за меня"
                checked: dialog.running
                onToggled: {
                    dialog.resetCheck()
                    if (checked) {
                        dialog.feedback = messenger.autoReply.enable(dialog.peer, dialog.modelId, apiKey.text)
                        if (!dialog.feedback.length) {
                            dialog.tokenSaved = true
                            apiKey.clear()
                        }
                    } else messenger.autoReply.disable(dialog.peer)
                    checked = Qt.binding(function() { return dialog.running })
                }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.muted
                text: "Polza.ai передаст модели OpenAI ваши заметки и до 40 последних сообщений этого чата. Пока автоответы включены, VK удерживает компьютер от сна при бездействии и отвечает даже при заблокированном экране."
            }
            Label { text: "Модель OpenAI в Polza.ai"; color: theme.ink }
            ComboBox {
                id: modelPicker
                objectName: "autoReplyModel"
                Layout.fillWidth: true; enabled: !dialog.running
                Accessible.name: "Модель OpenAI"
                onActivated: dialog.resetCheck()
            }
            TextField {
                id: customId
                Layout.fillWidth: true; visible: dialog.customModel; enabled: !dialog.running
                placeholderText: "Идентификатор модели"; Accessible.name: placeholderText
                selectByMouse: true; onTextChanged: dialog.resetCheck()
            }
            Label { visible: !dialog.running; text: "Токен Polza.ai"; color: theme.ink }
            TextField {
                id: apiKey
                objectName: "autoReplyApiKey"
                visible: !dialog.running; Layout.fillWidth: true
                echoMode: TextInput.Password
                Accessible.name: "Токен API Polza.ai"
                placeholderText: dialog.tokenSaved ? "Токен сохранён. Введите новый для замены" : "Введите токен из кабинета Polza.ai"
                onTextChanged: {
                    if (dialog.visible) messenger.autoReply.editToken(text)
                    dialog.resetCheck()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                enabled: !messenger.autoReply.checkingModel
                Button {
                    objectName: "autoReplyCheck"
                    text: "Проверить токен"
                    onClicked: {
                        dialog.resetCheck()
                        messenger.autoReply.checkModel(dialog.modelId, apiKey.text, false)
                    }
                }
                Button {
                    objectName: "autoReplySample"
                    enabled: dialog.modelId.length > 0
                    text: "Пробный ответ"
                    onClicked: {
                        dialog.resetCheck()
                        messenger.autoReply.checkModel(dialog.modelId, apiKey.text, true)
                    }
                }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                text: messenger.autoReply.checkingModel ? "Ожидаю ответ Polza.ai…" : dialog.feedback
                visible: text.length > 0
                color: messenger.autoReply.checkingModel || dialog.checkSucceeded ? theme.ink : theme.errorText
            }
            Label {
                objectName: "autoReplyDiagnostic"
                Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                text: dialog.config.error || (dialog.running && !messenger.autoReply.sleepProtected ? "Система не подтвердила защиту от сна." : dialog.config.status || "")
                visible: text.length > 0
                color: dialog.config.error ? theme.errorText : theme.muted
            }
            Label { text: "Заметки для автоответчика"; color: theme.ink }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 76
                background: Rectangle { color: theme.secondary; radius: 6; border.color: theme.divider }
                TextArea {
                    id: notes
                    objectName: "autoReplyNotes"
                    Accessible.name: "Заметки для автоответчика"
                    placeholderText: "Например, по пятницам я свободен после 18:00"
                    wrapMode: TextEdit.Wrap; selectByMouse: true
                    onTextChanged: if (dialog.visible) dialog.storeNotes()
                }
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            text: "Готово"
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: if (dialog.saveEnteredToken()) dialog.accept()
        }
    }
    onAccepted: close()
}
