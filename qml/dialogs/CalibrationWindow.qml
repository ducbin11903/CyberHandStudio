import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Encoder calibration per joint: auto sweep between the two mechanical stops, or
// points taken by hand, least-squares line, apply to the app and the firmware.
ToolWindow {
    id: win
    objectName: "calibration"
    title: qsTr("Calibration — CyberHand Studio")
    width: 820
    height: 560
    minimumWidth: 680
    minimumHeight: 460

    readonly property var cal: Core.calibration
    readonly property var joint: Core.hand.joints[cal.joint]

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // ---- joints list ---------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                TextLabel { text: qsTr("Calibration"); font.bold: true; font.pixelSize: Theme.fontSize + 2; Layout.fillWidth: true }
                StatusLight { compact: true }
            }
            Repeater {
                model: Core.hand.joints
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 46
                    radius: Theme.radiusSmall
                    color: index === win.cal.joint ? Theme.bgHover : Theme.bgSunken
                    border.width: index === win.cal.joint ? 1 : 0
                    border.color: Theme.divider
                    MouseArea { anchors.fill: parent; enabled: !win.cal.running; onClicked: win.cal.joint = index }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 6
                        spacing: 6
                        Rectangle { width: 8; height: 8; radius: 4; color: Theme.jointColor(index) }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            TextLabel { Layout.fillWidth: true; text: (index + 1) + " · " + modelData.name }
                            TextLabel { Layout.fillWidth: true; text: Math.round(modelData.countsPer90) + qsTr(" cnt/90°"); muted: true; font.pixelSize: Theme.fontTiny }
                        }
                        FlatButton {
                            text: win.cal.running && win.cal.joint === index ? qsTr("Stop") : qsTr("Start")
                            accent: !(win.cal.running && win.cal.joint === index)
                            enabled: !win.cal.running || win.cal.joint === index
                            tip: qsTr("Home về chặn duỗi rồi gập tới chặn gập, đo hành trình")
                            onClicked: {
                                if (win.cal.running) { win.cal.stopSweep(); return }
                                win.cal.joint = index
                                win.cal.startSweep()
                            }
                        }
                    }
                }
            }
            TextLabel {
                Layout.fillWidth: true
                visible: Core.ctrl.protocol === 0
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                muted: true
                font.pixelSize: Theme.fontTiny
                text: qsTr("Bench 1 khớp: Start đặt khớp đó làm khớp chính rồi chạy e → H → o +duty tới khi kẹt.")
            }
            Item { Layout.fillHeight: true }
            FormRow {
                label: qsTr("Duty quét"); labelWidth: 110
                NumberField { Layout.fillWidth: true; value: win.cal.sweepPct; decimals: 0; from: 5; to: 100; unit: "%"; onCommitted: (v) => win.cal.sweepPct = v }
            }
            FormRow {
                label: qsTr("Góc chặn gập"); labelWidth: 110
                NumberField { Layout.fillWidth: true; value: win.cal.stopDeg; decimals: 1; from: -360; to: 360; unit: "°"; onCommitted: (v) => win.cal.stopDeg = v }
            }
            FlatCheck {
                text: qsTr("Ghi cnt/90° xuống firmware khi kết nối")
                checked: win.cal.syncOnConnect
                onToggled: win.cal.syncOnConnect = checked
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: Theme.divider }

        // ---- points + fit ----------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Rectangle { width: 9; height: 9; radius: 4.5; color: Theme.jointColor(win.cal.joint) }
                TextLabel { text: win.joint ? win.joint.name : ""; font.bold: true; Layout.fillWidth: true }
                TextLabel {
                    text: win.cal.status
                    color: win.cal.running ? Theme.warnAmber : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                }
            }
            // take a point by hand
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextLabel { text: qsTr("Encoder"); secondary: true }
                ValueBox { Layout.preferredWidth: 90; text: win.joint && win.joint.hasFeedback ? Math.round(win.joint.rawCounts) + " cnt" : "—" }
                TextLabel { text: qsTr("Góc thật"); secondary: true }
                NumberField { id: degField; Layout.preferredWidth: 80; value: 0; decimals: 1; from: -360; to: 360; unit: "°"; onCommitted: (v) => value = v }
                FlatButton {
                    text: qsTr("Ghi điểm")
                    iconPath: Icons.plus
                    enabled: win.joint && win.joint.hasFeedback && !win.cal.running
                    tip: qsTr("Đặt khớp ở góc đo bằng thước, nhập góc, bấm để ghi")
                    onClicked: win.cal.addPoint(degField.value)
                }
                Item { Layout.fillWidth: true }
                FlatButton { text: qsTr("Xóa hết"); enabled: win.cal.points.length > 0 && !win.cal.running; onClicked: win.cal.clearPoints() }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                // points table
                Rectangle {
                    Layout.preferredWidth: 210
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: Theme.bgSunken
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        RowLayout {
                            TextLabel { text: "#"; secondary: true; Layout.preferredWidth: 20; font.pixelSize: Theme.fontSmall }
                            TextLabel { text: qsTr("Góc"); secondary: true; Layout.preferredWidth: 50; font.pixelSize: Theme.fontSmall }
                            TextLabel { text: qsTr("Count"); secondary: true; Layout.fillWidth: true; font.pixelSize: Theme.fontSmall }
                        }
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: win.cal.points
                            delegate: RowLayout {
                                width: ListView.view.width
                                height: 22
                                TextLabel { text: index + 1; muted: true; Layout.preferredWidth: 20; font.pixelSize: Theme.fontSmall }
                                TextLabel { text: Number(modelData.deg).toFixed(1) + "°"; Layout.preferredWidth: 50; font.pixelSize: Theme.fontSmall }
                                TextLabel { text: Math.round(modelData.cnt); Layout.fillWidth: true; font.pixelSize: Theme.fontSmall }
                                IconButton { width: 18; height: 18; iconPath: Icons.close; onClicked: win.cal.removePoint(index) }
                            }
                        }
                    }
                }
                // chart: counts vs angle, points + fitted line
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: Theme.bgDeep
                    Canvas {
                        id: chart
                        anchors.fill: parent
                        anchors.margins: 12
                        anchors.leftMargin: 44
                        anchors.bottomMargin: 24
                        property var pts: win.cal.points
                        property real slope: win.cal.slope
                        property real offset: win.cal.offset
                        onPtsChanged: requestPaint()
                        onSlopeChanged: requestPaint()
                        onOffsetChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        property real x0: 0
                        property real x1: 90
                        property real y0: 0
                        property real y1: 7000
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var p = pts
                            var j = win.joint
                            var dx0 = j ? Math.min(j.minDeg, 0) : 0, dx1 = j ? Math.max(j.maxDeg, 90) : 90
                            var cy0 = 0, cy1 = j ? Math.abs(j.countsPer90) * dx1 / 90 : 7000
                            for (var i = 0; i < p.length; ++i) {
                                dx0 = Math.min(dx0, p[i].deg); dx1 = Math.max(dx1, p[i].deg)
                                cy0 = Math.min(cy0, p[i].cnt); cy1 = Math.max(cy1, p[i].cnt)
                            }
                            if (win.cal.hasFit) {
                                cy0 = Math.min(cy0, slope * dx0 + offset, slope * dx1 + offset)
                                cy1 = Math.max(cy1, slope * dx0 + offset, slope * dx1 + offset)
                            }
                            if (cy1 - cy0 < 1) cy1 = cy0 + 1
                            x0 = dx0; x1 = dx1; y0 = cy0; y1 = cy1
                            function X(d) { return (d - dx0) / Math.max(1e-6, dx1 - dx0) * width }
                            function Y(c) { return height - (c - cy0) / (cy1 - cy0) * height }
                            ctx.strokeStyle = Theme.divider
                            ctx.lineWidth = 1
                            for (var g = 0; g <= 4; ++g) {
                                var gy = Math.round(g / 4 * height) + 0.5
                                ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke()
                            }
                            if (win.cal.hasFit) {
                                ctx.strokeStyle = Theme.playhead
                                ctx.lineWidth = 1.5
                                ctx.beginPath()
                                ctx.moveTo(X(dx0), Y(slope * dx0 + offset))
                                ctx.lineTo(X(dx1), Y(slope * dx1 + offset))
                                ctx.stroke()
                            }
                            ctx.fillStyle = Theme.jointColor(win.cal.joint)
                            for (var k = 0; k < p.length; ++k) {
                                ctx.beginPath(); ctx.arc(X(p[k].deg), Y(p[k].cnt), 4, 0, 2 * Math.PI); ctx.fill()
                            }
                        }
                        Text { x: -40; y: -6; text: Math.round(chart.y1); color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny }
                        Text { x: -40; y: chart.height - 8; text: Math.round(chart.y0); color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny }
                        Text { x: 0; y: chart.height + 6; text: Math.round(chart.x0) + "°"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny }
                        Text { anchors.right: parent.right; y: chart.height + 6; text: Math.round(chart.x1) + "°"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny }
                    }
                    TextLabel {
                        anchors.centerIn: parent
                        visible: win.cal.points.length === 0
                        text: qsTr("Chưa có điểm")
                        muted: true
                    }
                }
            }

            // fit result
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                radius: Theme.radiusSmall
                color: Theme.bgSunken
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        TextLabel {
                            text: win.cal.hasFit ? win.cal.equation : qsTr("Cần ≥ 2 điểm ở 2 góc khác nhau")
                            color: win.cal.hasFit ? Theme.infoBlue : Theme.textMuted
                            font.family: Theme.monoFamily
                        }
                        TextLabel {
                            text: win.cal.hasFit
                                  ? qsTr("Mới %1 cnt/90° · hiện tại %2 cnt/90°").arg(Math.round(win.cal.fitCountsPer90)).arg(win.joint ? Math.round(win.joint.countsPer90) : 0)
                                  : ""
                            secondary: true
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    FlatButton {
                        text: qsTr("Áp dụng")
                        accent: true
                        enabled: win.cal.hasFit && !win.cal.running
                        tip: qsTr("Đặt cnt/90° cho khớp, gửi  j <cnt>  nếu đây là khớp bench, lưu cấu hình")
                        onClicked: { if (win.cal.apply()) Core.saveNow() }
                    }
                }
            }
        }
    }
}
