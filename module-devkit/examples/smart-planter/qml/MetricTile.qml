import QtQuick
Rectangle {
    id: tile
    property var metric: ({})
    height: 155; radius: 18; color: metric.tint || "#dbeadc"
    Column {
        x: 13; y: 12; width: parent.width - 26; spacing: 7
        Row {
            spacing: 8
            Canvas {
                width: 25; height: 26
                onPaint: {
                    const c = getContext("2d"); c.reset(); c.strokeStyle = "#365f48"; c.fillStyle = "#365f48"; c.lineWidth = 2
                    if (tile.metric.kind === "temperature") {
                        c.strokeRect(10, 2, 6, 15); c.beginPath(); c.arc(13, 19, 5, 0, Math.PI * 2); c.fill()
                    } else if (tile.metric.kind === "humidity") {
                        c.beginPath(); c.moveTo(13, 2); c.bezierCurveTo(2, 14, 2, 24, 13, 24); c.bezierCurveTo(24, 24, 24, 14, 13, 2); c.stroke()
                    } else if (tile.metric.kind === "light") {
                        c.beginPath(); c.arc(13, 13, 6, 0, Math.PI * 2); c.stroke()
                        for (let i = 0; i < 8; i++) { const a = i * Math.PI / 4; c.beginPath(); c.moveTo(13 + Math.cos(a) * 9, 13 + Math.sin(a) * 9); c.lineTo(13 + Math.cos(a) * 12, 13 + Math.sin(a) * 12); c.stroke() }
                    } else {
                        c.beginPath(); c.arc(13, 14, 10, Math.PI, Math.PI * 2); c.stroke(); c.beginPath(); c.moveTo(13, 14); c.lineTo(19, 7); c.stroke(); c.fillRect(3, 20, 20, 2)
                    }
                }
            }
            Text { text: tile.metric.label || ""; font.pixelSize: 14; color: "#365f48"; height: 26; verticalAlignment: Text.AlignVCenter }
        }
        Text { text: tile.metric.value || "--"; font.pixelSize: 28; font.bold: true; color: "#153d2b"; width: parent.width; elide: Text.ElideRight }
        Text { text: tile.metric.unit || ""; color: "#365f48"; font.pixelSize: 13 }
        Text { text: tile.metric.note || ""; width: parent.width; wrapMode: Text.Wrap; color: "#4d6755"; font.pixelSize: 12 }
    }
}
