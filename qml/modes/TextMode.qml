import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// TEXT mode: import a CSV trajectory (or ask the AI for one), plot it, play it.
Item {
    id: root
    readonly property var traj: Core.textTrajectory
    readonly property var player: Core.player
    property var hidden: []
    property bool aiOpen: false
    property real zoom: 1.0

    property Component toolbar: Component {
        RowLayout {
            spacing: 4
            FlatField {
                Layout.preferredWidth: 190
                text: root.traj.name
                onEditingFinished: root.traj.name = text
            }
            IconButton { iconPath: Icons.folder; tip: qsTr("Mở CSV"); onClicked: openDialog.open() }
            IconButton { iconPath: Icons.save; tip: qsTr("Lưu CSV"); onClicked: saveDialog.open() }
            FlatCombo {
                id: demoCombo
                Layout.preferredWidth: 120
                model: [qsTr("Mẫu…")].concat(["hand_wave_staggered", "grasp_release", "finger_count"])
                onActivated: (i) => { if (i > 0) root.traj.makeDemo(i - 1); currentIndex = 0 }
            }
            IconButton {
                iconPath: Icons.sparkle
                checkable: true
                checked: root.aiOpen
                iconColor: checked ? Theme.playhead : Theme.textPrimary
                tip: qsTr("Tạo quỹ đạo bằng AI")
                onToggled: root.aiOpen = checked
            }
            VSep {}
            TransportBar { player: root.player }
            VSep {}
            IconButton { iconPath: Icons.fit; tip: qsTr("Vừa khung"); onClicked: root.zoom = 1.0 }
            IconButton { iconPath: Icons.minusCircle; tip: qsTr("Thu nhỏ"); onClicked: root.zoom = Math.max(1, root.zoom / 1.5) }
            FlatSlider {
                Layout.preferredWidth: 90
                from: 1; to: 20
                value: root.zoom
                onMoved: root.zoom = value
            }
            IconButton { iconPath: Icons.plusCircle; tip: qsTr("Phóng to"); onClicked: root.zoom = Math.min(20, root.zoom * 1.5) }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Mở quỹ đạo CSV")
        nameFilters: [qsTr("CSV (*.csv *.txt)"), qsTr("Tất cả (*)")]
        currentFolder: Core.documentsFolder
        onAccepted: {
            if (!root.traj.loadCsv(selectedFile)) Core.console.error(root.traj.lastError)
            root.player.toStart()
        }
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Lưu quỹ đạo CSV")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)")]
        currentFolder: Core.documentsFolder
        onAccepted: if (!root.traj.saveCsv(selectedFile)) Core.console.error(root.traj.lastError)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        ScrubBar {
            position: root.player.position
            duration: root.traj.duration
            onSeek: (t) => root.player.seek(t)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                PlotItem {
                    id: plot
                    anchors.fill: parent
                    source: root.traj
                    colors: Theme.jointColors
                    hiddenChannels: root.hidden
                    backgroundColor: Theme.bgPanel
                    gridColor: Theme.grid
                    textColor: Theme.textPrimary
                    xLabel: qsTr("Time")
                    yLabel: qsTr("Góc (°)")
                    yMin: 0
                    yMax: 90
                    yDivisions: 6
                    // zoom keeps the playhead in view
                    readonly property real span: Math.max(0.5, root.traj.duration / root.zoom)
                    xMin: root.zoom <= 1.001 ? 0 : Math.max(0, Math.min(root.traj.duration - span, root.player.position - span / 2))
                    xMax: xMin + (root.zoom <= 1.001 ? Math.max(0.5, root.traj.duration) : span)
                }
                // playhead
                Rectangle {
                    visible: root.traj.duration > 0
                    x: plot.plotLeft + (root.player.position - plot.xMin) / Math.max(1e-9, plot.xMax - plot.xMin) * plot.plotWidth - 1
                    y: plot.plotTop - 4
                    width: 2
                    height: plot.plotHeight + 4
                    color: Theme.playhead
                    Rectangle {
                        width: 10; height: 8
                        x: -4; y: -2
                        color: Theme.playhead
                        radius: 1
                    }
                }
                MouseArea {
                    x: plot.plotLeft; y: plot.plotTop
                    width: plot.plotWidth; height: plot.plotHeight
                    cursorShape: Qt.SizeHorCursor
                    function go(mx) { root.player.seek(Math.max(0, plot.pixelToX(mx + plot.plotLeft))) }
                    onPressed: (m) => go(m.x)
                    onPositionChanged: (m) => { if (pressed) go(m.x) }
                }
                TextLabel {
                    anchors.centerIn: parent
                    visible: root.traj.empty
                    text: qsTr("Mở file CSV (t, T2, T1, I, M, R, P) hoặc chọn mẫu")
                    secondary: true
                }
            }

            PlotLegend {
                Layout.preferredWidth: 150
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: implicitHeight
                names: Core.hand.keys
                labels: Core.hand.names
                colors: Theme.jointColors
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

        // ---- AI prompt ---------------------------------------------------
        Rectangle {
            visible: root.aiOpen
            Layout.fillWidth: true
            Layout.preferredHeight: aiCol.implicitHeight + 16
            radius: Theme.radiusSmall
            color: Theme.bgSunken

            ColumnLayout {
                id: aiCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                RowLayout {
                    spacing: 6
                    Icon { path: Icons.sparkle; color: Theme.playhead; width: 14; height: 14 }
                    TextLabel { text: qsTr("AI tạo quỹ đạo"); font.bold: true }
                    Item { Layout.fillWidth: true }
                    FlatCombo {
                        Layout.preferredWidth: 170
                        model: Core.llm.providerNames
                        currentIndex: Core.llm.provider
                        onActivated: (i) => Core.llm.provider = i
                    }
                    FlatField {
                        Layout.preferredWidth: 150
                        placeholderText: qsTr("model")
                        text: Core.llm.model
                        onEditingFinished: Core.llm.model = text
                    }
                    FlatField {
                        Layout.preferredWidth: 180
                        placeholderText: qsTr("API key")
                        echoMode: TextInput.Password
                        text: Core.llm.apiKey
                        onEditingFinished: Core.llm.apiKey = text
                    }
                    FlatField {
                        Layout.preferredWidth: 220
                        placeholderText: qsTr("endpoint")
                        text: Core.llm.endpoint
                        onEditingFinished: Core.llm.endpoint = text
                    }
                }
                RowLayout {
                    spacing: 6
                    FlatField {
                        id: promptField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Mô tả chuyển động, vd: đếm 1-2-3 bằng ngón trỏ, giữa, áp út rồi nắm tay")
                        onAccepted: genBtn.clicked()
                    }
                    NumberField { id: durField; Layout.preferredWidth: 70; value: 6; decimals: 1; from: 0; to: 120; unit: "s" }
                    FlatButton {
                        id: genBtn
                        text: Core.llm.busy ? qsTr("Đang tạo…") : qsTr("Generate")
                        accent: !Core.llm.busy
                        iconPath: Icons.sparkle
                        enabled: promptField.text.length > 0
                        onClicked: Core.llm.busy ? Core.llm.cancel() : Core.llm.generate(promptField.text, durField.value)
                    }
                }
                TextLabel {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: Core.llm.lastError.length ? Core.llm.lastError : ""
                    color: Theme.liveRed
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }
}
