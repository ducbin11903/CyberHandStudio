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
    readonly property bool live: Core.link.connected && tel.hasData

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
                value: !root.live ? "—" : (root.tel.en ? "ON" : "OFF")
                valueColor: root.live && root.tel.en ? Theme.accentGreen : Theme.textSecondary
            }
            Readout {
                title: qsTr("Mode")
                value: !root.live ? "—" : ["Open", "PID", "Home"][root.tel.mode] || ("" + root.tel.mode)
            }
            Readout {
                title: qsTr("Homed")
                value: root.tel.homed === 1 ? qsTr("Yes") : (root.tel.homed === 0 ? qsTr("No") : "—")
                valueColor: root.tel.homed === 1 ? Theme.accentGreen : (root.tel.homed === 0 ? Theme.warnAmber : Theme.textSecondary)
            }
            Readout {
                title: qsTr("Duty out")
                value: root.live ? root.f1(root.tel.out) : "—"
                sub: "%"
            }
            Readout {
                title: qsTr("Position") + (root.bench ? " · " + root.bench.key : "")
                value: root.live ? root.f1(root.tel.posDeg) : "—"
                sub: root.live ? "° · " + root.f0(root.tel.pos) + " cnt" : "°"
            }
            Readout {
                title: qsTr("Target")
                value: root.live && root.tel.mode === 1 ? root.f1(root.tel.tgtDeg) : "—"
                sub: root.live && root.tel.mode === 1 ? "° · " + root.f0(root.tel.tgt) + " cnt" : "°"
            }
            Readout {
                title: qsTr("Error")
                value: root.live && root.tel.mode === 1 ? root.f1(root.tel.errorDeg) : "—"
                sub: "°"
                valueColor: Math.abs(root.tel.errorDeg) > 5 ? Theme.warnAmber : Theme.textStrong
            }
            Readout {
                title: qsTr("Velocity (khớp)")
                value: root.live ? root.f0(root.tel.velDeg) : "—"
                sub: root.live ? "°/s · " + Number(root.tel.outputRpm).toFixed(1) + " RPM" : "°/s"
            }
        }

        // faults
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 4
            TextLabel { text: qsTr("Faults"); secondary: true; font.pixelSize: Theme.fontTiny; Layout.preferredWidth: 42 }
            Repeater {
                model: [{ bit: 1, name: "DIAG" }, { bit: 2, name: qsTr("Quá dòng") }, { bit: 4, name: "KEY" }]
                Badge {
                    readonly property bool active: root.live && ((root.tel.flt & modelData.bit) || (modelData.bit === 1 && root.tel.diagLow))
                    text: modelData.name
                    fill: active ? Theme.liveRed : Theme.bgSunken
                    textColor: active ? Theme.textStrong : Theme.textMuted
                }
            }
            Item { Layout.fillWidth: true }
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
                if (root.tel.noMotion && Math.abs(root.tel.iMa) < 20)
                    m.push(qsTr("Có duty nhưng không quay và không có dòng: kiểm tra nguồn motor, dây motor/encoder."))
                else if (root.tel.noMotion)
                    m.push(qsTr("Có dòng nhưng không quay: kẹt hoặc chạm chặn cơ khí."))
                return m.join("\n")
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
                    ValueBox { text: root.live ? Math.round(root.tel.iMa) : "—" }
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
                    visible: Core.telemetry.firmwareId.length > 0
                    text: Core.telemetry.firmwareId
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
