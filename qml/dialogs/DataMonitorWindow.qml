import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Data monitor: link statistics, every log field with min / max, per-joint state.
ToolWindow {
    id: win
    objectName: "datamonitor"
    title: qsTr("Data Monitor — CyberHand Studio")
    width: 760
    height: 520
    minimumWidth: 600
    minimumHeight: 380

    property var stats: ({})
    property var names: []

    function resetStats() { stats = ({}); names = [] }
    function fmt(v) {
        if (v === undefined || v === null || isNaN(v)) return "—"
        var a = Math.abs(v)
        return a >= 1000 || a === Math.round(a) ? Math.round(v).toString() : Number(v).toFixed(a < 1 ? 4 : 2)
    }

    // sample the latest fields at 10 Hz (not on every line)
    Timer {
        interval: 100
        repeat: true
        running: win.visible
        onTriggered: {
            var f = Core.telemetry.fields
            var s = win.stats
            var changed = false
            for (var k in f) {
                var v = f[k]
                if (s[k] === undefined) { s[k] = { v: v, lo: v, hi: v, n: 0 }; changed = true }
                var e = s[k]
                e.v = v
                if (v < e.lo) e.lo = v
                if (v > e.hi) e.hi = v
                e.n++
            }
            if (changed) win.names = Object.keys(s).sort()
            win.stats = s
            fieldList.forceLayout()
            win.statsRev++
        }
    }
    property int statsRev: 0

    component Stat: ColumnLayout {
        id: st
        property string title
        property string value
        spacing: 0
        Layout.fillWidth: true
        TextLabel { text: st.title; secondary: true; font.pixelSize: Theme.fontTiny }
        TextLabel { text: st.value; font.pixelSize: Theme.fontSize + 1 }
    }
    component H: TextLabel { secondary: true; font.pixelSize: Theme.fontSmall; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 54 }
    component V: TextLabel { font.pixelSize: Theme.fontSmall; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 54 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextLabel { text: qsTr("Data Monitor"); font.bold: true; font.pixelSize: Theme.fontSize + 2 }
            StatusLight { compact: true }
            Item { Layout.fillWidth: true }
            FlatButton { text: qsTr("Reset min/max"); onClicked: win.resetStats() }
        }

        // link stats
        GridLayout {
            Layout.fillWidth: true
            columns: 6
            columnSpacing: 8
            rowSpacing: 4
            Stat { title: qsTr("Cổng"); value: Core.link.connected ? Core.link.portName + " @ " + Core.link.baudRate : "—" }
            Stat { title: qsTr("Log"); value: Core.telemetry.sampleRate.toFixed(0) + " Hz" }
            Stat { title: qsTr("Dòng/s"); value: Core.link.linesPerSec.toFixed(1) }
            Stat { title: qsTr("RX"); value: Math.round(Core.link.rxBytesPerSec) + " B/s" }
            Stat { title: qsTr("Dòng cuối"); value: Core.link.lastLineAgeMs < 0 ? "—" : Core.link.lastLineAgeMs + " ms" }
            Stat { title: qsTr("Tổng dòng"); value: Math.round(Core.telemetry.totalMa) + " mA" }
        }
        TextLabel {
            Layout.fillWidth: true
            text: Core.telemetry.firmwareId.length ? "ID: " + Core.telemetry.firmwareId : qsTr("Firmware chưa gửi ID (lệnh i)")
            muted: true
            font.pixelSize: Theme.fontSmall
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // every field of the log line
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                radius: Theme.radiusSmall
                color: Theme.bgSunken
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2
                    RowLayout {
                        TextLabel { text: qsTr("Field"); secondary: true; Layout.preferredWidth: 80; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("Value"); secondary: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("Min"); secondary: true; Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("Max"); secondary: true; Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight; font.pixelSize: Theme.fontSmall }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    ListView {
                        id: fieldList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: win.names
                        ScrollBar.vertical: FlatScrollBar {}
                        delegate: RowLayout {
                            readonly property var e: (win.statsRev, win.stats[modelData])
                            width: ListView.view.width
                            height: 21
                            TextLabel { text: modelData; Layout.preferredWidth: 80; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSmall }
                            TextLabel { text: e ? win.fmt(e.v) : "—"; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; color: Theme.textStrong; font.pixelSize: Theme.fontSmall }
                            TextLabel { text: e ? win.fmt(e.lo) : "—"; Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight; secondary: true; font.pixelSize: Theme.fontSmall }
                            TextLabel { text: e ? win.fmt(e.hi) : "—"; Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight; secondary: true; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    TextLabel {
                        visible: win.names.length === 0
                        text: qsTr("Chưa có dữ liệu log")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }

            // joints
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusSmall
                color: Theme.bgSunken
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2
                    RowLayout {
                        TextLabel { text: qsTr("Khớp"); secondary: true; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                        H { text: qsTr("Lệnh°") }
                        H { text: qsTr("Đo°") }
                        H { text: qsTr("Đích°") }
                        H { text: qsTr("Count") }
                        H { text: "mA" }
                        H { text: qsTr("Peak") }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    Repeater {
                        model: Core.hand.joints
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.jointColor(index) }
                                TextLabel { Layout.fillWidth: true; text: modelData.key; font.pixelSize: Theme.fontSmall }
                            }
                            V { text: modelData.commandDeg.toFixed(1) }
                            V { text: modelData.hasFeedback ? modelData.measuredDeg.toFixed(1) : "—"; color: Theme.infoBlue }
                            V { text: modelData.hasFeedback ? modelData.targetDeg.toFixed(1) : "—" }
                            V { text: modelData.hasFeedback ? Math.round(modelData.rawCounts) : "—" }
                            V { text: modelData.hasFeedback ? Math.round(modelData.currentMa) : "—"; color: modelData.overLimit ? Theme.liveRed : Theme.textPrimary }
                            V { text: Math.round(modelData.peakMa) }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    TextLabel {
                        Layout.fillWidth: true
                        visible: Core.telemetry.lastEventText.length > 0
                        text: qsTr("Sự kiện dòng cuối: ") + Core.telemetry.lastEventText
                        color: Theme.warnAmber
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }
}
