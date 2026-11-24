pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: overlay
    objectName: "messageExplosion"
    property bool reducedMotion: false
    property bool windowActive: true
    property color accentColor: "#71aaeb"
    property color surfaceColor: "#222222"
    readonly property int runningBursts: bursts.length
    property var bursts: []
    property int sequence: 0

    // Coordinates belong to this overlay. Keeping the grab result alive also
    // keeps its in-memory image available after the message delegate disappears.
    function burst(rect, grabResult) {
        if (reducedMotion || !windowActive || !visible || bursts.length >= 4)
            return false
        if (!rect || !isFinite(rect.x) || !isFinite(rect.y)
                || !isFinite(rect.width) || !isFinite(rect.height)
                || rect.width <= 0 || rect.height <= 0)
            return false
        const item = burstComponent.createObject(overlay, {
            "x": rect.x, "y": rect.y, "width": rect.width, "height": rect.height,
            "snapshot": grabResult || null, "seed": ++sequence,
            "tint": accentColor, "fill": surfaceColor
        })
        if (!item) return false
        bursts = bursts.concat([item])
        return true
    }
    function release(item) {
        bursts = bursts.filter(function(value) { return value !== item })
        item.destroy()
    }
    function clear() {
        const previous = bursts
        bursts = []
        for (let i = 0; i < previous.length; ++i) {
            previous[i].stop()
            previous[i].destroy()
        }
    }
    onReducedMotionChanged: if (reducedMotion) clear()
    onWindowActiveChanged: if (!windowActive) clear()
    onVisibleChanged: if (!visible) clear()

    Component {
        id: burstComponent
        Item {
            id: blast
            objectName: "messageExplosionBurst"
            property var snapshot: null
            property int seed: 0
            property color tint: "#71aaeb"
            property color fill: "#222222"
            property real age: 0
            readonly property real travel: 1 - Math.pow(1 - age, 3)
            readonly property int columns: Math.min(6, Math.max(3, Math.ceil(width / 48)))
            readonly property int rows: Math.min(4, Math.max(2, Math.ceil(height / 24)))
            function random(index) {
                const value = Math.sin((index + 1) * 12.9898 + seed * 78.233) * 43758.5453
                return value - Math.floor(value)
            }
            function stop() { lifetime.stop(); snapshot = null }

            Rectangle {
                x: -8; y: -6; width: blast.width + 16; height: blast.height + 12
                radius: Math.min(18, height / 2)
                color: blast.tint
                opacity: 0.16 * Math.max(0, 1 - blast.age * 5)
                scale: 1 + blast.age * 0.12
            }
            Repeater {
                model: blast.columns * blast.rows
                Rectangle {
                    id: shard
                    objectName: "messageExplosionShard"
                    required property int index
                    readonly property int column: index % blast.columns
                    readonly property int row: Math.floor(index / blast.columns)
                    readonly property real cellWidth: blast.width / blast.columns
                    readonly property real cellHeight: blast.height / blast.rows
                    readonly property real originX: column * cellWidth
                    readonly property real originY: row * cellHeight
                    readonly property real spreadX: (column + 0.5 - blast.columns / 2) * (14 + blast.random(index) * 15)
                    readonly property real spreadY: (row + 0.5 - blast.rows / 2) * 18 - 35 - blast.random(index + 30) * 45
                    x: originX + spreadX * blast.travel
                    y: originY + spreadY * blast.travel + 80 * blast.age * blast.age
                    width: Math.max(1, cellWidth - 0.6); height: Math.max(1, cellHeight - 0.6)
                    rotation: (blast.random(index + 60) - 0.5) * 105 * blast.travel
                    scale: 1 - blast.age * 0.38
                    opacity: Math.pow(1 - blast.age, 1.6)
                    color: blast.fill
                    border.width: 0.6
                    border.color: Qt.rgba(blast.tint.r, blast.tint.g, blast.tint.b, 0.6)
                    radius: 2; clip: true
                    Image {
                        x: -shard.originX; y: -shard.originY
                        width: blast.width; height: blast.height
                        source: blast.snapshot ? blast.snapshot.url : ""
                        visible: source.toString().length > 0
                        fillMode: Image.Stretch; smooth: true
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: blast.tint
                        opacity: blast.snapshot ? 0.09 : 0.22 + blast.random(shard.index + 90) * 0.22
                    }
                    Rectangle {
                        x: 2; y: 1; width: Math.max(0, parent.width - 4); height: 1
                        color: "#ffffff"; opacity: 0.45 * (1 - blast.age)
                    }
                }
            }
            Repeater {
                model: 8
                Rectangle {
                    objectName: "messageExplosionSpark"
                    required property int index
                    readonly property real angle: index * Math.PI / 4 + blast.random(index + 120) * 0.3
                    readonly property real distance: 35 + blast.random(index + 150) * 50
                    x: blast.width / 2 + Math.cos(angle) * (blast.width * 0.22 + distance * blast.travel)
                    y: blast.height / 2 + Math.sin(angle) * (blast.height * 0.25 + distance * blast.travel) + 25 * blast.age * blast.age
                    width: 3 + blast.random(index + 180) * 3; height: 2
                    radius: 1; rotation: angle * 180 / Math.PI
                    color: index % 3 === 0 ? "#ffffff" : blast.tint
                    opacity: 0.8 * Math.pow(1 - blast.age, 2.4)
                    scale: 1 - blast.age * 0.6
                }
            }
            NumberAnimation {
                id: lifetime
                target: blast; property: "age"; from: 0; to: 1
                running: true
                duration: 640; easing.type: Easing.Linear
                onFinished: overlay.release(blast)
            }
        }
    }
}
