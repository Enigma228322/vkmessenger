import QtQuick

Rectangle {
    id: badge
    objectName: "scrollDateBadge"
    required property var view
    required property var sourceModel
    required property var theme
    property var scrollBar: null
    property string peer: ""
    property bool reducedMotion: false
    property string dateText: ""
    property bool engaged: false
    width: Math.min(dateLabel.implicitWidth + 28, parent ? Math.max(1, parent.width - 32) : 200)
    height: 32
    radius: 12
    color: theme.secondary
    border.color: theme.divider
    opacity: engaged && dateText.length > 0 ? 1 : 0
    visible: opacity > 0 && dateText.length > 0
    z: 20
    Behavior on opacity {
        enabled: !badge.reducedMotion
        NumberAnimation { duration: badge.engaged ? 100 : 360; easing.type: Easing.OutCubic }
    }
    Text {
        id: dateLabel
        anchors.centerIn: parent
        width: parent.width - 28
        text: badge.dateText
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: theme.ink
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    function currentDate() {
        if (!view || !sourceModel || !view.count) return ""
        let index = -1
        const offsets = [1, 8, 16, 32, 56, 80]
        for (const offset of offsets) {
            index = view.indexAt(view.contentX + view.width / 2, view.contentY + Math.min(offset, view.height - 1))
            if (index >= 0) break
        }
        if (index < 0) return ""
        // Inspect only nearby rows; scrolling never scans the full history.
        for (const offset of [0, 1, 2, 3, -1, -2, -3]) {
            if (index + offset < 0 || index + offset >= view.count) continue
            const delegate = view.itemAtIndex(index + offset)
            const row = delegate ? delegate.item : sourceModel.get(index + offset)
            if (row.gap) continue
            const timestamp = Number(row.sentAt || (row.raw ? row.raw.date : 0))
            if (!timestamp || !isFinite(timestamp)) continue
            const date = new Date(timestamp * 1000)
            if (isNaN(date.getTime())) continue
            const months = ["января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря"]
            return date.getDate() + " " + months[date.getMonth()] + (date.getFullYear() === new Date().getFullYear() ? "" : " " + date.getFullYear())
        }
        return ""
    }
    function reveal() {
        const value = currentDate()
        if (!value.length) return
        dateText = value
        engaged = true
        hideDelay.restart()
    }
    function dismiss() {
        hideDelay.stop()
        engaged = false
        dateText = ""
    }
    onPeerChanged: dismiss()
    Timer { id: hideDelay; interval: 2000; onTriggered: badge.engaged = false }
    WheelHandler {
        parent: badge.view
        target: null
        blocking: false
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) { badge.reveal(); event.accepted = false }
    }
    Connections {
        target: badge.view
        function onMovementStarted() { badge.reveal() }
        function onMovementEnded() { if (badge.engaged) hideDelay.restart() }
        function onContentYChanged() {
            if (badge.view.moving || badge.view.dragging || (badge.scrollBar && badge.scrollBar.pressed)) badge.reveal()
            else if (badge.engaged) {
                const value = badge.currentDate()
                if (value.length) badge.dateText = value
            }
        }
    }
    Connections {
        target: badge.scrollBar
        function onPressedChanged() {
            if (badge.scrollBar.pressed) badge.reveal()
            else if (badge.engaged) hideDelay.restart()
        }
    }
    Connections {
        target: badge.sourceModel
        function onModelAboutToBeReset() { badge.dismiss() }
    }
}
