import QtQuick

Item {
    id: backdrop
    required property var theme
    // The wallpaper is painted only on resize/theme changes. Its soft gradients
    // show through the glass without capturing and blurring the live chat.
    Rectangle { anchors.fill: parent; color: backdrop.theme.canvas }
    Canvas {
        id: light
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections { target: backdrop.theme; function onDarkChanged() { light.requestPaint() } }
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            function glow(x, y, radius, color) {
                const gradient = ctx.createRadialGradient(x, y, 0, x, y, radius)
                gradient.addColorStop(0, color)
                gradient.addColorStop(1, "transparent")
                ctx.fillStyle = gradient
                ctx.fillRect(0, 0, width, height)
            }
            glow(width * 0.12, height * 0.05, width * 0.72, backdrop.theme.ambientBlue)
            glow(width * 0.94, height * 0.32, width * 0.52, backdrop.theme.ambientViolet)
            glow(width * 0.46, height * 1.06, width * 0.62, backdrop.theme.ambientTeal)
        }
    }
}
