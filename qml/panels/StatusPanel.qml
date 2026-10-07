import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Right column: firmware state readouts + current monitor (reference style).
Card {
    id: root
    readonly property var tel: Core.telemetry
    readonly property var bench: Core.hand.joints[Core.hand.benchJoint]
    readonly property bool live: Core.ctrl.connected && tel.hasData
    // the 10 log channels belong to the main joint only when a port drives it
    readonly property bool mainLive: live && Core.ctrl.benchOnPort
    // fw 0.8: a port bridging the other MCU (LINK chip), link state
    readonly property bool linkMode: {
        var p = Core.ctrl.ports
        for (var i = 0; i < p.length; ++i)
            if (p[i].open && p[i].peer.length) return true
        return (tel.flt & 8) !== 0
    }
    readonly property var board: Core.board
    function levelColor(l) { return l === 2 ? Theme.liveRed : (l === 1 ? Theme.warnAmber : Theme.textStrong) }

    // one board value: caption, value, unit
    component BoardValue: ColumnLayout {
        id: bv
        property string title
        property string value
        property string unit
        property color valueColor: Theme.textStrong
        spacing: 0
        Layout.fillWidth: true
        TextLabel { text: bv.title; secondary: true; font.pixelSize: Theme.fontTiny }
        RowLayout {
            spacing: 2
            TextLabel { text: bv.value; color: bv.valueColor; font.pixelSize: Theme.fontSize + 4 }
            TextLabel { text: bv.unit; muted: true; font.pixelSize: Theme.fontTiny; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 2 }
        }
    }
    readonly property bool linkLost: {
        var p = Core.ctrl.ports
        for (var i = 0; i < p.length; ++i)
            if (p[i].open && p[i].peer.length && p[i].peerUp === 0) return true
        return false
    }

    function f1(v) { return (v === undefined || isNaN(v)) ? "—" : Number(v).toFixed(1) }
    function f0(v) { return (v === undefined || isNaN(v)) ? "—" : Math.round(v) }

    Flickable {
        id: sflick
        anchors.fill: parent
        contentHeight: scol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: FlatScrollBar {}

    ColumnLayout {
        id: scol
        width: sflick.width
        height: Math.max(sflick.height, implicitHeight)
        spacing: 0

        SectionHeader {
            Layout.fillWidth: true
            title: qsTr("Status")
            collapsible: false
            TextLabel {
                text: root.live ? root.tel.sampleRate.toFixed(0) + " Hz" : ""
                secondary: true
                font.pixelSize: Theme.fontSmall
                y: 4
            }
            StatusLight { compact: true; y: 2 }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            columns: 2
            columnSpacing: 10
            rowSpacing: 8

            component Readout: ColumnLayout {
                id: ro
                property string title
                property string value
                property string sub: ""
                property color valueColor: Theme.textStrong
                spacing: 0
                Layout.fillWidth: true
                TextLabel { text: ro.title; secondary: true; font.pixelSize: Theme.fontTiny }
                RowLayout {
                    spacing: 4
                    TextLabel { text: ro.value; color: ro.valueColor; font.pixelSize: Theme.fontLarge }
                    TextLabel { text: ro.sub; muted: true; font.pixelSize: Theme.fontTiny; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 3 }
                }
            }

            Readout {
                title: qsTr("Driver")
                value: !root.mainLive ? "—" : (root.tel.en ? "ON" : "OFF")
                valueColor: root.mainLive && root.tel.en ? Theme.accentGreen : Theme.textSecondary
            }
            Readout {
                title: qsTr("Mode")
                value: !root.mainLive ? "—" : ["Open", "PID", "Home"][root.tel.mode] || ("" + root.tel.mode)
            }
            Readout {
                title: qsTr("Homed")
                value: root.tel.homed === 1 ? qsTr("Yes") : (root.tel.homed === 0 ? qsTr("No") : "—")
                valueColor: root.tel.homed === 1 ? Theme.accentGreen : (root.tel.homed === 0 ? Theme.warnAmber : Theme.textSecondary)
            }
            Readout {
                title: qsTr("Duty out")
                value: root.mainLive ? root.f1(root.tel.out) : "—"
                sub: "%"
            }
            Readout {
                title: qsTr("Position") + (root.bench ? " · " + root.bench.key : "")
                value: root.mainLive ? root.f1(root.tel.posDeg) : "—"
                sub: root.mainLive ? "° · " + root.f0(root.tel.pos) + " cnt" : "°"
            }
            Readout {
                title: qsTr("Target")
                value: root.mainLive && root.tel.mode === 1 ? root.f1(root.tel.tgtDeg) : "—"
                sub: root.mainLive && root.tel.mode === 1 ? "° · " + root.f0(root.tel.tgt) + " cnt" : "°"
            }
            Readout {
                title: qsTr("Error")
                value: root.mainLive && root.tel.mode === 1 ? root.f1(root.tel.errorDeg) : "—"
                sub: "°"
                valueColor: Math.abs(root.tel.errorDeg) > 5 ? Theme.warnAmber : Theme.textStrong
            }
            Readout {
                title: qsTr("Velocity (khớp)")
                value: root.mainLive ? root.f0(root.tel.velDeg) : "—"
                sub: root.mainLive ? "°/s · " + Number(root.tel.outputRpm).toFixed(1) + " RPM" : "°/s"
            }
        }

        // faults: 1 DIAG, 2 OVERI, 4 ESTOP, 8 LINK, 16 OTEMP, 32 VBAT
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 4
            TextLabel { text: qsTr("Faults"); secondary: true; font.pixelSize: Theme.fontTiny; Layout.preferredWidth: 42; Layout.alignment: Qt.AlignTop; Layout.topMargin: 3 }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: [{ bit: 1, name: "DIAG" }, { bit: 2, name: qsTr("Quá dòng") }, { bit: 4, name: "ESTOP" }, { bit: 8, name: "LINK" },
                            { bit: 16, name: qsTr("Quá nhiệt") }, { bit: 32, name: qsTr("Pin yếu") }]
                    Badge {
                        visible: modelData.bit < 8 || (modelData.bit === 8 && root.linkMode) || (modelData.bit >= 16 && root.board.supported)
                        readonly property bool active: (root.live && ((root.tel.flt & modelData.bit) || (modelData.bit === 1 && root.tel.diagLow)
                                                                      || (modelData.bit === 8 && root.linkLost)))
                                                       || (modelData.bit === 16 && root.board.otempState === 2)
                                                       || (modelData.bit === 32 && root.board.vbatState === 2)
                        text: modelData.name
                        fill: active ? Theme.liveRed : Theme.bgSunken
                        textColor: active ? Theme.textStrong : Theme.textMuted
                    }
                }
            }
        }
        TextLabel {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 4
            visible: text.length > 0
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: Theme.fontSmall
            color: Theme.liveRed
            text: {
                if (!root.live) return ""
                var m = []
                if (root.tel.diagLow) m.push(qsTr("Chân DIAG đang thấp khi driver bật: driver lỗi hoặc mất nguồn motor."))
                if (root.linkLost) m.push(qsTr("Link UART tới MCU kia mất: khớp của MCU đó bị khóa."))
                else if (root.tel.flt & 8) m.push(qsTr("MCU kia đã tắt motor do mất link (watchdog). Link đã lên lại: Clear fault (c) để chạy tiếp."))
                if (root.tel.noMotion && Math.abs(root.tel.iMa) < 20)
                    m.push(qsTr("Có duty nhưng không quay và không có dòng: kiểm tra nguồn motor, dây motor/encoder."))
                else if (root.tel.noMotion)
                    m.push(qsTr("Có dòng nhưng không quay: kẹt hoặc chạm chặn cơ khí."))
                return m.join("\n")
            }
        }

        // ---- board sensors (fw 0.9: SHT40 + VBAT on U7) --------------------
        SectionHeader {
            id: boardHdr
            Layout.fillWidth: true
            Layout.topMargin: 10
            title: qsTr("Board")
            visible: root.board.supported || root.board.hasData
            TextLabel {
                y: 5
                font.pixelSize: Theme.fontTiny
                muted: true
                text: {
                    if (!root.board.hasData) return root.board.senseError.length ? "" : qsTr("chưa có số liệu")
                    var a = root.board.ageMs
                    return qsTr("từ %1 · %2").arg(root.board.source).arg(a < 0 ? "—" : (a < 1000 ? a + " ms" : (a / 1000).toFixed(a < 10000 ? 1 : 0) + " s"))
                }
            }
            IconButton {
                width: 18; height: 18
                iconSize: 12
                iconPath: Icons.refresh
                tip: qsTr("Đọc lại cảm biến (sense)")
                enabled: Core.ctrl.connected
                onClicked: root.board.refresh()
            }
        }
        ColumnLayout {
            visible: boardHdr.visible && boardHdr.expanded
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 6
            spacing: 6
            // no new values for 3 s: dimmed
            opacity: root.board.stale ? 0.45 : 1.0

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                BoardValue {
                    title: qsTr("Nhiệt độ")
                    value: root.board.hasTemp ? root.board.temp.toFixed(1) : "—"
                    unit: "°C"
                    valueColor: root.levelColor(root.board.tempLevel)
                }
                BoardValue {
                    title: qsTr("Độ ẩm")
                    value: root.board.hasRh ? root.board.rh.toFixed(0) : "—"
                    unit: "%"
                }
                BoardValue {
                    title: "VBAT"
                    value: root.board.hasVbat ? root.board.vbat.toFixed(2) : "—"
                    unit: root.board.hasVbat && !root.board.batteryPresent ? qsTr("không pin") : "V"
                    valueColor: root.levelColor(root.board.vbatLevel)
                }
                BoardValue {
                    title: qsTr("Pin %1S").arg(root.board.cells)
                    value: isNaN(root.board.batteryPct) ? "—" : Math.round(root.board.batteryPct)
                    unit: "%"
                    valueColor: root.levelColor(root.board.vbatLevel)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    TextLabel { text: qsTr("°C · 10 phút"); muted: true; font.pixelSize: Theme.fontTiny }
                    Sparkline {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        points: root.board.tempHistory
                        now: root.board.historyNow
                        minSpan: 4
                        lineColor: root.board.tempLevel === 0 ? Theme.infoBlue : root.levelColor(root.board.tempLevel)
                        thresholds: root.board.thresholdsKnown
                                    ? [{ v: root.board.tOff, color: Theme.liveRed }, { v: root.board.tOff - 10, color: Theme.warnAmber }]
                                    : []
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    TextLabel { text: qsTr("V · 10 phút"); muted: true; font.pixelSize: Theme.fontTiny }
                    Sparkline {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        points: root.board.vbatHistory
                        now: root.board.historyNow
                        minSpan: 0.4
                        lineColor: root.board.vbatLevel === 0 ? Theme.accentGreen : root.levelColor(root.board.vbatLevel)
                        thresholds: root.board.thresholdsKnown
                                    ? [{ v: root.board.vOff, color: Theme.liveRed }, { v: root.board.vOn, color: Theme.warnAmber }]
                                    : []
                    }
                }
            }
            TextLabel {
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                font.pixelSize: Theme.fontTiny
                color: Theme.liveRed
                text: root.board.senseError.length ? root.board.senseError
                      : (root.board.supported && !root.board.hasTemp && root.board.i2cErr > 0
                         ? qsTr("SHT40 không đọc được (lỗi I2C %1)").arg(root.board.i2cErr) : "")
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.topMargin: 10; height: 1; color: Theme.divider }

        // ---- current monitor --------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            spacing: 6

            // actual current bar
            Rectangle {
                Layout.preferredWidth: 38
                Layout.fillHeight: true
                Layout.minimumHeight: 110
                radius: Theme.radius
                color: Theme.bgSunken
                readonly property real maxMa: Math.max(root.tel.currentLimitMa * 1.5, 100)
                TextLabel {
                    anchors.top: parent.top; anchors.topMargin: 5
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "mA"; muted: true; font.pixelSize: Theme.fontTiny
                }
                Rectangle {
                    id: track
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top; anchors.topMargin: 22
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 10
                    width: 4; radius: 2
                    color: Theme.bgDeep
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; radius: 2
                        height: Math.min(1, Math.abs(root.tel.iMa) / parent.parent.maxMa) * parent.height
                        color: root.tel.overLimit ? Theme.liveRed : Theme.barText
                    }
                    // peak tick
                    Rectangle {
                        width: 12; height: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height - Math.min(1, root.tel.peakMa / parent.parent.maxMa) * parent.height
                        color: Theme.textSecondary
                        visible: root.tel.peakMa > 0
                    }
                    // limit tick
                    Rectangle {
                        width: 16; height: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height - Math.min(1, root.tel.currentLimitMa / parent.parent.maxMa) * parent.height
                        color: Theme.warnAmber
                    }
                }
                Rectangle {
                    width: 10; height: 10; radius: 5
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                    color: root.live ? (root.tel.overLimit ? Theme.liveRed : Theme.ledGreen) : Theme.textMuted
                }
            }

            // limit slider
            Rectangle {
                Layout.preferredWidth: 46
                Layout.fillHeight: true
                radius: Theme.radius
                color: Theme.bgSunken
                TextLabel {
                    anchors.top: parent.top; anchors.topMargin: 5
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Math.round(root.tel.currentLimitMa) + "/" + Math.round(limitSlider.to)
                    font.pixelSize: Theme.fontTiny
                }
                FlatSlider {
                    id: limitSlider
                    orientation: Qt.Vertical
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top; anchors.topMargin: 22
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 18
                    from: 0; to: 1000; stepSize: 10
                    value: root.tel.currentLimitMa
                    fillColor: Theme.limitOlive
                    onMoved: root.tel.currentLimitMa = Math.max(10, value)
                }
                TextLabel {
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "0"; font.pixelSize: Theme.fontTiny; secondary: true
                }
            }

            // table
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 6
                TextLabel { text: qsTr("Current"); font.bold: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                RowLayout {
                    TextLabel { text: qsTr("Actual"); Layout.fillWidth: true }
                    ValueBox { text: root.mainLive ? Math.round(root.tel.iMa) : "—" }
                }
                RowLayout {
                    TextLabel { text: qsTr("Peak"); Layout.fillWidth: true }
                    ValueBox {
                        text: Math.round(root.tel.peakMa)
                        MouseArea { anchors.fill: parent; onClicked: root.tel.resetPeak(); cursorShape: Qt.PointingHandCursor }
                        HoverHandler { id: peakHover }
                        Tip { visible: peakHover.hovered; text: qsTr("Bấm để reset") }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                RowLayout {
                    TextLabel { text: qsTr("Status"); Layout.fillWidth: true }
                    Badge {
                        readonly property bool bad: root.tel.overLimit || root.tel.flt || root.tel.diagLow
                        readonly property string ev: root.tel.recentEvent
                        text: !root.live ? "—" : (ev.length ? ev : (root.tel.overLimit ? "HIGH" : ((root.tel.flt || root.tel.diagLow) ? "FAULT" : (root.tel.noMotion ? "STALL" : "OK"))))
                        fill: !root.live ? Theme.bgSunken : (ev.length ? Theme.warnAmber : (bad ? Theme.liveRed : (root.tel.noMotion ? Theme.warnAmber : Theme.infoBlue)))
                        textColor: !root.live ? Theme.textMuted : Theme.textOnAccent
                        HoverHandler { id: evHover }
                        Tip { visible: evHover.hovered && root.tel.lastEventText.length > 0; text: root.tel.lastEventText }
                    }
                }
                Item { Layout.fillHeight: true }
                TextLabel {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: Core.ctrl.idText.length ? Core.ctrl.idText : Core.telemetry.firmwareId
                    muted: true
                    font.pixelSize: Theme.fontTiny
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }
        }
    }
}
}
