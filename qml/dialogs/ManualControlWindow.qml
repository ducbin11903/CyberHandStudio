import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Manual control: one vertical slider per joint with command / measured / current,
// plus the total current draw.
ToolWindow {
    id: win
    objectName: "manual"
    title: qsTr("Manual Control — CyberHand Studio")
    width: 720
    height: 470
    minimumWidth: 560
    minimumHeight: 380

    property bool live: true
    property bool output: true
    readonly property var joints: Core.hand.joints
    readonly property real totalLimit: {
        var s = 0
        for (var i = 0; i < joints.length; ++i) s += joints[i].currentLimitMa
        return s
    }

    function send(force) {
        if (!win.output) return
        Core.player.pause()
        Core.ctrl.streamCommands(force)
    }

    Timer {
        id: sendTimer
        interval: Math.max(5, Math.round(1000 / Core.player.sendRate))
        onTriggered: win.send(false)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ---- header ---------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextLabel { text: qsTr("Manual Control"); font.bold: true; font.pixelSize: Theme.fontSize + 2 }
            StatusLight { compact: true }
            Item { Layout.fillWidth: true }
            FlatCheck { text: qsTr("Gửi khi kéo"); checked: win.live; onToggled: win.live = checked }
            FlatCheck { text: qsTr("Output"); checked: win.output; onToggled: win.output = checked }
            VSep {}
            FlatButton {
                text: qsTr("Lấy từ tay")
                tip: qsTr("Đặt slider theo góc đo được")
                onClicked: {
                    for (var i = 0; i < win.joints.length; ++i)
                        if (win.joints[i].hasFeedback) win.joints[i].commandDeg = win.joints[i].measuredDeg
                }
            }
            FlatButton {
                text: qsTr("Về 0")
                onClicked: {
                    for (var i = 0; i < win.joints.length; ++i) win.joints[i].commandDeg = win.joints[i].minDeg
                    win.send(true)
                }
            }
            FlatButton { text: qsTr("Gửi"); accent: true; iconPath: Icons.send; onClicked: win.send(true) }
        }
        TextLabel {
            Layout.fillWidth: true
            visible: Core.ctrl.protocol === 0 || Core.ctrl.portKeys.length > 0
            text: Core.ctrl.portKeys.length > 0
                  ? qsTr("Cổng %1: chỉ gửi %2; khớp mờ nằm ở MCU kia, chỉ cập nhật mô phỏng.").arg(Core.ctrl.portMcu).arg(Core.ctrl.portKeys.join(", "))
                  : qsTr("Giao thức Bench 1 khớp: chỉ %1 được gửi tới tay, các khớp khác chỉ cập nhật mô phỏng.").arg(Core.hand.joints[Core.hand.benchJoint].name)
            muted: true
            font.pixelSize: Theme.fontSmall
        }

        // ---- joint columns ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            Repeater {
                model: win.joints
                Rectangle {
                    id: col
                    readonly property var j: modelData
                    readonly property bool sent: !j.offPort && (Core.ctrl.protocol !== 0 || index === Core.hand.benchJoint)
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: Theme.bgSunken
                    border.width: index === Core.hand.benchJoint ? 1 : 0
                    border.color: Theme.divider

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4
                        opacity: col.j.offPort ? 0.5 : 1.0
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 4
                            Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.jointColor(index) }
                            TextLabel { text: (index + 1) + " · " + col.j.key; font.bold: true }
                        }
                        TextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: col.j.offPort ? qsTr("chưa kết nối") : col.j.name
                            color: col.j.offPort ? Theme.warnAmber : Theme.textSecondary
                            font.pixelSize: Theme.fontTiny
                        }
                        // G474: one motor of the MCU on / off (e <key> / d <key>)
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            visible: Core.ctrl.portKeys.length > 0 && !col.j.offPort
                            spacing: 2
                            FlatButton { text: "E"; tip: "e " + col.j.key; onClicked: Core.ctrl.enableJoint(index) }
                            FlatButton { text: "D"; danger: true; tip: "d " + col.j.key; onClicked: Core.ctrl.disableJoint(index) }
                        }
                        NumberField {
                            Layout.fillWidth: true
                            value: col.j.commandDeg; decimals: 1; from: col.j.minDeg; to: col.j.maxDeg; unit: "°"
                            onCommitted: (v) => { col.j.commandDeg = v; win.send(false) }
                        }
                        TextLabel { Layout.alignment: Qt.AlignHCenter; text: Math.round(col.j.maxDeg) + "°"; muted: true; font.pixelSize: Theme.fontTiny }

                        // slider + measured marker + current bar
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 90
                            FlatSlider {
                                id: sl
                                orientation: Qt.Vertical
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.horizontalCenterOffset: -8
                                height: parent.height
                                from: col.j.minDeg; to: col.j.maxDeg; stepSize: 0.5
                                value: col.j.commandDeg
                                fillColor: Theme.jointColor(index)
                                opacity: col.sent ? 1 : 0.55
                                onMoved: { col.j.commandDeg = value; if (win.live && !sendTimer.running) sendTimer.start() }
                                onPressedChanged: if (!pressed) win.send(false)
                            }
                            // measured position
                            Rectangle {
                                visible: col.j.hasFeedback
                                x: sl.x + sl.width / 2 - 13
                                y: sl.topPadding + (1 - Math.max(0, Math.min(1, (col.j.measuredDeg - col.j.minDeg) / Math.max(1e-6, col.j.maxDeg - col.j.minDeg)))) * (sl.availableHeight - 12) + 5
                                width: 7; height: 2
                                color: Theme.infoBlue
                            }
                            // current, 0 .. 1.5 x limit
                            Rectangle {
                                id: iTrack
                                readonly property real maxMa: col.j.currentLimitMa * 1.5
                                anchors.left: sl.right
                                anchors.leftMargin: 6
                                width: 5
                                height: parent.height
                                radius: 2
                                color: Theme.bgDeep
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    radius: 2
                                    height: Math.min(1, Math.abs(col.j.currentMa) / iTrack.maxMa) * parent.height
                                    color: col.j.overLimit ? Theme.liveRed : Theme.barText
                                }
                                Rectangle {
                                    width: 11; height: 2
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: parent.height - (1 / 1.5) * parent.height
                                    color: Theme.warnAmber
                                }
                            }
                        }
                        TextLabel { Layout.alignment: Qt.AlignHCenter; text: Math.round(col.j.minDeg) + "°"; muted: true; font.pixelSize: Theme.fontTiny }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                        RowLayout {
                            Layout.fillWidth: true
                            TextLabel { text: qsTr("Đo"); secondary: true; font.pixelSize: Theme.fontTiny; Layout.fillWidth: true }
                            TextLabel { text: col.j.hasFeedback ? col.j.measuredDeg.toFixed(1) + "°" : "—"; color: Theme.infoBlue; font.pixelSize: Theme.fontSmall }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextLabel { text: qsTr("Dòng"); secondary: true; font.pixelSize: Theme.fontTiny; Layout.fillWidth: true }
                            TextLabel {
                                text: col.j.hasFeedback ? Math.round(col.j.currentMa) + " mA" : "—"
                                color: col.j.overLimit ? Theme.liveRed : Theme.textPrimary
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                    }
                }
            }
        }

        // ---- total current ------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextLabel { text: qsTr("Total Current Draw"); font.bold: true }
            Rectangle {
                Layout.fillWidth: true
                height: 8
                radius: 4
                color: Theme.bgDeep
                Rectangle {
                    height: parent.height
                    radius: 4
                    width: Math.min(1, Core.telemetry.totalMa / Math.max(1, win.totalLimit)) * parent.width
                    color: Core.telemetry.overLimit ? Theme.liveRed : Theme.barText
                }
            }
            ValueBox { Layout.preferredWidth: 90; text: Core.link.connected && Core.telemetry.hasData ? Math.round(Core.telemetry.totalMa) + " mA" : "—" }
        }
    }
}
