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

    // the two text channels: serial ports, or the CAN nodes
    QtObject {
        id: dm
        readonly property bool can: Core.ctrl.connectionMode === 2
        readonly property var l1: can ? Core.can.node1 : Core.link
        readonly property var l2: can ? Core.can.node2 : Core.link2
    }

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
        TextLabel {
            text: st.value
            font.pixelSize: Theme.fontSize + 1
            Layout.fillWidth: true
            Layout.maximumWidth: win.width / 3
            elide: Text.ElideMiddle
        }
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
            Stat {
                title: qsTr("Cổng")
                value: {
                    var l = []
                    var p = Core.ctrl.ports
                    for (var i = 0; i < p.length; ++i)
                        if (p[i].open) l.push(p[i].name + (p[i].mcu.length ? " (" + p[i].mcu + ")" : ""))
                    if (!l.length)
                        return "—"
                    return l.join(" + ") + (dm.can ? " · " + Core.can.description : " @ " + Core.link.baudRate)
                }
            }
            Stat { title: qsTr("Log"); value: Core.telemetry.sampleRate.toFixed(0) + " Hz" }
            Stat { title: qsTr("Dòng/s"); value: (dm.l1.linesPerSec + dm.l2.linesPerSec).toFixed(1) }
            Stat {
                title: dm.can ? qsTr("CAN") : qsTr("RX")
                value: dm.can ? qsTr("%1 khung/s · mất %2").arg(Math.round(Core.can.rxFramesPerSec)).arg(Core.can.statusLost)
                              : Math.round(dm.l1.rxBytesPerSec + dm.l2.rxBytesPerSec) + " B/s"
            }
            Stat {
                title: qsTr("Dòng cuối")
                value: {
                    var a = dm.l1.connected ? dm.l1.lastLineAgeMs : -1
                    var b = dm.l2.connected ? dm.l2.lastLineAgeMs : -1
                    var m = Math.max(a, b)
                    return m < 0 ? "—" : m + " ms"
                }
            }
            Stat { title: qsTr("Tổng dòng"); value: Math.round(Core.telemetry.totalMa) + " mA" }
        }
        TextLabel {
            Layout.fillWidth: true
            text: Core.ctrl.idText.length ? "ID: " + Core.ctrl.idText.replace(/\n/g, "\nID: ") : qsTr("Firmware chưa gửi ID (lệnh i)")
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
                            opacity: modelData.offPort ? 0.45 : 1.0
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.jointColor(index) }
                                TextLabel { Layout.fillWidth: true; text: modelData.key + (modelData.offPort ? qsTr("  chưa kết nối") : ""); font.pixelSize: Theme.fontSmall }
                            }
                            V { text: modelData.commandDeg.toFixed(1) }
                            V { text: modelData.hasFeedback ? modelData.measuredDeg.toFixed(1) : "—"; color: Theme.infoBlue }
                            V { text: modelData.hasFeedback ? modelData.targetDeg.toFixed(1) : "—" }
                            V { text: modelData.hasFeedback ? Math.round(modelData.rawCounts) : "—" }
                            V { text: modelData.hasFeedback ? Math.round(modelData.currentMa) : "—"; color: modelData.overLimit ? Theme.liveRed : Theme.textPrimary }
                            V { text: Math.round(modelData.peakMa) }
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.minimumHeight: 6 }
                    // event log: over-current reactions, LINK, OTEMP, VBAT (with time)
                    RowLayout {
                        Layout.fillWidth: true
                        TextLabel { text: qsTr("Event log"); secondary: true; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                        TextLabel { text: Core.events.count; muted: true; font.pixelSize: Theme.fontTiny }
                        IconButton { iconPath: Icons.trash; iconSize: 12; width: 20; height: 20; tip: qsTr("Xóa"); onClicked: Core.events.clear() }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    ListView {
                        id: evList
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        clip: true
                        model: Core.events
                        ScrollBar.vertical: FlatScrollBar {}
                        onCountChanged: Qt.callLater(positionViewAtEnd)
                        delegate: RowLayout {
                            width: ListView.view.width
                            height: 18
                            spacing: 6
                            TextLabel { text: model.time; muted: true; font.family: Theme.monoFamily; font.pixelSize: Theme.fontTiny }
                            TextLabel {
                                text: model.code
                                Layout.preferredWidth: 44
                                color: !model.alarm ? Theme.okGreen
                                       : (["WARN", "RETR", "HOLD"].indexOf(model.code) >= 0 ? Theme.warnAmber : Theme.liveRed)
                                font.pixelSize: Theme.fontTiny
                                font.bold: true
                            }
                            TextLabel { Layout.fillWidth: true; text: model.text; font.pixelSize: Theme.fontSmall }
                        }
                        TextLabel {
                            anchors.centerIn: parent
                            visible: Core.events.count === 0
                            text: qsTr("Chưa có sự kiện")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }
}
