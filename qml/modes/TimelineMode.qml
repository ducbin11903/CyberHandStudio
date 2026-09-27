import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// TIMELINE mode: clip library | one track per joint | clip details.
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
    readonly property var kindIcons: [Icons.curve, Icons.hold, Icons.ramp, Icons.sine, Icons.square, Icons.triangle, Icons.sawtooth, Icons.easeIn]
    readonly property var shapeIcons: [Icons.easeIn, Icons.easeOut, Icons.inOut, Icons.outIn]
    property int activeTrack: 2

    function clipIcon(kind, easing, a, b, shape) {
        if (kind === 0) return easingIcons[easing]
        if (kind === 2 && b < a) return Icons.rampDown
        if (kind === 7) return shapeIcons[shape] || Icons.easeIn
        return kindIcons[kind] || Icons.curve
    }
    function clipLabel(m) {
        switch (m.kind) {
        case 0: return "→ " + Math.round(m.target) + "°"
        case 1: return Math.round(m.pa) + "°"
        case 2: case 7: return Math.round(m.pa) + "→" + Math.round(m.pb) + "°"
        default: return Math.round(m.pa) + "–" + Math.round(m.pb) + "° · " + Number(m.period).toFixed(2).replace(/\.?0+$/, "") + " s"
        }
    }
    // library drop: scene point -> track/time
    function dropAt(key, sx, sy) {
        var p = lanes.mapFromItem(null, sx, sy)
        if (p.x < 0 || p.y < 0 || p.x > lanes.width || p.y > lanes.height) return
        var track = Math.floor(p.y / root.rowHeight)
        var row = root.tl.addPreset(track, root.snapT(p.x / root.pps), key)
        if (row >= 0) { root.tl.selected = row; root.activeTrack = track }
        else Core.console.error(root.tl.lastError)
    }

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
            Item { Layout.fillWidth: true }
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
    TextPrompt {
        id: customPrompt
        property int row: -1
        onAccepted: (name) => { root.tl.saveCustom(row, name); Core.saveNow() }
    }
    Rectangle {
        id: dragProxy
        property string name
        property string icon
        visible: false
        z: 100
        width: 84; height: 28
        radius: Theme.radiusSmall
        color: Theme.clip
        opacity: 0.9
        Row {
            anchors.centerIn: parent
            spacing: 4
            Icon { width: 14; height: 14; path: Icons[dragProxy.icon] || Icons.curve; color: Theme.textStrong; anchors.verticalCenter: parent.verticalCenter }
            Text { text: dragProxy.name; color: Theme.textStrong; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSmall }
        }
    }

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

        ClipLibrary {
            Layout.preferredWidth: 176
            Layout.fillHeight: true
            onDragStarted: (key, name, icon) => { dragProxy.name = name; dragProxy.icon = icon; dragProxy.visible = true }
            onDragMoved: (sx, sy) => { var p = root.mapFromItem(null, sx, sy); dragProxy.x = p.x - dragProxy.width / 2; dragProxy.y = p.y - dragProxy.height / 2 }
            onDropped: (key, sx, sy) => { dragProxy.visible = false; root.dropAt(key, sx, sy) }
            onPicked: (key) => {
                var row = root.tl.addPreset(root.activeTrack, root.snapT(root.player.position), key)
                if (row >= 0) root.tl.selected = row
                else Core.console.error(root.tl.lastError)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- time header + ruler ------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.preferredHeight: 30
                Layout.maximumHeight: 30
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
                Layout.fillHeight: false
                Layout.preferredHeight: root.rowHeight * Core.hand.count
                Layout.maximumHeight: root.rowHeight * Core.hand.count + 10
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
                            color: index === root.activeTrack ? Theme.bgHover : Theme.bgDeep
                            MouseArea { anchors.fill: parent; onClicked: root.activeTrack = index }
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
                                Rectangle {
                                    // active track marker
                                    visible: index === root.activeTrack
                                    width: 2; height: parent.height
                                    color: Theme.jointColor(index)
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { root.tl.selected = -1; root.activeTrack = index }
                                    onDoubleClicked: (m) => {
                                        var row = root.tl.addPreset(index, root.snapT(m.x / root.pps), "move")
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
                                    property var values: model.curve
                                    property int track: model.track
                                    onValuesChanged: requestPaint()
                                    onWidthChanged: requestPaint()
                                    onTrackChanged: requestPaint()
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        var v = values
                                        if (!v || v.length < 2) return
                                        var j = Core.hand.joints[track]
                                        var lo = j.minDeg, hi = j.maxDeg
                                        ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.38)
                                        ctx.lineWidth = 1.2
                                        ctx.beginPath()
                                        for (var i = 0; i < v.length; ++i) {
                                            var px = i / (v.length - 1) * width
                                            var py = height - (v[i] - lo) / Math.max(1e-6, hi - lo) * height
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
                                    path: root.clipIcon(model.kind, model.easing, model.pa, model.pb, model.shape)
                                    color: Theme.textStrong
                                }
                                TextLabel {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: clipItem.width > 90
                                    text: root.clipLabel(model)
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
                                        root.activeTrack = model.track
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
            // ---- overview: whole timeline, drag the window to scroll ------------
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.preferredHeight: 30
                Layout.maximumHeight: 30
                spacing: 0
                TextLabel { Layout.preferredWidth: root.labelWidth; text: qsTr("Tổng quan"); muted: true; font.pixelSize: Theme.fontSmall }
                Rectangle {
                    id: overview
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: Theme.bgDeep
                    readonly property real sx: width / Math.max(0.5, root.tl.length)
                    Repeater {
                        model: root.tl
                        Rectangle {
                            x: model.start * overview.sx
                            y: 3 + model.track * ((overview.height - 6) / Core.hand.count)
                            width: Math.max(2, model.duration * overview.sx)
                            height: Math.max(2, (overview.height - 6) / Core.hand.count - 1)
                            color: Theme.jointColor(model.track)
                            opacity: root.tl.selected === index ? 1 : 0.75
                        }
                    }
                    Rectangle {
                        x: root.player.position * overview.sx
                        width: 1; height: parent.height
                        color: Theme.playhead
                    }
                    Rectangle {
                        id: viewWin
                        readonly property real frac: lanesView.width / Math.max(1, lanesView.contentWidth)
                        visible: frac < 0.999
                        x: lanesView.contentX / Math.max(1, lanesView.contentWidth) * overview.width
                        width: Math.max(8, frac * overview.width)
                        height: parent.height
                        color: Theme.alpha(Theme.textStrong, 0.08)
                        border.color: Theme.textSecondary
                        radius: Theme.radiusSmall
                    }
                    MouseArea {
                        anchors.fill: parent
                        function go(mx) {
                            var cw = lanesView.contentWidth
                            var cx = mx / overview.width * cw - lanesView.width / 2
                            lanesView.contentX = Math.max(0, Math.min(cw - lanesView.width, cx))
                        }
                        onPressed: (m) => go(m.x)
                        onPositionChanged: (m) => { if (pressed) go(m.x) }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            TextLabel {
                Layout.fillWidth: true
                muted: true
                font.pixelSize: Theme.fontSmall
                text: qsTr("Kéo clip từ thư viện vào làn · nhấp đúp làn để thêm Move · kéo để dời · kéo mép phải đổi độ dài · Delete xóa · Ctrl+D nhân bản · Ctrl+cuộn zoom")
            }
        }

        ClipDetails {
            Layout.preferredWidth: 264
            Layout.fillHeight: true
            onSaveCustomRequested: (row) => {
                customPrompt.row = row
                customPrompt.ask(qsTr("Tên clip Custom"), root.tl.kindNames[root.tl.clip(row).kind])
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: root.visible && root.tl.selected >= 0
        onActivated: root.tl.removeSelected()
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: root.visible && root.tl.selected >= 0
        onActivated: root.tl.duplicateClip(root.tl.selected)
    }
}
