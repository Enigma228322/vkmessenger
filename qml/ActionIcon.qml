import QtQuick

Canvas {
    id: icon
    property string kind: ""
    property color stroke: "#939393"
    implicitWidth: 24
    implicitHeight: 24
    onKindChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(width / 24, height / 24)
        c.strokeStyle = stroke
        c.lineWidth = 1.7
        c.lineCap = "round"
        c.lineJoin = "round"
        function path(points, close) {
            c.beginPath(); c.moveTo(points[0][0], points[0][1])
            for (let i = 1; i < points.length; ++i) c.lineTo(points[i][0], points[i][1])
            if (close) c.closePath()
            c.stroke()
        }
        if (kind === "reply" || kind === "forward") {
            if (kind === "forward") { c.translate(24, 0); c.scale(-1, 1) }
            path([[10,5],[3,11],[10,17],[10,13]])
            c.beginPath(); c.moveTo(3,11); c.bezierCurveTo(13,9,19,10,21,19); c.lineTo(21,14); c.bezierCurveTo(20,7,15,7,10,8); c.lineTo(10,5); c.stroke()
        } else if (kind === "important") {
            const points = []
            for (let i = 0; i < 10; ++i) { const a = -Math.PI/2 + i*Math.PI/5, r = i%2 ? 4.5 : 9; points.push([12+Math.cos(a)*r,12+Math.sin(a)*r]) }
            path(points, true)
        } else if (kind === "copy") {
            c.strokeRect(8,7,12,14); path([[16,3],[4,3],[4,17]])
        } else if (kind === "edit") {
            path([[4,16],[16,4],[20,8],[8,20],[3,21]],true); path([[13,7],[17,11]])
        } else if (kind === "delete" || kind === "cancel") {
            path([[4,6],[20,6]]); path([[7,6],[8,21],[16,21],[17,6]]); path([[9,3],[15,3],[16,6]]); path([[10,10],[10,17]]); path([[14,10],[14,17]])
        } else if (kind === "select") {
            c.beginPath(); c.arc(12,12,9,0,Math.PI*2); c.stroke(); path([[8,12],[11,15],[16,9]])
        } else if (kind === "pin") {
            path([[8,3],[16,3],[15,10],[19,14],[5,14],[9,10]],true); path([[12,14],[12,22]])
        } else if (kind === "link") {
            c.save(); c.translate(12,12); c.rotate(-Math.PI/4); c.strokeRect(-4,-10,8,9); c.strokeRect(-4,1,8,9); path([[0,-4],[0,4]]); c.restore()
        } else if (kind === "search") {
            c.beginPath(); c.arc(10,10,6,0,Math.PI*2); c.stroke(); path([[15,15],[21,21]])
        } else if (kind === "info") {
            c.beginPath(); c.arc(12,12,9,0,Math.PI*2); c.stroke(); path([[12,10],[12,17]]); path([[12,6],[12,6.5]])
        } else if (kind === "retry") {
            c.beginPath(); c.arc(12,12,8,0.4,5.3); c.stroke(); path([[12,3],[17,5],[17,0]])
        } else {
            c.beginPath(); c.arc(12,8,4,0,Math.PI*2); c.stroke(); c.beginPath(); c.arc(12,22,8,Math.PI,2*Math.PI); c.stroke()
        }
    }
}
