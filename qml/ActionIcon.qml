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
        } else if (kind === "notifications" || kind === "muted") {
            c.beginPath(); c.moveTo(5,17); c.lineTo(7,14); c.lineTo(7,9); c.bezierCurveTo(7,2,17,2,17,9); c.lineTo(17,14); c.lineTo(19,17); c.closePath(); c.stroke()
            c.beginPath(); c.arc(12,19,2,0,Math.PI); c.stroke()
            if (kind === "muted") path([[3,3],[21,21]])
        } else if (kind === "more") {
            for (let x = 5; x <= 19; x += 7) { c.beginPath(); c.arc(x,12,1,0,Math.PI*2); c.stroke() }
        } else if (kind === "settings") {
            c.beginPath(); c.arc(12,12,4,0,Math.PI*2); c.stroke()
            const points = []
            for (let i = 0; i < 32; ++i) { const a = i*Math.PI/16, r = i%4 < 2 ? 10 : 8; points.push([12+Math.cos(a)*r,12+Math.sin(a)*r]) }
            path(points, true)
        } else if (kind === "archive" || kind === "folder") {
            if (kind === "archive") { c.strokeRect(3,4,18,4); path([[5,8],[5,21],[19,21],[19,8]]); path([[10,12],[14,12]]) }
            else path([[3,6],[10,6],[12,9],[21,9],[21,20],[3,20]],true)
        } else if (kind === "add") {
            path([[12,4],[12,20]]); path([[4,12],[20,12]])
        } else if (kind === "people") {
            c.beginPath(); c.arc(9,8,3.5,0,Math.PI*2); c.stroke()
            c.beginPath(); c.arc(9,21,7,Math.PI,2*Math.PI); c.stroke()
            c.beginPath(); c.arc(17,8,3, -Math.PI/2,Math.PI/2); c.stroke()
            c.beginPath(); c.arc(17,21,5, -Math.PI/2,0); c.stroke()
        } else if (kind === "photo") {
            c.strokeRect(3,3,18,18); path([[3,17],[8,11],[12,15],[16,10],[21,16]])
            c.beginPath(); c.arc(8,7,1,0,Math.PI*2); c.stroke()
        } else if (kind === "document") {
            path([[5,2],[14,2],[19,7],[19,22],[5,22]],true); path([[14,2],[14,7],[19,7]]); path([[8,12],[16,12]]); path([[8,16],[14,16]])
        } else if (kind === "video") {
            c.strokeRect(3,4,18,16); path([[10,8],[16,12],[10,16]],true)
        } else if (kind === "microphone") {
            c.beginPath(); c.arc(12,5,3,Math.PI,2*Math.PI); c.lineTo(15,12); c.arc(12,12,3,0,Math.PI); c.closePath(); c.stroke()
            c.beginPath(); c.arc(12,12,7,0,Math.PI); c.stroke(); path([[5,12],[5,10]]); path([[19,12],[19,10]]); path([[12,19],[12,22]])
        } else if (kind === "smile") {
            c.beginPath(); c.arc(12,12,9,0,Math.PI*2); c.stroke(); path([[8,8],[8,9]]); path([[16,8],[16,9]])
            c.beginPath(); c.arc(12,12,5,0.3,Math.PI-0.3); c.stroke()
        } else if (kind === "leave") {
            path([[10,3],[3,3],[3,21],[10,21]]); path([[8,12],[21,12],[17,8]]); path([[21,12],[17,16]])
        } else if (kind === "chat") {
            c.beginPath(); c.moveTo(12,3); c.bezierCurveTo(1,3,0,17,8,19); c.lineTo(4,22); c.lineTo(11,20); c.bezierCurveTo(25,20,25,3,12,3); c.stroke()
        } else if (kind === "send") {
            path([[12,3],[4,12],[9,12],[9,21],[15,21],[15,12],[20,12]],true)
        } else if (kind === "play") {
            path([[8,4],[20,12],[8,20]],true)
        } else if (kind === "pause") {
            path([[8,5],[8,19]]); path([[16,5],[16,19]])
        } else if (kind === "chevronLeft" || kind === "chevronRight") {
            if (kind === "chevronRight") { c.translate(24,0); c.scale(-1,1) }
            path([[15,5],[8,12],[15,19]])
        } else if (kind === "download") {
            path([[12,3],[12,16],[7,11]]); path([[12,16],[17,11]]); path([[4,17],[4,21],[20,21],[20,17]])
        } else if (kind === "expand" || kind === "collapse") {
            if (kind === "expand") { path([[9,3],[3,3],[3,9]]); path([[15,3],[21,3],[21,9]]); path([[3,15],[3,21],[9,21]]); path([[15,21],[21,21],[21,15]]) }
            else { path([[3,9],[9,9],[9,3]]); path([[15,3],[15,9],[21,9]]); path([[3,15],[9,15],[9,21]]); path([[15,21],[15,15],[21,15]]) }
        } else if (kind === "volume" || kind === "volumeOff") {
            path([[3,9],[7,9],[12,5],[12,19],[7,15],[3,15]],true)
            if (kind === "volumeOff") { path([[16,9],[22,15]]); path([[22,9],[16,15]]) }
            else { c.beginPath(); c.arc(12,12,6,-0.8,0.8); c.stroke(); c.beginPath(); c.arc(12,12,10,-0.7,0.7); c.stroke() }
        } else if (kind === "zoomIn" || kind === "zoomOut") {
            c.beginPath(); c.arc(10,10,6,0,Math.PI*2); c.stroke(); path([[15,15],[21,21]]); path([[7,10],[13,10]])
            if (kind === "zoomIn") path([[10,7],[10,13]])
        } else if (kind === "fit") {
            c.strokeRect(3,5,18,14); path([[7,12],[17,12]]); path([[9,10],[7,12],[9,14]]); path([[15,10],[17,12],[15,14]])
        } else if (kind === "skipBack" || kind === "skipForward") {
            if (kind === "skipForward") { c.translate(24,0); c.scale(-1,1) }
            c.beginPath(); c.arc(12,13,8,-2.1,2.8); c.stroke(); path([[4,3],[4,8],[9,8]])
        } else if (kind === "close") {
            path([[6,6],[18,18]]); path([[18,6],[6,18]])
        } else if (kind === "chevronDown") {
            path([[5,9],[12,16],[19,9]])
        } else if (kind === "menu") {
            path([[4,6],[20,6]]); path([[4,12],[20,12]]); path([[4,18],[20,18]])
        } else if (kind === "retry") {
            c.beginPath(); c.arc(12,12,8,0.4,5.3); c.stroke(); path([[12,3],[17,5],[17,0]])
        } else {
            c.beginPath(); c.arc(12,8,4,0,Math.PI*2); c.stroke(); c.beginPath(); c.arc(12,22,8,Math.PI,2*Math.PI); c.stroke()
        }
    }
}
