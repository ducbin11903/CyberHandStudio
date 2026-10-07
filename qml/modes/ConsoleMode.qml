import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// CONSOLE mode: firmware text lines, sent commands, command input with history.
Item {
    id: root
    readonly property var con: Core.console
    property var history: []
    property int histPos: -1
    property bool autoScroll: true

    function sendCurrent() {
        var t = input.text.trim()
        if (!t.length) return
        Core.ctrl.sendConsole(t)
        var h = history.slice()
        if (h.length === 0 || h[h.length - 1] !== t) h.push(t)
        if (h.length > 100) h.shift()
        history = h
        histPos = -1
        input.text = ""
    }

    property Component toolbar: Component {
        RowLayout {
            spacing: 6
            IconButton { iconPath: Icons.trash; tip: qsTr("Xóa"); onClicked: root.con.clear() }
            IconButton { iconPath: Icons.copy; tip: qsTr("Copy tất cả"); onClicked: root.con.copyAll() }
            IconButton { iconPath: Icons.save; tip: qsTr("Lưu log"); onClicked: saveDialog.open() }
            VSep {}
            FlatCheck { text: qsTr("Dòng log số"); checked: root.con.showTelemetry; onToggled: root.con.showTelemetry = checked }
            FlatCheck { text: qsTr("Lệnh stream"); checked: root.con.showStreamed; onToggled: root.con.showStreamed = checked }
            FlatCheck { text: qsTr("Tự cuộn"); checked: root.autoScroll; onToggled: root.autoScroll = checked }
            // U6 / U7 lines (2 ports or the fw 0.8 link)
            VSep { visible: root.con.sources.length > 0 }
            TextLabel { visible: root.con.sources.length > 0; text: qsTr("Nguồn"); secondary: true }
            FlatCombo {
                visible: root.con.sources.length > 0
                Layout.preferredWidth: 90
                model: [qsTr("Tất cả")].concat(root.con.sources)
                currentIndex: root.con.sourceFilter.length ? root.con.sources.indexOf(root.con.sourceFilter) + 1 : 0
                onActivated: (i) => root.con.sourceFilter = i === 0 ? "" : root.con.sources[i - 1]
            }
            TextLabel {
                visible: root.con.hiddenStreamed > 0 && !root.con.showStreamed
                muted: true
                text: qsTr("(%1 lệnh stream ẩn)").arg(root.con.hiddenStreamed)
            }
            Item { Layout.fillWidth: true }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Lưu log console")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text (*.txt *.log)")]
        currentFolder: Core.documentsFolder
        onAccepted: root.con.saveToFile(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgSunken
            radius: Theme.radiusSmall
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: root.con
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: FlatScrollBar { policy: ScrollBar.AsNeeded }
                onCountChanged: if (root.autoScroll) Qt.callLater(positionViewAtEnd)
                delegate: Row {
                    width: ListView.view.width
                    spacing: 10
                    readonly property bool tagged: root.con.sources.length > 0
                    Text {
                        text: model.time
                        color: Theme.textMuted
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSmall
                    }
                    Text {
                        text: ["", "›", "·", "!", "~"][model.kind]
                        width: 8
                        color: model.kind === 1 ? Theme.accentGreen : (model.kind === 3 ? Theme.liveRed : Theme.textMuted)
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSmall
                    }
                    Text {
                        visible: parent.tagged
                        width: 22
                        text: model.src
                        color: Theme.mcuColor(model.src)
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSmall
                    }
                    Text {
                        width: parent.width - (parent.tagged ? 152 : 120)
                        text: model.text
                        wrapMode: Text.WrapAnywhere
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSmall
                        color: {
                            switch (model.kind) {
                            case 1: return Theme.accentGreen
                            case 2: return Theme.infoBlue
                            case 3: return Theme.liveRed
                            case 4: return Theme.textMuted
                            }
                            var t = model.text
                            if (t.indexOf("ERR") === 0) return Theme.liveRed
                            if (t.indexOf("HOME:") === 0) return Theme.playhead
                            if (t.indexOf("OTEMP:") === 0 || t.indexOf("VBAT:") === 0 || t.indexOf("LINK:") === 0) return Theme.warnAmber
                            if (t.indexOf("OK") === 0) return Theme.textStrong
                            return Theme.textPrimary
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            FlatCombo {
                Layout.preferredWidth: 90
                model: ["LF", "CR", "CR+LF"]
                currentIndex: Core.link.lineEnding
                onActivated: (i) => Core.link.lineEnding = i
            }
            // 2 ports: where a typed line goes (auto = by command: g <j>, e/d/c/m <key>, @U7 ...)
            FlatCombo {
                visible: Core.ctrl.openPorts > 1
                Layout.preferredWidth: 96
                model: Core.ctrl.consoleTargets
                currentIndex: Core.ctrl.consoleTarget
                onActivated: (i) => Core.ctrl.consoleTarget = i
            }
            FlatField {
                id: input
                Layout.fillWidth: true
                font.family: Theme.monoFamily
                placeholderText: Core.ctrl.openPorts > 1 || root.con.sources.length > 0
                                 ? qsTr("Lệnh CLI · @U7 <lệnh> gửi thẳng U7 · ↑/↓ lịch sử")
                                 : qsTr("Lệnh CLI (h = help) · ↑/↓ lịch sử")
                onAccepted: root.sendCurrent()
                Keys.onUpPressed: {
                    if (!root.history.length) return
                    root.histPos = root.histPos < 0 ? root.history.length - 1 : Math.max(0, root.histPos - 1)
                    text = root.history[root.histPos]
                }
                Keys.onDownPressed: {
                    if (root.histPos < 0) return
                    root.histPos++
                    if (root.histPos >= root.history.length) { root.histPos = -1; text = "" }
                    else text = root.history[root.histPos]
                }
            }
            FlatButton { text: qsTr("Send"); iconPath: Icons.send; accent: true; onClicked: root.sendCurrent() }
            Repeater {
                model: ["h", "s", "i", "e", "d"]
                FlatButton { text: modelData; onClicked: Core.ctrl.sendConsole(modelData) }
            }
        }
    }
}
