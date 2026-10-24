import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: sheet
    objectName: "folderSheet"
    modal: true
    anchors.centerIn: parent
    width: Math.min(620, parent.width - 48)
    height: Math.min(650, parent.height - 48)
    padding: 24
    standardButtons: Dialog.Close
    title: editing ? (folderId.length ? "Изменить папку" : "Новая папка") : "Папки диалогов"
    property bool reducedMotion: false
    property bool editing: false
    property string folderId: ""
    property var chosen: []
    property var peers: []
    readonly property var controller: messenger.folders
    background: Rectangle { radius: 18; color: sheet.palette.window; border.color: sheet.palette.mid }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: sheet.reducedMotion ? 0 : 150 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: sheet.reducedMotion ? 0 : 100 } }
    function show() { editing = false; open(); controller.refresh() }
    function edit(folder) {
        folderId = folder ? folder.id : ""
        chosen = folder ? folder.peers.slice() : []
        nameField.text = folder ? folder.name : ""
        query.text = ""
        let rows = []
        for (let i = 0; i < messenger.conversations.rowCount(); ++i) rows.push(messenger.conversations.get(i))
        for (const peer of chosen) if (!rows.some(function(row) { return row.peer === peer })) rows.push({peer: peer, title: "Сохранённая переписка", avatar: ""})
        peers = rows; editing = true; nameField.forceActiveFocus()
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; visible: !sheet.editing; text: "Папки синхронизируются с VK. Их удаление сохраняет переписки."; wrapMode: Text.Wrap }
        RowLayout {
            visible: !sheet.editing
            Button { text: "Создать папку"; enabled: !sheet.controller.busy && !messenger.offline && !messenger.demo; onClicked: sheet.edit(null) }
            Button { text: "Обновить"; enabled: !sheet.controller.busy; onClicked: sheet.controller.refresh() }
        }
        TextField { id: nameField; objectName: "folderName"; visible: sheet.editing; Layout.fillWidth: true; maximumLength: 16; placeholderText: "Название папки" }
        TextField { id: query; visible: sheet.editing; Layout.fillWidth: true; placeholderText: "Найти переписку" }
        Label { visible: sheet.editing; text: "Выбрано переписок: " + sheet.chosen.length }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 6
            model: sheet.editing ? sheet.peers : sheet.controller.items
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                visible: !sheet.editing || String(modelData.title || "").toLowerCase().indexOf(query.text.toLowerCase()) >= 0
                height: visible ? 54 : 0
                contentItem: RowLayout {
                    CheckBox {
                        visible: sheet.editing
                        checked: sheet.chosen.indexOf(String(row.modelData.peer)) >= 0
                        onClicked: row.toggle()
                    }
                    Label { Layout.fillWidth: true; text: sheet.editing ? row.modelData.title : row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight }
                    Button { visible: !sheet.editing && !!row.modelData.editable; text: "↑"; enabled: !sheet.controller.busy; Accessible.name: "Переместить папку выше"; onClicked: sheet.controller.move(row.modelData.id, -1) }
                    Button { visible: !sheet.editing && !!row.modelData.editable; text: "↓"; enabled: !sheet.controller.busy; Accessible.name: "Переместить папку ниже"; onClicked: sheet.controller.move(row.modelData.id, 1) }
                    Button { visible: !sheet.editing && !!row.modelData.editable; text: "Изменить"; enabled: !sheet.controller.busy; onClicked: sheet.edit(row.modelData) }
                    Button { visible: !sheet.editing && !!row.modelData.editable; text: "Удалить"; enabled: !sheet.controller.busy; onClicked: { confirmDelete.folderId = row.modelData.id; confirmDelete.open() } }
                }
                function toggle() {
                    const peer = String(modelData.peer)
                    sheet.chosen = sheet.chosen.indexOf(peer) >= 0 ? sheet.chosen.filter(function(p) { return p !== peer }) : sheet.chosen.concat([peer])
                }
                onClicked: { if (sheet.editing) toggle(); else { sheet.controller.selected = modelData.id; sheet.close() } }
            }
        }
        Label { Layout.fillWidth: true; visible: sheet.controller.error.length > 0; text: sheet.controller.error; wrapMode: Text.Wrap; color: "#cf5353" }
        RowLayout {
            visible: sheet.editing
            Button { text: "Назад"; enabled: !sheet.controller.busy; onClicked: sheet.editing = false }
            Item { Layout.fillWidth: true }
            Button { text: sheet.controller.busy ? "Сохранение…" : "Сохранить"; enabled: !sheet.controller.busy && nameField.text.trim().length > 0; onClicked: { if (sheet.folderId.length) sheet.controller.update(sheet.folderId, nameField.text, sheet.chosen); else sheet.controller.create(nameField.text, sheet.chosen) } }
        }
        BusyIndicator { visible: sheet.controller.busy; running: visible; Layout.alignment: Qt.AlignHCenter }
    }
    Dialog {
        id: confirmDelete
        property string folderId: ""
        anchors.centerIn: parent
        title: "Удалить папку?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: "Все переписки останутся в мессенджере." }
        onAccepted: sheet.controller.remove(folderId)
    }
    Connections { target: sheet.controller; function onCompleted() { sheet.editing = false } }
}
