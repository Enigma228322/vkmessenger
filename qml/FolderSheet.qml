import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: sheet
    objectName: "folderSheet"
    modal: true; anchors.centerIn: parent
    width: Math.min(620, parent.width - 48); height: Math.min(650, parent.height - 48); padding: 24
    title: editing ? (folderId.length ? "Изменить папку" : "Новая папка") : "Папки диалогов"
    property bool reducedMotion: false
    property bool editing: false
    property string folderId: ""
    property var chosen: []
    property string appliedQuery: ""
    readonly property var controller: messenger.folders
    Theme { id: design; dark: messenger.darkTheme }
    component FolderButton: Button {
        id: button
        property bool primary: false
        implicitHeight: 40; leftPadding: 14; rightPadding: 14
        contentItem: Text { text: button.text; font: button.font; color: !button.enabled ? design.muted : button.primary ? "white" : design.ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 8; color: !button.enabled ? design.secondary : button.primary ? design.accent : button.hovered ? design.hover : design.secondary; border.color: button.visualFocus ? design.accent : "transparent" }
    }
    Binding { target: messenger.folderChoices; property: "query"; value: sheet.visible && sheet.editing ? sheet.appliedQuery : "" }
    onClosed: { searchDelay.stop(); messenger.folderChoices.savedPeers = []; appliedQuery = ""; chosen = []; folderId = "" }
    background: Rectangle { radius: 18; color: sheet.palette.window; border.color: design.divider }
    header: Label { text: sheet.title; color: design.ink; font.pixelSize: 18; font.weight: Font.DemiBold; leftPadding: 24; rightPadding: 24; topPadding: 20; bottomPadding: 8 }
    footer: Item {
        implicitHeight: 68
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.topMargin: 8; spacing: 10
            FolderButton { text: "Назад"; visible: sheet.editing; enabled: !sheet.controller.busy; onClicked: sheet.leaveEditor() }
            Item { Layout.fillWidth: true }
            FolderButton { text: "Закрыть"; onClicked: sheet.close() }
            FolderButton {
                objectName: "saveFolder"
                primary: true
                visible: sheet.editing
                text: sheet.controller.busy ? "Сохранение…" : "Сохранить"
                enabled: !sheet.controller.busy && !messenger.offline && !messenger.demo && messenger.connected && nameField.text.trim().length > 0
                onClicked: sheet.save()
            }
        }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: sheet.reducedMotion ? 0 : 240; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: sheet.reducedMotion ? 0 : 180; easing.type: Easing.OutCubic } }
    function show() { leaveEditor(); open(); controller.refresh() }
    function leaveEditor() { editing = false; searchDelay.stop(); appliedQuery = ""; messenger.folderChoices.savedPeers = []; results.currentIndex = -1 }
    function edit(folder) {
        folderId = folder ? String(folder.id) : ""
        chosen = folder ? folder.peers.slice() : []
        nameField.text = folder ? folder.name : ""
        query.text = ""; appliedQuery = ""; searchDelay.stop()
        messenger.folderChoices.savedPeers = chosen
        editing = true; results.currentIndex = -1; nameField.forceActiveFocus()
    }
    function toggle(peer) {
        const id = String(peer)
        chosen = chosen.indexOf(id) >= 0 ? chosen.filter(function(p) { return p !== id }) : chosen.concat([id])
    }
    function activate(row) {
        if (!row) return
        if (editing) toggle(row.peer)
        else { controller.selected = row.id; close() }
    }
    function activateCurrent() {
        if (results.currentIndex < 0 || results.currentIndex >= results.count) return
        activate(editing ? messenger.folderChoices.get(results.currentIndex) : controller.items[results.currentIndex])
    }
    function applySearch() { searchDelay.stop(); if (!query.inputMethodComposing) { appliedQuery = query.text.trim(); results.currentIndex = -1 } }
    function focusResults(direction) {
        if (query.inputMethodComposing) return
        applySearch()
        if (!results.count) return
        results.currentIndex = direction > 0 ? 0 : results.count - 1
        results.forceActiveFocus(); results.positionViewAtIndex(results.currentIndex, ListView.Contain)
    }
    function save() {
        if (folderId.length) controller.update(folderId, nameField.text, chosen)
        else controller.create(nameField.text, chosen)
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { Layout.fillWidth: true; visible: !sheet.editing; text: "Папки синхронизируются с VK. Их удаление сохраняет переписки."; wrapMode: Text.Wrap; color: design.muted }
        RowLayout {
            visible: !sheet.editing
            Button { text: "Создать папку"; enabled: !sheet.controller.busy && !messenger.offline && !messenger.demo; onClicked: sheet.edit(null) }
            Button { text: "Обновить"; enabled: !sheet.controller.busy; onClicked: sheet.controller.refresh() }
        }
        TextField {
            id: nameField; objectName: "folderName"; visible: sheet.editing; Layout.fillWidth: true; implicitHeight: 40
            maximumLength: 16; placeholderText: "Название папки"; Accessible.name: placeholderText; color: design.ink; placeholderTextColor: design.muted
            background: Rectangle { radius: 10; color: design.secondary; border.color: nameField.activeFocus ? design.accent : design.divider }
            onAccepted: if (!inputMethodComposing) query.forceActiveFocus()
        }
        TextField {
            id: query
            objectName: "folderSearch"
            visible: sheet.editing; Layout.fillWidth: true; implicitHeight: 40
            placeholderText: "Найти переписку"; Accessible.name: placeholderText
            leftPadding: 36; rightPadding: 38; color: design.ink; placeholderTextColor: design.muted
            background: Rectangle { radius: 10; color: design.secondary; border.color: query.activeFocus ? design.accent : design.divider }
            onTextChanged: { searchDelay.stop(); if (!text.length) sheet.applySearch(); else if (!inputMethodComposing) searchDelay.restart() }
            onInputMethodComposingChanged: { searchDelay.stop(); if (!inputMethodComposing) searchDelay.restart() }
            onAccepted: sheet.focusResults(1)
            Keys.onDownPressed: function(event) { event.accepted = !inputMethodComposing; if (event.accepted) sheet.focusResults(1) }
            Keys.onUpPressed: function(event) { event.accepted = !inputMethodComposing; if (event.accepted) sheet.focusResults(-1) }
            Keys.onEscapePressed: function(event) { event.accepted = !inputMethodComposing && text.length > 0; if (event.accepted) clear() }
            ActionIcon { x: 11; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; kind: "search"; stroke: design.muted }
            ToolButton {
                objectName: "clearFolderSearch"
                anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; width: 28; height: 28
                visible: query.text.length > 0; focusPolicy: Qt.NoFocus; Accessible.name: "Очистить поиск переписок"
                contentItem: ActionIcon { kind: "close"; stroke: design.muted }
                onClicked: { query.clear(); query.forceActiveFocus() }
            }
        }
        RowLayout {
            Layout.fillWidth: true; visible: sheet.editing
            Label { objectName: "folderSelectionCount"; Layout.fillWidth: true; text: "Выбрано переписок: " + sheet.chosen.length; color: design.muted }
            ToolButton { objectName: "clearFolderSelection"; visible: sheet.chosen.length > 0; text: "Сбросить"; onClicked: sheet.chosen = [] }
        }
        ListView {
            id: results
            objectName: "folderResults"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
            model: !sheet.visible ? null : sheet.editing ? messenger.folderChoices : sheet.controller.items
            activeFocusOnTab: true; keyNavigationEnabled: true; currentIndex: -1
            Keys.onReturnPressed: sheet.activateCurrent()
            Keys.onEnterPressed: sheet.activateCurrent()
            Keys.onSpacePressed: if (sheet.editing) sheet.activateCurrent()
            Keys.onEscapePressed: function(event) { event.accepted = sheet.editing; if (event.accepted) query.forceActiveFocus() }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home && count) { currentIndex = 0; positionViewAtBeginning(); event.accepted = true }
                else if (event.key === Qt.Key_End && count) { currentIndex = count - 1; positionViewAtEnd(); event.accepted = true }
            }
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                objectName: "folderResultRow"
                required property var modelData
                width: ListView.view.width; height: 58; padding: 8
                highlighted: sheet.editing && sheet.chosen.indexOf(String(modelData.peer)) >= 0
                Accessible.role: Accessible.ListItem
                Accessible.name: String((sheet.editing ? modelData.title : modelData.name) || "Переписка") + (highlighted ? ", выбрано" : "")
                Accessible.selected: highlighted
                Accessible.onPressAction: sheet.activate(modelData)
                background: Rectangle { radius: 10; color: row.highlighted ? design.selected : row.hovered ? design.hover : "transparent"; border.color: row.ListView.isCurrentItem && results.activeFocus ? design.accent : "transparent" }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        visible: sheet.editing; Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 18; color: design.secondary
                        Text { anchors.centerIn: parent; text: String(row.modelData.title || "?").substring(0, 1).toUpperCase(); color: design.accent; font.pixelSize: 15 }
                        CachedAvatar { anchors.fill: parent; remoteSource: row.modelData.avatar || "" }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 3
                        Label { Layout.fillWidth: true; text: (sheet.editing ? row.modelData.title : row.modelData.name) || ""; textFormat: Text.PlainText; elide: Text.ElideRight; color: design.ink; font.weight: Font.DemiBold }
                        Label { Layout.fillWidth: true; text: sheet.editing ? row.modelData.missing ? "Сохранена в папке" : row.modelData.preview || "" : row.modelData.editable ? "Переписок: " + (row.modelData.peers || []).length : "Системная папка"; visible: text.length > 0; elide: Text.ElideRight; textFormat: Text.PlainText; font.pixelSize: 11; color: design.muted }
                    }
                    ActionIcon { Layout.preferredWidth: 22; Layout.preferredHeight: 22; visible: sheet.editing; kind: row.highlighted ? "select" : "add"; stroke: row.highlighted ? design.accent : design.muted }
                    Loader {
                        active: !sheet.editing && !!row.modelData.editable; visible: active
                        sourceComponent: RowLayout {
                            spacing: 2
                            ToolButton { text: "↑"; enabled: !sheet.controller.busy; Accessible.name: "Переместить папку выше"; onClicked: sheet.controller.move(row.modelData.id, -1) }
                            ToolButton { text: "↓"; enabled: !sheet.controller.busy; Accessible.name: "Переместить папку ниже"; onClicked: sheet.controller.move(row.modelData.id, 1) }
                            Button { text: "Изменить"; enabled: !sheet.controller.busy; onClicked: sheet.edit(row.modelData) }
                            ToolButton { text: "×"; enabled: !sheet.controller.busy; Accessible.name: "Удалить папку"; onClicked: { confirmDelete.folderId = row.modelData.id; confirmDelete.open() } }
                        }
                    }
                }
                onClicked: sheet.activate(modelData)
            }
            Label { objectName: "folderEmptyState"; anchors.centerIn: parent; width: Math.max(1, parent.width - 24); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; visible: !results.count && !sheet.controller.busy; text: sheet.editing ? sheet.appliedQuery.length ? "Ничего не найдено" : "Пока нет переписок" : "Папок пока нет"; color: design.muted }
        }
        Label { Layout.fillWidth: true; visible: sheet.controller.error.length > 0; text: sheet.controller.error; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: design.errorText }
        BusyIndicator { visible: sheet.controller.busy; running: visible; Layout.alignment: Qt.AlignHCenter }
    }
    Timer { id: searchDelay; interval: 150; onTriggered: sheet.applySearch() }
    Dialog {
        id: confirmDelete
        property string folderId: ""
        anchors.centerIn: parent; title: "Удалить папку?"; modal: true; standardButtons: Dialog.Yes | Dialog.No
        Label { text: "Все переписки останутся в мессенджере." }
        onAccepted: sheet.controller.remove(folderId)
    }
    Connections { target: sheet.controller; function onCompleted() { sheet.leaveEditor() } }
    Connections { target: messenger; function onChanged() { if (!messenger.connected && sheet.visible) sheet.close() } }
}
