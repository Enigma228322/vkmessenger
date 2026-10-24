import QtQuick

Canvas {
    id: root
    objectName: "readIndicator"
    property bool read: false
    property color ink: read ? "#438bc7" : "#8b9aab"
    implicitWidth: 18
    implicitHeight: 14
    onReadChanged: requestPaint()
    onInkChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = ink
        ctx.lineWidth = 1.5
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()
        ctx.moveTo(1.5, 7); ctx.lineTo(5, 10.5); ctx.lineTo(11, 3.5)
        if (read) { ctx.moveTo(8, 9); ctx.lineTo(9.5, 10.5); ctx.lineTo(16, 3.5) }
        ctx.stroke()
    }
}
