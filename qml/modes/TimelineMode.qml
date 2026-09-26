import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// TIMELINE mode: one track per joint, clips move the joint to a target with an easing curve.
Item {
    id: root
    readonly property var tl: Core.timeline
    readonly property var traj: Core.timelineTrajectory
    readonly property var player: Core.player
    readonly property int labelWidth: 170
    readonly property int rowHeight: 32
    property real zoom: 1.0
    property bool snap: true
    property real snapStep: 0.1
    property bool showCurves: true
    readonly property real pps: Math.max(10, (lanesView.width - 2) / Math.max(0.5, tl.length) * zoom)
    readonly property var easingIcons: [Icons.ramp, Icons.curve, Icons.easeIn, Icons.easeOut, Icons.step]

    function snapT(t) { return snap ? Math.round(t / snapStep) * snapStep : t }

    property Component toolbar: Component {
        RowLayout {
            spacing: 4
            FlatField {
                Layout.preferredWidth: 170
                text: root.tl.name
                onEditingFinished: root.tl.name = text
            }
            IconButton { iconPath: Icons.settings; tip: qsTr("Timeline: độ dài, mở/lưu"); onClicked: settingsPop.open() }
            VSep {}
            IconButton { iconPath: Icons.chart; checkable: true; checked: root.showCurves; tip: qsTr("Hiện đường cong trong clip"); onToggled: root.showCurves = checked }
            IconButton { iconPath: Icons.hash; checkable: true; checked: root.snap; tip: qsTr("Bắt lưới 0.1 s"); onToggled: root.snap = checked }
            VSep {}
            TransportBar { player: root.player }
            VSep {}
            IconButton { iconPath: Icons.fit; tip: qsTr("Vừa khung"); onClicked: root.zoom = 1 }
            IconButton { iconPath: Icons.minusCircle; onClicked: root.zoom = Math.max(1, root.zoom / 1.5) }
            FlatSlider { Layout.preferredWidth: 90; from: 1; to: 12; value: root.zoom; onMoved: root.zoom = value }
            IconButton { iconPath: Icons.plusCircle; onClicked: root.zoom = Math.min(12, root.zoom * 1.5) }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Mở timeline")
        nameFilters: [qsTr("Timeline (*.json)")]
        currentFolder: Core.documentsFolder
        onAccepted: if (!root.tl.loadJson(selectedFile)) Core.console.error(root.tl.lastError)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Lưu timeline")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Timeline (*.json)")]
        currentFolder: Core.documentsFolder
        onAccepted: if (!root.tl.saveJson(selectedFile)) Core.console.error(root.tl.lastError)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Xuất quỹ đạo CSV")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)")]
        currentFolder: Core.documentsFolder
        onAccepted: root.traj.saveCsv(selectedFile)
    }
    ConfirmDialog { id: confirm }

    Popup {
        id: settingsPop
        x: 180; y: -4
        width: 250
        padding: 10
        background: Rectangle { color: Theme.bgDeep; radius: Theme.radius; border.color: Theme.border }
        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            FormRow {
                label: qsTr("Độ dài"); labelWidth: 70
                NumberField { Layout.fillWidth: true; value: root.tl.length; decimals: 2; from: 0.5; to: 3600; unit: "s"; onCommitted: (v) => root.tl.length = v }
            }
            FlatButton { Layout.fillWidth: true; text: qsTr("Mở timeline (.json)"); iconPath: Icons.folder; onClicked: { settingsPop.close(); openDialog.open() } }
            FlatButton { Layout.fillWidth: true; text: qsTr("Lưu timeline (.json)"); iconPath: Icons.save; onClicked: { settingsPop.close(); saveDialog.open() } }
            FlatButton { Layout.fillWidth: true; text: qsTr("Xuất CSV quỹ đạo"); iconPath: Icons.download; onClicked: { settingsPop.close(); exportDialog.open() } }
            FlatButton { Layout.fillWidth: true; text: qsTr("Nạp mẫu opposition"); onClicked: { settingsPop.close(); root.tl.makeDemo() } }
            FlatButton {
                Layout.fillWidth: true; text: qsTr("Xóa hết clip"); danger: true
                onClicked: { settingsPop.close(); confirm.ask(qsTr("Xóa toàn bộ clip trên timeline?"), function() { root.tl.clear() }, qsTr("Xóa")) }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- time header + ruler ------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                spacing: 0
                Item {
                    Layout.preferredWidth: root.labelWidth
                    Layout.fillHeight: true
                    RowLayout {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Rectangle {
                            Layout.preferredWidth: 54; Layout.preferredHeight: 20
                            radius: Theme.radiusSmall; color: Theme.bgSunken
                            TextLabel { anchors.centerIn: parent; text: root.player.position.toFixed(2); font.pixelSize: Theme.fontSmall }
                        }
                        TextLabel { text: "/ " + root.tl.length.toFixed(2) + "  s"; secondary: true; font.pixelSize: Theme.fontSmall }
                    }
                }
                Item {
                    id: ruler
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.bgSunken; radius: 2 }
                    Repeater {
                        model: Math.floor(root.tl.length / 0.1) + 1
                        Rectangle {
                            readonly property bool major: index % 10 === 0
                            readonly property bool mid: index % 5 === 0
                            x: index * 0.1 * root.pps - lanesView.contentX
                            y: parent.height - height
                            width: 1
                            height: major ? 12 : (mid ? 8 : 5)
                            color: major ? Theme.textSecondary : Theme.grid
                            visible: x >= 0 && x <= ruler.width
                            TextLabel {
                                visible: parent.major
                                x: 3; y: -14
                                text: (index / 10).toFixed(0) + "s"
                                font.pixelSize: Theme.fontTiny
                                secondary: true
                            }
                        }
                    }
                    // playhead marker (orange triangle)
                    Rectangle {
                        x: root.player.position * root.pps - lanesView.contentX - 5
                        y: parent.height - 16
                        width: 10; height: 8; radius: 1
                        color: Theme.playhead
                    }
                    MouseArea {
                        anchors.fill: parent
                        function go(mx) { root.player.seek(Math.max(0, (mx + lanesView.contentX) / root.pps)) }
                        onPressed: (m) => go(m.x)
                        onPositionChanged: (m) => { if (pressed) go(m.x) }
                    }
                }
            }

            // ---- tracks -----------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: root.rowHeight * Core.hand.count
                spacing: 0

                // labels
                Column {
                    Layout.preferredWidth: root.labelWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: 0
                    Repeater {
                        model: Core.hand.joints
                        Rectangle {
                            width: root.labelWidth
                            height: root.rowHeight
                            color: Theme.bgDeep
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.bgPanel }
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 10
                                spacing: 10
                                TextLabel { text: index + 1; width: 10; secondary: true }
                                Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.jointColor(index); anchors.verticalCenter: parent.verticalCenter }
                                TextLabel { text: modelData.name; width: root.labelWidth - 50 }
                            }
                        }
                    }
                }

                Flickable {
                    id: lanesView
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.rowHeight * Core.hand.count
                    Layout.alignment: Qt.AlignTop
                    contentWidth: Math.max(width, root.tl.length * root.pps + 2)
                    contentHeight: height
                    clip: true
                    interactive: false
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: FlatScrollBar {}
                    WheelHandler {
                        onWheel: (e) => {
                            if (e.modifiers & Qt.ControlModifier)
                                root.zoom = Math.max(1, Math.min(12, root.zoom * (e.angleDelta.y > 0 ? 1.2 : 1 / 1.2)))
                            else
                                lanesView.contentX = Math.max(0, Math.min(lanesView.contentWidth - lanesView.width, lanesView.contentX - e.angleDelta.y))
                        }
                    }

                    Item {
                        id: lanes
                        width: lanesView.contentWidth
                        height: root.rowHeight * Core.hand.count

                        // lane backgrounds
                        Repeater {
                            model: Core.hand.count
                            Rectangle {
                                y: index * root.rowHeight
                                width: lanes.width
                                height: root.rowHeight
                                color: Theme.bgSunken
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.bgPanel }
                                Rectangle {
                                    // end of timeline
                                    x: root.tl.length * root.pps
                                    width: Math.max(0, parent.width - x)
                                    height: parent.height
                                    color: Theme.bgDeep
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.tl.selected = -1
                                    onDoubleClicked: (m) => {
                                        var t = root.snapT(m.x / root.pps)
                                        var j = Core.hand.joints[index]
                                        var v = root.tl.valueAt(index, t)
                                        var target = v < (j.minDeg + j.maxDeg) / 2 ? j.maxDeg : j.minDeg
                                        var row = root.tl.addClip(index, t, 1.0, target, 1)
                                        if (row >= 0) root.tl.selected = row
                                    }
                                }
                            }
                        }

                        // clips
                        Repeater {
                            model: root.tl
                            Rectangle {
                                id: clipItem
                                readonly property bool sel: root.tl.selected === index
                                x: model.start * root.pps
                                y: model.track * root.rowHeight + 1
                                width: Math.max(4, model.duration * root.pps)
                                height: root.rowHeight - 3
                                radius: 2
                                color: sel ? Theme.clipSelected : Theme.clip
                                border.width: 1
                                border.color: sel ? Theme.textStrong : Theme.clipDark

                                Canvas {
                                    id: curve
                                    visible: root.showCurves && clipItem.width > 30
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    property real fromV: model.fromValue
                                    property real toV: model.target
                                    property int easing: model.easing
                                    onFromVChanged: requestPaint()
                                    onToVChanged: requestPaint()
                                    onEasingChanged: requestPaint()
                                    onWidthChanged: requestPaint()
                                    function ease(e, a) {
                                        if (e === 0) return a
                                        if (e === 2) return a * a
                                        if (e === 3) return 1 - (1 - a) * (1 - a)
                                        if (e === 4) return a > 0 ? 1 : 0
                                        return 0.5 - 0.5 * Math.cos(Math.PI * a)
                                    }
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        var j = Core.hand.joints[model.track]
                                        var lo = j.minDeg, hi = j.maxDeg
                                        ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.35)
                                        ctx.lineWidth = 1.2
                                        ctx.beginPath()
                                        for (var i = 0; i <= 40; ++i) {
                                            var a = i / 40
                                            var v = fromV + (toV - fromV) * ease(easing, a)
                                            var px = a * width
                                            var py = height - (v - lo) / Math.max(1e-6, hi - lo) * height
                                            if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                                        }
                                        ctx.stroke()
                                    }
                                }
                                Icon {
                                    x: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 14; height: 14
                                    visible: clipItem.width > 22
                                    path: root.easingIcons[model.easing]
                                    color: Theme.textStrong
                                }
                                TextLabel {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: clipItem.width > 70
                                    text: "→ " + Math.round(model.target) + "°"
                                    font.pixelSize: Theme.fontTiny
                                    color: Theme.textStrong
                                }

                                MouseArea {
                                    id: body
                                    anchors.fill: parent
                                    anchors.rightMargin: 6
                                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                    property real pressX: 0
                                    property real origStart: 0
                                    onPressed: (m) => {
                                        root.tl.selected = index
                                        pressX = mapToItem(lanes, m.x, m.y).x
                                        origStart = model.start
                                    }
                                    onPositionChanged: (m) => {
                                        if (!pressed) return
                                        var dx = mapToItem(lanes, m.x, m.y).x - pressX
                                        root.tl.moveClip(index, root.snapT(origStart + dx / root.pps))
                                    }
                                }
                                MouseArea {
                                    anchors.right: parent.right
                                    width: 7
                                    height: parent.height
                                    cursorShape: Qt.SizeHorCursor
                                    property real pressX: 0
                                    property real origDur: 0
                                    onPressed: (m) => {
                                        root.tl.selected = index
                                        pressX = mapToItem(lanes, m.x, m.y).x
                                        origDur = model.duration
                                    }
                                    onPositionChanged: (m) => {
                                        if (!pressed) return
                                        var dx = mapToItem(lanes, m.x, m.y).x - pressX
                                        root.tl.resizeClip(index, Math.max(0.1, root.snapT(origDur + dx / root.pps)))
                                    }
                                }
                            }
                        }

                        // playhead
                        Rectangle {
                            x: root.player.position * root.pps - 1
                            width: 2
                            height: lanes.height
                            color: Theme.playhead
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            TextLabel {
                Layout.fillWidth: true
                muted: true
                font.pixelSize: Theme.fontSmall
                text: qsTr("Nhấp đúp vào làn để thêm clip · kéo để dời · kéo mép phải để đổi độ dài · Delete để xóa · Ctrl+cuộn để zoom")
            }
        }

        // ---- inspector --------------------------------------------------
        Rectangle {
            id: inspector
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: Theme.bgSunken
            property int rev: 0
            readonly property var c: (rev, root.tl.selected >= 0 ? root.tl.clip(root.tl.selected) : ({}))
            readonly property var joint: c.track !== undefined ? Core.hand.joints[c.track] : null
            Connections {
                target: root.tl
                function onDataChanged() { inspector.rev++ }
                function onSelectedChanged() { inspector.rev++ }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                visible: root.tl.selected >= 0

                TextLabel { text: qsTr("Clip"); font.bold: true }
                TextLabel {
                    text: inspector.joint ? inspector.joint.name : ""
                    color: inspector.joint ? Theme.jointColor(inspector.c.track) : Theme.textPrimary
                }
                FormRow {
                    label: qsTr("Bắt đầu"); labelWidth: 64
                    NumberField { Layout.fillWidth: true; value: inspector.c.start || 0; decimals: 2; from: 0; to: 3600; unit: "s"; onCommitted: (v) => root.tl.moveClip(root.tl.selected, v) }
                }
                FormRow {
                    label: qsTr("Độ dài"); labelWidth: 64
                    NumberField { Layout.fillWidth: true; value: inspector.c.duration || 0; decimals: 2; from: 0.05; to: 3600; unit: "s"; onCommitted: (v) => root.tl.resizeClip(root.tl.selected, v) }
                }
                FormRow {
                    label: qsTr("Đích"); labelWidth: 64
                    NumberField { Layout.fillWidth: true; value: inspector.c.target || 0; decimals: 1; from: -180; to: 180; unit: "°"; onCommitted: (v) => root.tl.setClipTarget(root.tl.selected, v) }
                }
                FlatSlider {
                    Layout.fillWidth: true
                    from: inspector.joint ? inspector.joint.minDeg : 0
                    to: inspector.joint ? inspector.joint.maxDeg : 90
                    value: inspector.c.target || 0
                    fillColor: inspector.joint ? Theme.jointColor(inspector.c.track) : Theme.textSecondary
                    onMoved: root.tl.setClipTarget(root.tl.selected, value)
                }
                FormRow {
                    label: qsTr("Easing"); labelWidth: 64
                    FlatCombo {
                        Layout.fillWidth: true
                        model: root.tl.easingNames
                        currentIndex: inspector.c.easing !== undefined ? inspector.c.easing : 1
                        onActivated: (i) => root.tl.setClipEasing(root.tl.selected, i)
                    }
                }
                TextLabel {
                    text: qsTr("Từ %1° → %2°").arg(Math.round(inspector.c.fromValue || 0)).arg(Math.round(inspector.c.target || 0))
                    secondary: true
                    font.pixelSize: Theme.fontSmall
                }
                Item { Layout.fillHeight: true }
                FlatButton {
                    Layout.fillWidth: true
                    text: qsTr("Xóa clip")
                    danger: true
                    iconPath: Icons.trash
                    onClicked: root.tl.removeSelected()
                }
            }
            TextLabel {
                anchors.centerIn: parent
                width: parent.width - 20
                visible: root.tl.selected < 0
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideNone
                secondary: true
                font.pixelSize: Theme.fontSmall
                text: qsTr("Chọn một clip để sửa đích, độ dài và kiểu chuyển động")
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: root.visible && root.tl.selected >= 0
        onActivated: root.tl.removeSelected()
    }
}
