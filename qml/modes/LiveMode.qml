import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// LIVE mode: telemetry channels from the firmware log, scrolling plot.
Item {
    id: root
    readonly property var live: Core.live
    property var hidden: ["en", "flt", "diag", "mode", "duty", "pos", "tgt", "vel"]
    property int preset: 0
    property real windowSec: 10
    readonly property var presets: [
        { name: qsTr("Góc (°)"), only: ["pos_deg", "tgt_deg", "T2_deg", "T1_deg", "I_deg", "M_deg", "R_deg", "P_deg"], min: 0, max: 100, auto: false },
        { name: qsTr("Dòng (mA)"), only: ["i_mA", "i1", "i2", "i3", "i4", "i5", "i6"], min: 0, max: 400, auto: true },
        { name: qsTr("Duty (%)"), only: ["out", "duty"], min: -100, max: 100, auto: false },
        { name: qsTr("Tốc độ (cnt/s)"), only: ["vel"], min: -2000, max: 2000, auto: true },
        { name: qsTr("Tất cả (tự chọn)"), only: [], min: 0, max: 100, auto: true }
    ]

    property Component toolbar: Component {
        RowLayout {
            spacing: 6
            FlatCombo {
                Layout.preferredWidth: 150
                model: root.presets.map(function(p) { return p.name })
                currentIndex: root.preset
                onActivated: (i) => root.preset = i
            }
            FlatCombo {
                Layout.preferredWidth: 80
                model: ["5 s", "10 s", "30 s", "60 s", "120 s"]
                currentIndex: [5, 10, 30, 60, 120].indexOf(root.windowSec)
                onActivated: (i) => root.windowSec = [5, 10, 30, 60, 120][i]
            }
            IconButton {
                iconPath: root.live.paused ? Icons.play : Icons.pause
                iconFilled: true
                tip: root.live.paused ? qsTr("Tiếp tục") : qsTr("Tạm dừng vẽ")
                onClicked: root.live.paused = !root.live.paused
            }
            IconButton { iconPath: Icons.trash; tip: qsTr("Xóa dữ liệu"); onClicked: root.live.clear() }
            VSep {}
            TextLabel {
                secondary: true
                text: Core.ctrl.connected ? qsTr("%1 kênh · %2 Hz").arg(root.live.channelNames.length).arg(Core.telemetry.sampleRate.toFixed(0))
                                          : qsTr("Kết nối để xem log")
            }
            Item { Layout.fillWidth: true }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        PlotItem {
            id: plot
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: root.live
            followLatest: true
            window: root.windowSec
            onlyChannels: root.presets[root.preset].only
            hiddenChannels: root.hidden
            autoY: root.presets[root.preset].auto
            yMin: root.presets[root.preset].min
            yMax: root.presets[root.preset].max
            colors: {
                var c = []
                for (var i = 0; i < root.live.channelNames.length; ++i) c.push(Theme.seriesColor(i))
                return c
            }
            backgroundColor: Theme.bgPanel
            gridColor: Theme.grid
            textColor: Theme.textPrimary
            xLabel: qsTr("Time (s)")
            yLabel: root.presets[root.preset].name
        }
        PlotLegend {
            Layout.preferredWidth: 140
            Layout.fillHeight: true
            readonly property var only: root.presets[root.preset].only
            names: only.length ? root.live.channelNames.filter(function(n) { return only.indexOf(n) >= 0 }) : root.live.channelNames
            colors: names.map(function(n) { return Theme.seriesColor(root.live.channelNames.indexOf(n)) })
            hidden: root.hidden
            onToggled: (name, vis) => {
                var h = root.hidden.slice()
                var i = h.indexOf(name)
                if (vis && i >= 0) h.splice(i, 1)
                if (!vis && i < 0) h.push(name)
                root.hidden = h
            }
        }
    }
}
