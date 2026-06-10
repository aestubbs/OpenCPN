import QtQuick

// A compact compass dial: tick ring + cardinal letters + needle.
Item {
    id: dial
    property double angle: -1   // degrees true; -1 = unavailable
    property string label: ""
    width: 64; height: 64

    Canvas {
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            const c = width / 2
            ctx.reset()
            ctx.strokeStyle = "#5a6878"
            ctx.lineWidth = 1
            for (let d = 0; d < 360; d += 30) {
                const a = d * Math.PI / 180
                const r0 = d % 90 === 0 ? c - 11 : c - 7
                ctx.beginPath()
                ctx.moveTo(c + Math.sin(a) * r0, c - Math.cos(a) * r0)
                ctx.lineTo(c + Math.sin(a) * (c - 3), c - Math.cos(a) * (c - 3))
                ctx.stroke()
            }
            ctx.fillStyle = "#8a98a8"
            ctx.font = "7px sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            const r = c - 17
            ctx.fillText("N", c, c - r)
            ctx.fillText("E", c + r, c)
            ctx.fillText("S", c, c + r)
            ctx.fillText("W", c - r, c)
        }
    }
    Rectangle {  // needle
        visible: dial.angle >= 0
        width: 2; height: parent.height / 2 - 8
        radius: 1
        color: "#ff5a48"
        x: parent.width / 2 - 1
        y: 8
        transformOrigin: Item.Bottom
        rotation: dial.angle
        antialiasing: true
        Behavior on rotation {
            RotationAnimation { duration: 350; direction: RotationAnimation.Shortest }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        text: dial.label
        color: "#e8f0ff"; font.pointSize: 8; font.bold: true
    }
}
