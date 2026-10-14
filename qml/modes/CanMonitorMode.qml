import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// CAN MONITOR mode: raw frames on the bus (time, Rx/Tx, ID, length, FD/BRS, data, decoded),
// ID filter, pause, and a frame typed by hand.
Item {
    id: root
    readonly property var mon: Core.can.monitor
    property bool autoScroll: true

    function sendFrame() {
        if (Core.can.sendRaw(idField.text, dataField.text, fdCheck.checked, brsCheck.checked))
            sendError.text = ""
        else
            sendError.text = Core.can.errorString
    }

    property Component toolbar: Component {
        RowLayout {
            spacing: 6
            IconButton { iconPath: Icons.trash; tip: qsTr("Xóa"); onClicked: root.mon.clear() }
            IconButton {
                iconPath: root.mon.paused ? Icons.play : Icons.pause
                checked: root.mon.paused
                tip: root.mon.paused ? qsTr("Tiếp tục ghi khung") : qsTr("Tạm dừng ghi khung")
                onClicked: root.mon.paused = !root.mon.paused
            }
            FlatCheck { text: qsTr("Tự cuộn"); checked: root.autoScroll; onToggled: root.autoScroll = checked }
            VSep {}
            TextLabel { text: qsTr("Lọc ID"); secondary: true }
            FlatField {
                Layout.preferredWidth: 170
                font.family: Theme.monoFamily
                placeholderText: "281 282 300-3FF !080"
                text: root.mon.filter
                onEditingFinished: root.mon.filter = text
            }
            TextLabel { muted: true; text: qsTr("%1 khung").arg(root.mon.count) }
            Item { Layout.fillWidth: true }
            TextLabel {
                visible: Core.can.connected
                secondary: true
                color: Core.can.busProblem ? Theme.liveRed : (Core.can.statusLost > 0 ? Theme.warnAmber : Theme.textSecondary)
                text: qsTr("RX %1 / TX %2 khung/s · STATUS mất %3")
                      .arg(Math.round(Core.can.rxFramesPerSec)).arg(Math.round(Core.can.txFramesPerSec))
                      .arg(Core.can.statusLostText)
                      + (Core.can.busStateText.length ? " · " + Core.can.busStateText : "")
            }
        }
    }

    component Cell: Text {
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fontSmall
        color: Theme.textPrimary
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignVCenter
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        TextLabel {
            Layout.fillWidth: true
            visible: !Core.can.connected
            muted: true
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            text: Core.ctrl.connectionMode === 2
                  ? qsTr("CAN chưa kết nối — bấm Connect.")
                  : qsTr("Monitor dùng cho kết nối CAN: Communication → Kết nối → CAN.")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgSunken
            radius: Theme.radiusSmall

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Cell { Layout.preferredWidth: 70; text: qsTr("t (s)"); color: Theme.textMuted }
                    Cell { Layout.preferredWidth: 22; text: ""; color: Theme.textMuted }
                    Cell { Layout.preferredWidth: 64; text: "ID"; color: Theme.textMuted }
                    Cell { Layout.preferredWidth: 26; text: "len"; color: Theme.textMuted }
                    Cell { Layout.preferredWidth: 48; text: qsTr("kiểu"); color: Theme.textMuted }
                    Cell { Layout.fillWidth: true; text: "data"; color: Theme.textMuted }
                    Cell { Layout.preferredWidth: 300; text: qsTr("giải mã"); color: Theme.textMuted }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.mon
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: true
                    ScrollBar.vertical: FlatScrollBar { policy: ScrollBar.AsNeeded }
                    onCountChanged: if (root.autoScroll) Qt.callLater(positionViewAtEnd)
                    delegate: RowLayout {
                        width: ListView.view.width
                        height: 17
                        spacing: 8
                        readonly property bool tx: model.dir === "Tx"
                        Cell { Layout.preferredWidth: 70; text: model.time; color: Theme.textMuted }
                        Cell {
                            Layout.preferredWidth: 22
                            text: model.dir
                            color: parent.tx ? Theme.accentGreen : Theme.infoBlue
                        }
                        Cell {
                            Layout.preferredWidth: 64
                            text: model.frameId
                            font.bold: true
                            color: {
                                var id = parseInt(model.frameId, 16)
                                if (id === 0) return Theme.liveRed
                                if ((id & 0x780) === 0x280) return Theme.textStrong
                                if ((id & 0x700) === 0x300) return Theme.barText
                                return Theme.textPrimary
                            }
                        }
                        Cell { Layout.preferredWidth: 26; text: model.len; horizontalAlignment: Text.AlignRight }
                        Cell { Layout.preferredWidth: 48; text: model.flags; color: Theme.textSecondary }
                        Cell { Layout.fillWidth: true; text: model.hexData; color: Theme.textSecondary }
                        Cell { Layout.preferredWidth: 300; text: model.info }
                    }
                }
            }
        }

        // ---- one frame by hand ------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextLabel { text: "ID"; secondary: true }
            FlatField {
                id: idField
                Layout.preferredWidth: 70
                font.family: Theme.monoFamily
                text: "201"
                onAccepted: root.sendFrame()
            }
            TextLabel { text: "data"; secondary: true }
            FlatField {
                id: dataField
                Layout.fillWidth: true
                font.family: Theme.monoFamily
                placeholderText: qsTr("hex: 69 hoặc 69 00 … (FD: tự đệm 0 tới độ dài hợp lệ)")
                text: "69"
                onAccepted: root.sendFrame()
            }
            FlatCheck { id: fdCheck; text: "FD"; checked: true }
            FlatCheck { id: brsCheck; text: "BRS"; checked: true; enabled: fdCheck.checked }
            FlatButton {
                text: qsTr("Send")
                iconPath: Icons.send
                accent: true
                enabled: Core.can.connected
                onClicked: root.sendFrame()
            }
            VSep {}
            // presets: id, data, tooltip
            Repeater {
                model: [["701", "01", qsTr("ping U6")], ["702", "01", qsTr("ping U7")],
                        ["200", "73", qsTr("\"s\" cho cả 2 MCU: 1 dòng status dạng text")]]
                FlatButton {
                    text: modelData[0]
                    tip: modelData[2]
                    onClicked: { idField.text = modelData[0]; dataField.text = modelData[1] }
                }
            }
        }
        TextLabel {
            id: sendError
            Layout.fillWidth: true
            visible: text.length > 0
            color: Theme.liveRed
            font.pixelSize: Theme.fontSmall
        }
    }
}
