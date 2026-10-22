import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Rectangle {
    id: page
    property var sessionModel: null
    property real hostUiScale: 1
    property bool weekView: false
    property double clock: Date.now()
    readonly property var latest: sessionModel ? sessionModel.data : ({})
    readonly property bool fresh: latest.received_at !== undefined && clock - Number(latest.received_at) < 15000
    readonly property real temperature: number(latest.temperature_C)
    readonly property real humidity: number(latest.humidity_RH)
    readonly property real light: number(latest.light_lux)
    readonly property real battery: number(latest.battery_pct)
    readonly property bool external: latest.external_power === true
    readonly property string source: latest.power_source || "unknown"
    readonly property string sourceText: external ? "外部供电" : source === "battery" ? "电池供电" : "供电来源待检测"
    readonly property string lightText: !isFinite(light) ? "等待光照数据" : light < 50 ? "光线偏暗 · 可移近窗边" : light < 150 ? "光线较弱 · 留意植物需求" : light < 1000 ? "散射光环境 · 按品种调整" : "光线较强 · 留意暴晒"
    readonly property var points: (weekView ? latest.history_week : latest.history_day) || []
    readonly property string protectionText: {
        const s = latest.power_protection || ""
        if (s === "source_unknown_auto_sleep_inhibited") return "未接供电检测，自动休眠未启用（需硬件电池保护）"
        if (s === "external_supply_no_low_battery_shutdown") return "外部供电有效，不因低电量关机"
        if (s === "battery_low_warning") return "电池低压警告，持续低压后安全休眠"
        if (s === "battery_guard_active") return "电池保护已启用，低于3.40V持续20秒后休眠"
        return "等待固件状态"
    }
    Material.theme: Material.Light
    Material.accent: "#23734f"
    implicitHeight: content.height + 40
    color: "#edf4ed"
    radius: 20
    function number(v) { return v === null || v === undefined || typeof v === "boolean" ? NaN : Number(v) }
    function format(v, d) { return isFinite(v) ? v.toFixed(d) : "--" }
    Timer { interval: 1000; running: page.visible; repeat: true; onTriggered: page.clock = Date.now() }
    Column {
        id: content
        x: 20; y: 20; width: parent.width - 40; spacing: 16
        Row {
            width: parent.width; spacing: 12
            Column {
                width: parent.width - 160; spacing: 5
                Text { text: "PLANT COMPANION / 植物环境助手"; color: "#41705b"; font.pixelSize: 12; font.letterSpacing: 1 }
                Text { text: "让每一片叶子，都有好环境"; color: "#163d2e"; font.pixelSize: 24; font.bold: true; width: parent.width; wrapMode: Text.Wrap }
            }
            Rectangle {
                width: 148; height: 48; radius: 24; color: page.fresh ? "#d1e9da" : "#f5dfbe"
                Text { anchors.centerIn: parent; text: page.fresh ? "实时监测中" : "等待新数据"; color: "#254c37"; font.pixelSize: 15 }
            }
        }
        Rectangle {
            width: parent.width; height: width < 600 ? 235 : 212; radius: 22; color: "#183f31"
            PlantScene { width: Math.min(parent.width * 0.32, 210); height: 190; x: 8; y: 10 }
            Column {
                x: Math.min(parent.width * 0.32, 210) + 16; y: 22; width: parent.width - x - 18; spacing: 9
                Text { text: "我的植物小站"; font.pixelSize: 23; font.bold: true; color: "#f0f6dd" }
                Text { text: page.lightText; font.pixelSize: 16; color: "#d8eacb"; width: parent.width; wrapMode: Text.Wrap }
                Text { text: "环境建议仅供参考，最佳温湿度与光照因植物品种而异。"; font.pixelSize: 13; color: "#b3cdba"; width: parent.width; wrapMode: Text.Wrap }
                Rectangle {
                    width: parent.width; height: page.width < 600 ? 75 : 53; radius: 12; color: "#2b5141"
                    Text { anchors.fill: parent; anchors.margins: 10; verticalAlignment: Text.AlignVCenter
                        text: "BME280 空气温湿度  ·  BH1750 光照\n未接土壤湿度探头，不提供土壤读数或自动浇水"; color: "#e1ecd7"; font.pixelSize: 12; wrapMode: Text.Wrap }
                }
            }
        }
        Grid {
            id: metricGrid
            width: parent.width; columns: width < 600 ? 2 : 4; spacing: 10
            Repeater {
                model: [
                    { label: "空气温度", value: page.format(page.temperature, 1), unit: "°C", note: "观察昼夜温差", kind: "temperature", tint: "#dbeadc" },
                    { label: "空气湿度", value: page.format(page.humidity, 1), unit: "%RH", note: "非土壤含水率", kind: "humidity", tint: "#deedf2" },
                    { label: "环境光照", value: page.format(page.light, 0), unit: "lux", note: "照度不等于光合辐射", kind: "light", tint: "#f5edcc" },
                    { label: "大气压力", value: page.format(page.number(page.latest.pressure_hPa), 1), unit: "hPa", note: "记录环境变化", kind: "pressure", tint: "#e9e3f0" }
                ]
                MetricTile { required property var modelData; width: (metricGrid.width - (metricGrid.columns - 1) * metricGrid.spacing) / metricGrid.columns; metric: modelData }
            }
        }
        Rectangle {
            width: parent.width; height: 320; radius: 20; color: "#ffffff"; border.color: "#d8e4d9"
            Column {
                x: 16; y: 14; width: parent.width - 32; spacing: 8
                Row {
                    width: parent.width; spacing: 8
                    Text { width: parent.width - 212; height: 48; verticalAlignment: Text.AlignVCenter; text: "温度生长日记"; color: "#193f2e"; font.pixelSize: 20; font.bold: true }
                    Button { width: 98; height: 48; text: "一天"; onClicked: page.weekView = false; Accessible.name: "查看最近24小时温度"
                        contentItem: Text { text: "一天"; color: !page.weekView ? "white" : "#275d3d"; font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 24; color: !page.weekView ? "#286f49" : "#e6efe5" }
                    }
                    Button { width: 98; height: 48; text: "一周"; onClicked: page.weekView = true; Accessible.name: "查看最近7天温度"
                        contentItem: Text { text: "一周"; color: page.weekView ? "white" : "#275d3d"; font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 24; color: page.weekView ? "#286f49" : "#e6efe5" }
                    }
                }
                TrendChart { width: parent.width; height: 186; points: page.points; weekView: page.weekView; endTime: page.number(page.latest.received_at) }
                Text { width: parent.width; wrapMode: Text.Wrap; color: "#58705f"; font.pixelSize: 12
                    text: page.weekView ? "最近7天 · 每2小时均值 / 淡色带为最低～最高 · 缺测不连线" : "最近24小时 · 每15分钟均值 / 淡色带为最低～最高 · 缺测不连线" }
                Text { width: parent.width; wrapMode: Text.Wrap; color: page.latest.history_error ? "#a1352b" : "#58705f"; font.pixelSize: 12
                    text: page.latest.history_error || ("已记录 " + page.points.length + " 个时段 · 历史保存在Pi，首次使用不会生成虚假日/周数据") }
            }
        }
        Rectangle {
            width: parent.width; height: width < 600 ? 176 : 150; radius: 20; color: "#ffffff"; border.color: "#d8e4d9"
            Column {
                x: 16; y: 14; width: parent.width - 32; spacing: 9
                Row {
                    spacing: 12
                    Rectangle {
                        width: 56; height: 30; radius: 5; color: "transparent"; border.width: 2; border.color: "#376c4d"
                        Rectangle { x: 4; y: 4; height: 22; width: isFinite(page.battery) ? 48 * Math.max(0, Math.min(100, page.battery)) / 100 : 0; radius: 2; color: page.battery < 20 ? "#bd5c3d" : "#72ab76" }
                    }
                    Text { text: "电量 " + page.format(page.battery, 0) + "%   " + page.format(page.number(page.latest.battery_V), 2) + " V"; font.pixelSize: 20; color: "#193f2e"; font.bold: true }
                }
                Text { text: page.sourceText + (page.latest.charging === true ? " · 充电中" : ""); color: "#376c4d"; font.pixelSize: 15 }
                Text { width: parent.width; text: "电量计读数与供电来源分别判断；未接供电检测时显示未知，不推断为USB供电。"; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#58705f" }
                Text { width: parent.width; text: "低压保护：" + page.protectionText; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#58705f" }
            }
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: "#58705f"
            text: "最后更新：" + (page.latest.received_at ? new Date(Number(page.latest.received_at)).toLocaleString(Qt.locale("zh_CN"), "MM-dd hh:mm:ss") : "尚未收到数据") + "   ·   本地采集，不依赖天气互联网服务"
        }
    }
}
