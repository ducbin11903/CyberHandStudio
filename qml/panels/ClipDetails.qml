import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Right side of the Timeline: every parameter of the selected clip + formula + chart.
Rectangle {
    id: root
    readonly property var tl: Core.timeline
    readonly property int row: tl.selected
    property int rev: 0
    readonly property var c: (rev, row >= 0 ? tl.clip(row) : ({}))
    readonly property var joint: c.track !== undefined ? Core.hand.joints[c.track] : null
    readonly property int kind: c.kind !== undefined ? c.kind : 0
    readonly property bool wave: kind >= 3 && kind <= 6
    readonly property real lo: joint ? joint.minDeg : 0
    readonly property real hi: joint ? joint.maxDeg : 90
    signal saveCustomRequested(int row)

    radius: Theme.radiusSmall
    color: Theme.bgSunken

    Connections {
        target: root.tl
        function onDataChanged() { root.rev++ }
        function onSelectedChanged() { root.rev++ }
        function onCountChanged() { root.rev++ }
    }

    component Param: RowLayout {
        id: pr
        property string label
        property real value
        property int decimals: 1
        property real from: -1e6
        property real to: 1e6
        property string unit: "°"
        property string param
        spacing: 6
        Layout.fillWidth: true
        TextLabel { text: pr.label; Layout.preferredWidth: 74; secondary: true }
        NumberField {
            Layout.fillWidth: true
            value: pr.value; decimals: pr.decimals; from: pr.from; to: pr.to; unit: pr.unit
            onCommitted: (v) => root.tl.setClipParam(root.row, pr.param, v)
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 8
        visible: root.row >= 0
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: FlatScrollBar {}

        ColumnLayout {
            id: col
            width: flick.width - 6
            spacing: 5

            RowLayout {
                Layout.fillWidth: true
                TextLabel { text: qsTr("Clip Details"); font.bold: true; Layout.fillWidth: true }
                TextLabel { text: "#" + (root.row + 1); muted: true; font.pixelSize: Theme.fontSmall }
            }
            RowLayout {
                spacing: 6
                Layout.fillWidth: true
                TextLabel { text: qsTr("Type"); Layout.preferredWidth: 74; secondary: true }
                FlatCombo {
                    Layout.fillWidth: true
                    model: root.tl.kindNames
                    currentIndex: root.kind
                    onActivated: (i) => root.tl.setClipParam(root.row, "kind", i)
                }
            }
            RowLayout {
                spacing: 6
                Layout.fillWidth: true
                TextLabel { text: qsTr("Axis"); Layout.preferredWidth: 74; secondary: true }
                Rectangle { width: 8; height: 8; radius: 4; color: Theme.jointColor(root.c.track || 0) }
                FlatCombo {
                    Layout.fillWidth: true
                    model: Core.hand.names
                    currentIndex: root.c.track !== undefined ? root.c.track : 0
                    onActivated: (i) => {
                        if (!root.tl.setClipTrack(root.row, i)) {
                            Core.console.error(root.tl.lastError)
                            currentIndex = root.c.track
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            Param { label: qsTr("Start"); value: root.c.start || 0; decimals: 2; from: 0; to: 3600; unit: "s"; param: "start" }
            Param { label: qsTr("End"); value: root.c.end || 0; decimals: 2; from: 0; to: 3600; unit: "s"; param: "end" }
            Param { label: qsTr("Duration"); value: root.c.duration || 0; decimals: 2; from: 0.05; to: 3600; unit: "s"; param: "duration" }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

            // ---- curve profile ------------------------------------------
            TextLabel { text: qsTr("Curve Profile"); font.bold: true }
            // Move
            Param { visible: root.kind === 0; label: qsTr("Target"); value: root.c.target || 0; from: root.lo; to: root.hi; param: "target" }
            FlatSlider {
                visible: root.kind === 0
                Layout.fillWidth: true
                from: root.lo; to: root.hi
                value: root.c.target || 0
                fillColor: root.joint ? Theme.jointColor(root.c.track) : Theme.textSecondary
                onMoved: root.tl.setClipParam(root.row, "target", value)
            }
            RowLayout {
                visible: root.kind === 0
                spacing: 6
                Layout.fillWidth: true
                TextLabel { text: qsTr("Easing"); Layout.preferredWidth: 74; secondary: true }
                FlatCombo {
                    Layout.fillWidth: true
                    model: root.tl.easingNames
                    currentIndex: root.c.easing !== undefined ? root.c.easing : 1
                    onActivated: (i) => root.tl.setClipParam(root.row, "easing", i)
                }
            }
            // Hold
            Param { visible: root.kind === 1; label: qsTr("Value"); value: root.c.a || 0; from: root.lo; to: root.hi; param: "a" }
            // Ramp / Power
            Param { visible: root.kind === 2 || root.kind === 7; label: qsTr("Start"); value: root.c.a || 0; from: root.lo; to: root.hi; param: "a" }
            Param { visible: root.kind === 2 || root.kind === 7; label: qsTr("End"); value: root.c.b || 0; from: root.lo; to: root.hi; param: "b" }
            Param { visible: root.kind === 2; label: qsTr("Slope"); value: root.c.slope || 0; decimals: 1; unit: "°/s"; param: "slope" }
            Param { visible: root.kind === 7; label: qsTr("Exponent"); value: root.c.exponent || 2; decimals: 2; from: 0.1; to: 10; unit: ""; param: "exponent" }
            RowLayout {
                visible: root.kind === 7
                spacing: 6
                Layout.fillWidth: true
                TextLabel { text: qsTr("Shape"); Layout.preferredWidth: 74; secondary: true }
                FlatCombo {
                    Layout.fillWidth: true
                    model: root.tl.shapeNames
                    currentIndex: root.c.shape || 0
                    onActivated: (i) => root.tl.setClipParam(root.row, "shape", i)
                }
            }
            // Waves
            Param { visible: root.wave; label: qsTr("Crest"); value: root.c.b || 0; from: root.lo; to: root.hi; param: "b" }
            Param { visible: root.wave; label: qsTr("Trough"); value: root.c.a || 0; from: root.lo; to: root.hi; param: "a" }
            Param { visible: root.wave; label: qsTr("Period"); value: root.c.period || 1; decimals: 2; from: 0.02; to: 3600; unit: "s"; param: "period" }
            Param { visible: root.wave; label: qsTr("Shift"); value: root.c.shift || 0; decimals: 2; unit: "s"; param: "shift" }

            TextLabel {
                Layout.fillWidth: true
                text: root.c.formula || ""
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideNone
                color: Theme.infoBlue
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSmall
            }
            ClipChart {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                values: (root.rev, root.row >= 0 ? root.tl.clipCurve(root.row, 160) : [])
                lo: root.lo; hi: root.hi
                t0: root.c.start || 0
                t1: root.c.end || 1
                fromValue: root.kind === 0 ? (root.c.fromValue || 0) : NaN
                lineColor: root.joint ? Theme.jointColor(root.c.track) : Theme.clipSelected
                marker: Core.player.position
            }
            TextLabel {
                text: qsTr("Vào %1° · ra %2°").arg(Math.round(root.c.fromValue || 0)).arg(Math.round(root.c.endValue || 0))
                secondary: true
                font.pixelSize: Theme.fontSmall
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                FlatButton { text: qsTr("Nhân bản"); iconPath: Icons.copy; tip: qsTr("Chép clip ra ngay sau nó (Ctrl+D)"); onClicked: root.tl.duplicateClip(root.row) }
                FlatButton { text: qsTr("Lưu Custom"); iconPath: Icons.save; tip: qsTr("Lưu clip này vào thư viện Custom"); onClicked: root.saveCustomRequested(root.row) }
            }
            FlatButton {
                Layout.fillWidth: true
                text: qsTr("Xóa clip")
                danger: true
                iconPath: Icons.trash
                onClicked: root.tl.removeSelected()
            }
        }
    }
    TextLabel {
        anchors.centerIn: parent
        width: parent.width - 20
        visible: root.row < 0
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideNone
        secondary: true
        font.pixelSize: Theme.fontSmall
        text: qsTr("Chọn một clip để sửa kiểu, trục, thời gian và tham số đường cong")
    }
}
