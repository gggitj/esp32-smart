import QtQuick

// Original botanical illustration, not a synthetic sensor indicator.
Item {
    Canvas {
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset(); c.scale(width / 200, height / 190)
            c.fillStyle = "#285745"; c.beginPath(); c.arc(98, 91, 77, 0, Math.PI * 2); c.fill()
            c.strokeStyle = "#a8d4a0"; c.lineWidth = 5; c.lineCap = "round"
            c.beginPath(); c.moveTo(100, 131); c.bezierCurveTo(95, 98, 110, 76, 101, 42); c.stroke()
            c.fillStyle = "#8dc484"
            c.beginPath(); c.moveTo(101, 78); c.bezierCurveTo(65, 83, 50, 58, 52, 43); c.bezierCurveTo(83, 40, 101, 58, 101, 78); c.fill()
            c.fillStyle = "#c5dfa4"
            c.beginPath(); c.moveTo(101, 100); c.bezierCurveTo(105, 61, 133, 58, 151, 62); c.bezierCurveTo(151, 88, 131, 106, 101, 100); c.fill()
            c.fillStyle = "#a7d58c"
            c.beginPath(); c.moveTo(102, 51); c.bezierCurveTo(83, 40, 88, 19, 104, 14); c.bezierCurveTo(122, 34, 116, 43, 102, 51); c.fill()
            c.fillStyle = "#dfb289"; c.beginPath(); c.moveTo(65, 128); c.lineTo(136, 128); c.lineTo(126, 172); c.lineTo(76, 172); c.closePath(); c.fill()
            c.fillStyle = "#f1cfaa"; c.fillRect(61, 122, 79, 13)
            c.fillStyle = "#e3ca85"; c.beginPath(); c.arc(160, 26, 12, 0, Math.PI * 2); c.fill()
            c.strokeStyle = "#71976f"; c.lineWidth = 2; c.beginPath(); c.moveTo(52, 176); c.lineTo(149, 176); c.stroke()
        }
    }
}
