import QtQuick
Canvas {
    id: chart
    property var points: []
    property bool weekView: false
    property double endTime: NaN
    onPointsChanged: requestPaint()
    onWeekViewChanged: requestPaint()
    onEndTimeChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d"); c.reset()
        const left = 42, right = width - 16, top = 16, bottom = height - 28
        const span = (weekView ? 7 : 1) * 86400000, step = (weekView ? 120 : 15) * 60000
        const end = isFinite(endTime) ? endTime : Date.now(), start = end - span
        let lo = Infinity, hi = -Infinity
        for (let i = 0; i < points.length; i++) { lo = Math.min(lo, Number(points[i].min)); hi = Math.max(hi, Number(points[i].max)) }
        if (!isFinite(lo)) { lo = 15; hi = 35 }
        const pad = Math.max(1, (hi - lo) * 0.15); lo -= pad; hi += pad
        function px(t) { return left + Math.max(0, Math.min(1, (t - start) / span)) * (right - left) }
        function py(v) { return bottom - (v - lo) / (hi - lo) * (bottom - top) }
        c.font = "11px sans-serif"; c.lineWidth = 1
        for (let j = 0; j <= 3; j++) {
            const y = top + (bottom - top) * j / 3
            c.strokeStyle = "#e5ede5"; c.beginPath(); c.moveTo(left, y); c.lineTo(right, y); c.stroke()
            c.fillStyle = "#657c69"; c.fillText((hi - (hi - lo) * j / 3).toFixed(1) + "°", 0, y + 4)
        }
        for (let j = 0; j <= 4; j++) {
            const t = start + span * j / 4, d = new Date(t)
            const label = weekView ? ((d.getMonth() + 1) + "/" + d.getDate()) : (String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0"))
            c.fillStyle = "#657c69"; c.fillText(label, Math.min(width - 40, Math.max(left, px(t) - 14)), height - 7)
        }
        c.save(); c.beginPath(); c.rect(left, top, right - left, bottom - top); c.clip()
        for (let i = 0; i < points.length; i++) {
            const p = points[i], x = px(Number(p.t)), y = py(Number(p.v))
            c.strokeStyle = "#b9d8bf"; c.lineWidth = 5; c.beginPath(); c.moveTo(x, py(Number(p.min))); c.lineTo(x, py(Number(p.max))); c.stroke()
            if (i > 0 && Number(p.t) - Number(points[i - 1].t) <= step * 1.5) {
                c.strokeStyle = "#2d7b50"; c.lineWidth = 2.5; c.beginPath(); c.moveTo(px(Number(points[i - 1].t)), py(Number(points[i - 1].v))); c.lineTo(x, y); c.stroke()
            }
            c.fillStyle = "#2d7b50"; c.beginPath(); c.arc(x, y, 3, 0, Math.PI * 2); c.fill()
        }
        c.restore()
        if (!points.length) { c.fillStyle = "#657c69"; c.font = "15px sans-serif"; c.fillText("开始积累真实温度，缺测时段保持空白", left + 12, (top + bottom) / 2) }
    }
}
