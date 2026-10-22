import QtQuick
import QtQuick.Controls

/*
 * Environment module dashboard.
 *
 * Shows the live reading from the ESP32 module, a rolling temperature chart
 * (indoor vs. outdoor), and a light-level assessment.
 *
 * Weather comes from Open-Meteo (no API key). Set WEATHER_LAT / WEATHER_LON
 * below to the installation site. If the Pi has no internet the outdoor
 * values stay at "--" and the chart simply shows the indoor line.
 */
Rectangle {
    id: page

    property var sessionModel: null
    property real hostUiScale: 1
    property bool diagnostics: false

    readonly property real uiScale: hostUiScale
    readonly property real gap: 14 * uiScale
    readonly property var latest: sessionModel ? sessionModel.data : ({})

    // --- site coordinates (edit these) -------------------------------------
    property real weatherLat: 31.2304
    property real weatherLon: 121.4737
    property string weatherPlace: "上海"
    // -----------------------------------------------------------------------

    // Indoor history: [{t: ms, v: celsius}]
    property var indoor: []
    // Outdoor history: [{t: ms, v: celsius}]
    property var outdoor: []
    readonly property int maxPoints: 400
    readonly property int windowMs: 10 * 60 * 1000   // 10 minutes
    /* The visible time span adapts to how much history exists, so the first
     * minute of data is not squeezed into the right-hand edge. */
    readonly property real chartSpanMs: {
        if (indoor.length < 2) return 20000
        const span = indoor[indoor.length - 1].t - indoor[0].t
        return Math.max(20000, Math.min(windowMs, span * 1.1))
    }
    readonly property string chartCaption: {
        const s = Math.round(chartSpanMs / 1000)
        return s >= 120 ? ("最近 " + Math.round(s / 60) + " 分钟温度")
                        : ("最近 " + s + " 秒温度")
    }

    property real outdoorTemp: NaN
    property real outdoorHigh: NaN
    property real outdoorLow: NaN
    property string outdoorText: "室外 --"
    property string weatherInfo: ""
    property bool weatherOk: false
    property bool weatherBusy: false

    implicitHeight: body.y + body.height + gap
    color: "#071522"
    radius: 14 * uiScale

    function num(v) {
        const n = Number(v)
        return isFinite(n) ? n : NaN
    }

    function fmt(v, digits) {
        return isFinite(v) ? v.toFixed(digits) : "--"
    }

    function appendIndoor() {
        if (!sessionModel) return
        const v = num(sessionModel.data ? sessionModel.data.temperature_C : undefined)
        if (!isFinite(v)) return
        const now = Date.now()
        let list = indoor
        /* The module pushes about one frame per second. Only collapse bursts
         * that arrive within a few hundred ms of each other, otherwise every
         * point would replace the previous one and the curve would never form. */
        if (list.length > 0 && now - list[list.length - 1].t < 400) {
            // Same burst, replace the last point instead of stacking duplicates.
            list = list.slice(0, list.length - 1)
        }
        list = list.concat([{ t: now, v: v }]).slice(-maxPoints)
        indoor = list
        chart.requestPaint()
    }

    Connections {
        target: page.sessionModel
        function onChanged() { page.appendIndoor() }
    }

    /* Safety net: the host normally signals on every frame, but sampling at
     * 1 Hz guarantees the curve keeps moving even if a frame is missed. */
    Timer {
        interval: 1000
        running: !page.diagnostics
        repeat: true
        onTriggered: page.appendIndoor()
    }

    Component.onCompleted: {
        appendIndoor()
        fetchWeather()
    }

    Timer {
        interval: 10 * 60 * 1000
        running: true
        repeat: true
        onTriggered: page.fetchWeather()
    }

    function fetchWeather() {
        if (weatherBusy) return
        weatherBusy = true
        const url = "https://api.open-meteo.com/v1/forecast?latitude=" + weatherLat
                  + "&longitude=" + weatherLon
                  + "&current=temperature_2m,weather_code"
                  + "&daily=temperature_2m_max,temperature_2m_min"
                  + "&forecast_days=1"
                  + "&timezone=Asia%2FShanghai"
        const xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            page.weatherBusy = false
            if (xhr.status !== 200) {
                page.weatherOk = false
                page.outdoorText = "室外 --"
                page.weatherInfo = "天气服务不可达"
                return
            }
            try {
                const d = JSON.parse(xhr.responseText)
                const t = Number(d.current.temperature_2m)
                if (!isFinite(t)) throw new Error("no temperature")
                page.outdoorTemp = t
                if (d.daily) {
                    page.outdoorHigh = Number(d.daily.temperature_2m_max[0])
                    page.outdoorLow = Number(d.daily.temperature_2m_min[0])
                }
                page.weatherOk = true
                page.outdoorText = page.weatherPlace + " " + t.toFixed(1) + " °C"
                page.weatherInfo = page.weatherCodeText(Number(d.current.weather_code))
                page.outdoor = (page.outdoor.concat([{ t: Date.now(), v: t }])).slice(-page.maxPoints)
                page.chart.requestPaint()
            } catch (e) {
                page.weatherOk = false
                page.outdoorText = "室外 --"
                page.weatherInfo = "天气数据解析失败"
            }
        }
        xhr.open("GET", url)
        xhr.timeout = 15000
        xhr.send()
    }

    function weatherCodeText(code) {
        if (!isFinite(code)) return ""
        if (code === 0) return "晴"
        if (code <= 3) return "多云"
        if (code <= 48) return "雾"
        if (code <= 57) return "毛毛雨"
        if (code <= 67) return "雨"
        if (code <= 77) return "雪"
        if (code <= 82) return "阵雨"
        if (code <= 86) return "阵雪"
        return "雷雨"
    }

    // --- light assessment ---------------------------------------------------
    readonly property real lux: num(latest.light_lux)
    readonly property string lightLabel: {
        if (!isFinite(lux)) return "未连接"
        if (lux < 50) return "很暗 · 建议开灯"
        if (lux < 150) return "偏暗 · 阅读吃力"
        if (lux < 500) return "适中 · 适合阅读"
        if (lux < 1000) return "明亮"
        return "过亮 · 避免直射"
    }
    readonly property color lightColor: {
        if (!isFinite(lux)) return "#607d91"
        if (lux < 50) return "#ff6b6b"
        if (lux < 150) return "#ffb454"
        if (lux < 500) return "#7bd88f"
        if (lux < 1000) return "#82e3ff"
        return "#e0a3ff"
    }
    // 0..1 position of the current lux on a log scale (1 .. 2000 lux)
    readonly property real lightRatio: {
        if (!isFinite(lux)) return 0
        const clamped = Math.max(1, Math.min(2000, lux))
        return Math.log(clamped) / Math.log(2000)
    }

    readonly property real indoorTemp: num(latest.temperature_C)
    readonly property real delta: (isFinite(indoorTemp) && isFinite(outdoorTemp))
                                  ? indoorTemp - outdoorTemp : NaN

    // --- header -------------------------------------------------------------
    Text {
        id: heading
        x: page.gap; y: page.gap
        width: Math.max(0, page.width - 2 * page.gap - button.width - page.gap)
        text: "环境监测"
        color: "#f2f8ff"
        font.pointSize: 22 * page.uiScale
        font.bold: true
    }
    Button {
        id: button
        anchors.right: parent.right
        anchors.rightMargin: page.gap
        y: page.gap
        width: Math.min(150 * page.uiScale, Math.max(0, page.width * 0.28))
        height: Math.max(48 * page.uiScale, implicitHeight)
        text: page.diagnostics ? "仪表盘" : "报文诊断"
        font.pointSize: 15 * page.uiScale
        onClicked: page.diagnostics = !page.diagnostics
    }

    // --- body ---------------------------------------------------------------
    Item {
        id: body
        visible: !page.diagnostics
        x: page.gap
        y: heading.y + heading.height + page.gap
        width: Math.max(0, page.width - 2 * page.gap)
        height: cards.height + gap + chartBox.height + gap + footer.height

        Row {
            id: cards
            width: parent.width
            spacing: page.gap
            readonly property real cw: (width - 2 * spacing) / 3

            // Indoor
            Rectangle {
                width: cards.cw; height: 104 * page.uiScale
                radius: 12 * page.uiScale
                color: "#102738"; border.color: "#2b4a5e"
                Rectangle { x:0; y:0; width:4*page.uiScale; height:parent.height; radius:2*page.uiScale; color:"#ffb454" }
                Text { x:14*page.uiScale; y:10*page.uiScale; text:"室内温度"; color:"#8fa9bb"; font.pointSize:14*page.uiScale }
                Text {
                    x:14*page.uiScale; y:34*page.uiScale
                    text: page.fmt(page.indoorTemp, 1) + " °C"
                    color:"#ffffff"; font.pointSize:28*page.uiScale; font.bold:true
                }
                Text {
                    x:14*page.uiScale; y:78*page.uiScale
                    text: isFinite(page.delta) ? ("比室外 " + (page.delta >= 0 ? "+" : "") + page.delta.toFixed(1) + " °C") : "室外数据未就绪"
                    color:"#607d91"; font.pointSize:12*page.uiScale
                }
            }

            // Outdoor
            Rectangle {
                width: cards.cw; height: 104 * page.uiScale
                radius: 12 * page.uiScale
                color: "#102738"; border.color: "#2b4a5e"
                Rectangle { x:0; y:0; width:4*page.uiScale; height:parent.height; radius:2*page.uiScale; color:"#82e3ff" }
                Text { x:14*page.uiScale; y:10*page.uiScale; text:"室外温度"; color:"#8fa9bb"; font.pointSize:14*page.uiScale }
                Text {
                    x:14*page.uiScale; y:34*page.uiScale
                    text: page.weatherOk ? (page.outdoorTemp.toFixed(1) + " °C") : "--"
                    color:"#ffffff"; font.pointSize:28*page.uiScale; font.bold:true
                }
                Text {
                    x:14*page.uiScale; y:78*page.uiScale
                    text: page.weatherOk
                          ? (page.weatherPlace + " " + page.weatherInfo
                             + (isFinite(page.outdoorHigh) && isFinite(page.outdoorLow)
                                ? (" · " + page.outdoorLow.toFixed(0) + "~" + page.outdoorHigh.toFixed(0) + "°C")
                                : ""))
                          : page.weatherInfo
                    color:"#607d91"; font.pointSize:12*page.uiScale
                    elide: Text.ElideRight
                    width: parent.width - 22*page.uiScale
                }
            }

            // Light
            Rectangle {
                width: cards.cw; height: 104 * page.uiScale
                radius: 12 * page.uiScale
                color: "#102738"; border.color: "#2b4a5e"
                Rectangle { x:0; y:0; width:4*page.uiScale; height:parent.height; radius:2*page.uiScale; color: page.lightColor }
                Text { x:14*page.uiScale; y:10*page.uiScale; text:"光照强度"; color:"#8fa9bb"; font.pointSize:14*page.uiScale }
                Text {
                    x:14*page.uiScale; y:34*page.uiScale
                    text: isFinite(page.lux) ? (Math.round(page.lux) + " lux") : "--"
                    color:"#ffffff"; font.pointSize:28*page.uiScale; font.bold:true
                }
                Rectangle {
                    x:14*page.uiScale; y:76*page.uiScale
                    width: parent.width - 28*page.uiScale
                    height: 6*page.uiScale; radius:3*page.uiScale
                    color: "#1c3547"
                    Rectangle {
                        width: parent.width * page.lightRatio
                        height: parent.height; radius: parent.radius
                        color: page.lightColor
                    }
                }
                Text {
                    x:14*page.uiScale; y:86*page.uiScale
                    width: parent.width - 28*page.uiScale
                    text: page.lightLabel
                    color: page.lightColor; font.pointSize:11*page.uiScale
                    elide: Text.ElideRight
                }
            }
        }

        // Chart
        Rectangle {
            id: chartBox
            y: cards.height + page.gap
            width: parent.width
            height: 196 * page.uiScale
            radius: 12 * page.uiScale
            color: "#102738"; border.color: "#2b4a5e"

            Text {
                id: chartTitle
                x:14*page.uiScale; y:8*page.uiScale
                text: page.chartCaption
                color:"#e4eff8"; font.pointSize:14*page.uiScale
            }
            // legend
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 14*page.uiScale
                y: 8*page.uiScale
                spacing: 12*page.uiScale
                Rectangle { width:10*page.uiScale; height:3*page.uiScale; radius:1; color:"#ffb454"; anchors.verticalCenter: parent.verticalCenter }
                Text { text:"室内"; color:"#8fa9bb"; font.pointSize:11*page.uiScale; anchors.verticalCenter: parent.verticalCenter }
                Rectangle { width:10*page.uiScale; height:3*page.uiScale; radius:1; color:"#82e3ff"; anchors.verticalCenter: parent.verticalCenter }
                Text { text:"室外"; color:"#8fa9bb"; font.pointSize:11*page.uiScale; anchors.verticalCenter: parent.verticalCenter }
            }

            Canvas {
                id: chart
                x: 14*page.uiScale
                y: chartTitle.y + chartTitle.height + 10*page.uiScale
                width: Math.max(0, parent.width - 28*page.uiScale)
                height: Math.max(0, parent.height - y - 12*page.uiScale)
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: paintChart()
            }
        }

        // Footer
        Row {
            id: footer
            y: chartBox.y + chartBox.height + page.gap
            width: parent.width
            spacing: page.gap
            Text {
                text: "湿度 " + (isFinite(page.num(page.latest.humidity_RH)) ? page.num(page.latest.humidity_RH).toFixed(1) + "%" : "--")
                color:"#b5c9d9"; font.pointSize:13*page.uiScale
            }
            Text {
                text: "气压 " + (isFinite(page.num(page.latest.pressure_hPa)) ? (page.num(page.latest.pressure_hPa)/1).toFixed(1) + " hPa" : "--")
                color:"#b5c9d9"; font.pointSize:13*page.uiScale
            }
            Text {
                text: "电池 " + (isFinite(page.num(page.latest.battery_V))
                                 ? page.num(page.latest.battery_V).toFixed(2) + "V "
                                   + (isFinite(page.num(page.latest.battery_pct)) ? page.num(page.latest.battery_pct) + "%" : "")
                                 : "--")
                color:"#9be564"; font.pointSize:13*page.uiScale
            }
            Text {
                text: "样本 " + page.indoor.length
                color:"#4d6577"; font.pointSize:13*page.uiScale
            }
        }
    }

    function paintChart() {
        const ctx = chart.getContext("2d")
        ctx.clearRect(0, 0, chart.width, chart.height)
        const w = chart.width, h = chart.height
        if (w < 20 || h < 20) return

        /* Anchor the right edge to the newest sample so the curve always ends
         * flush with the axis instead of drifting left between samples. */
        const now = page.indoor.length > 0
                    ? page.indoor[page.indoor.length - 1].t
                    : Date.now()
        const windowMs = page.chartSpanMs
        const t0 = now - windowMs

        // Collect visible points
        const inPts = []
        for (let i = 0; i < page.indoor.length; i++) {
            const p = page.indoor[i]
            if (p.t >= t0) inPts.push(p)
        }

        // Y range from indoor data (padded), falls back to a sensible span
        let lo = 20, hi = 30
        if (inPts.length > 0) {
            lo = hi = inPts[0].v
            for (let i = 1; i < inPts.length; i++) {
                lo = Math.min(lo, inPts[i].v)
                hi = Math.max(hi, inPts[i].v)
            }
        }
        if (isFinite(page.outdoorTemp)) {
            lo = Math.min(lo, page.outdoorTemp)
            hi = Math.max(hi, page.outdoorTemp)
        }
        if (hi - lo < 2) { lo -= 1; hi += 1 }
        const pad = (hi - lo) * 0.15
        lo -= pad; hi += pad

        const px = function(t) { return (t - t0) / windowMs * w }
        const py = function(v) { return h - 6 - (h - 20) * (v - lo) / (hi - lo) }

        // grid
        ctx.strokeStyle = "#1c3547"
        ctx.lineWidth = 1
        for (let g = 0; g <= 4; g++) {
            const yy = 6 + (h - 20) * g / 4
            ctx.beginPath(); ctx.moveTo(0, yy); ctx.lineTo(w, yy); ctx.stroke()
        }

        // y labels
        ctx.fillStyle = "#4d6577"
        ctx.font = Math.round(9 * page.uiScale) + "px sans-serif"
        ctx.fillText(hi.toFixed(1), 2, 14)
        ctx.fillText(lo.toFixed(1), 2, h - 4)

        // outdoor: horizontal stepped line
        if (isFinite(page.outdoorTemp)) {
            const y = py(page.outdoorTemp)
            ctx.save()
            ctx.setLineDash([6, 4])
            ctx.strokeStyle = "#82e3ff"
            ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke()
            ctx.restore()
        }

        // indoor polyline
        if (inPts.length >= 2) {
            ctx.strokeStyle = "#ffb454"
            ctx.lineWidth = 2.4
            ctx.beginPath()
            for (let i = 0; i < inPts.length; i++) {
                const X = px(inPts[i].t), Y = py(inPts[i].v)
                if (i === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y)
            }
            ctx.stroke()

            // last point marker
            const last = inPts[inPts.length - 1]
            ctx.fillStyle = "#ffb454"
            ctx.beginPath()
            ctx.arc(px(last.t), py(last.v), 3, 0, Math.PI * 2)
            ctx.fill()
        } else {
            ctx.fillStyle = "#4d6577"
            ctx.font = Math.round(12 * page.uiScale) + "px sans-serif"
            ctx.fillText("等待温度数据…", 8, h / 2)
        }
    }

    TextArea {
        id: report
        visible: page.diagnostics
        x: page.gap
        y: body.y
        width: Math.max(0, page.width - 2 * page.gap)
        height: Math.max(240 * page.uiScale, contentHeight + topPadding + bottomPadding)
        readOnly: true
        wrapMode: TextEdit.Wrap
        text: JSON.stringify(page.latest, null, 2)
        color: "#e4eff8"
        font.pointSize: 14 * page.uiScale
        background: Rectangle { color: "#102738"; radius: 10 * page.uiScale }
    }
}
