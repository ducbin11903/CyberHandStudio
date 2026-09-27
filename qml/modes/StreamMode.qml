import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// STREAM mode: real-time joint input over UDP (script, glove, AI agent), bars or graph view.
Item {
    id: root
    readonly property var st: Core.stream
    property int view: 1 // 0 graph, 1 bars

    property Component toolbar: Component {
        RowLayout {
            spacing: 8
            FlatButton {
                text: root.st.listening ? qsTr("Stop") : qsTr("Start")
                accent: !root.st.listening
                onClicked: root.st.toggle()
            }
            TextLabel { text: qsTr("Live") }
            Rectangle {
                width: 10; height: 10; radius: 5
                color: root.st.listening && root.st.ageMs >= 0 && root.st.ageMs < 500 ? Theme.liveRed : Theme.textMuted
            }
            VSep {}
            FlatCheck {
                text: qsTr("Nguồn demo (sóng sin)")
                checked: root.st.demoSource
                onToggled: root.st.demoSource = checked
            }
            TextLabel {
                visible: root.st.lastError.length > 0
                text: root.st.lastError
                color: Theme.liveRed
            }
            Item { Layout.fillWidth: true }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // ---- left settings column ------------------------------------------
        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: Theme.bgPanel
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
                    spacing: 1
                    Section {
                        title: qsTr("Communication")
                        FormRow {
                            label: qsTr("UDP port"); labelWidth: 80
                            NumberField { Layout.fillWidth: true; value: root.st.port; decimals: 0; from: 1; to: 65535; onCommitted: (v) => root.st.port = v }
                        }
                        FormRow {
                            label: qsTr("Đơn vị"); labelWidth: 80
                            FlatCombo { Layout.fillWidth: true; model: root.st.unitNames; currentIndex: root.st.unit; onActivated: (i) => root.st.unit = i }
                        }
                        FlatSwitch {
                            Layout.fillWidth: true
                            text: qsTr("Mirror Enabled")
                            checked: root.st.mirror
                            onToggled: root.st.mirror = checked
                        }
                        TextLabel {
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontTiny
                            muted: true
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            text: root.st.mirror ? qsTr("Đang gửi khung nhận được tới tay") : qsTr("Chỉ hiển thị, chưa gửi tới tay")
                        }
                    }
                    Section {
                        title: qsTr("Details")
                        FormRow { label: qsTr("FPS"); labelWidth: 80; ValueBox { Layout.fillWidth: true; text: root.st.fps.toFixed(0) } }
                        FormRow { label: qsTr("Frames"); labelWidth: 80; ValueBox { Layout.fillWidth: true; text: root.st.frames } }
                        FormRow {
                            label: qsTr("Nguồn"); labelWidth: 80
                            ValueBox { Layout.fillWidth: true; text: root.st.lastSender.length ? root.st.lastSender : "—"; pixelSize: Theme.fontSmall }
                        }
                    }
                    Section {
                        title: qsTr("Calibration")
                        RowLayout {
                            Layout.fillWidth: true
                            FlatButton {
                                Layout.fillWidth: true
                                text: root.st.calibrating ? qsTr("Đang ghi min/max…") : qsTr("Start")
                                checked: root.st.calibrating
                                onClicked: root.st.startCalibration()
                            }
                        }
                        TextLabel {
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontTiny
                            muted: true
                            text: root.st.calibratedAt.length ? qsTr("Last Calibrated: %1").arg(root.st.calibratedAt) : qsTr("Chưa hiệu chuẩn")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FlatButton { Layout.fillWidth: true; text: qsTr("Save"); enabled: root.st.calibrating; onClicked: { root.st.finishCalibration(); Core.saveNow() } }
                            FlatButton { Layout.fillWidth: true; text: qsTr("Clear"); onClicked: root.st.clearCalibration() }
                        }
                    }
                    Section {
                        title: qsTr("Poses")
                        Repeater {
                            model: Core.poses
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 26
                                radius: Theme.radiusSmall
                                color: pm.containsMouse ? Theme.bgHover : Theme.bgSunken
                                TextLabel { x: 8; anchors.verticalCenter: parent.verticalCenter; text: model.name }
                                Rectangle {
                                    width: 16; height: 16; radius: 8
                                    anchors.right: parent.right; anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: Theme.okGreen
                                    Icon { anchors.centerIn: parent; width: 11; height: 11; path: Icons.check; stroke: 2.4; color: Theme.bgDeep }
                                }
                                MouseArea { id: pm; anchors.fill: parent; hoverEnabled: true; onClicked: Core.poses.apply(index) }
                            }
                        }
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTr("Lưu khung hiện tại thành pose")
                            iconPath: Icons.plus
                            onClicked: { Core.poses.capture("Pose " + (Core.poses.count + 1)); Core.saveNow() }
                        }
                    }
                    Item { Layout.preferredHeight: 6 }
                }
            }
        }

        // ---- bars / graph -----------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                TextLabel {
                    text: qsTr("Gửi mẫu tới UDP %1: {\"q\":[0,0,45,90,90,90]} · \"0,0,45,90,90,90\" · \"I:45,M:90\"").arg(root.st.port)
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    Layout.fillWidth: true
                }
                SegmentToggle {
                    options: ["Graph", "Bars"]
                    currentIndex: root.view
                    onActivated: (i) => root.view = i
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.view

                PlotItem {
                    source: Core.streamSeries
                    followLatest: true
                    window: 10
                    colors: Theme.jointColors
                    backgroundColor: Theme.bgPanel
                    gridColor: Theme.grid
                    textColor: Theme.textPrimary
                    yMin: 0; yMax: 90; yDivisions: 6
                    xLabel: qsTr("Time (s)")
                    yLabel: qsTr("Góc (°)")
                }

                // bars
                Rectangle {
                    color: Theme.bgSunken
                    radius: Theme.radiusSmall
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 0
                        Repeater {
                            model: Core.hand.joints
                            ColumnLayout {
                                id: barCol
                                readonly property var outVal: root.st.output[index]
                                readonly property bool has: outVal !== undefined && outVal !== null
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 3
                                TextLabel { text: index + 1; Layout.alignment: Qt.AlignHCenter; font.bold: true }
                                TextLabel { text: modelData.name; Layout.alignment: Qt.AlignHCenter; font.pixelSize: Theme.fontSmall }
                                TextLabel { text: Math.round(modelData.maxDeg); Layout.alignment: Qt.AlignHCenter; font.pixelSize: Theme.fontSmall; secondary: true }
                                Item {
                                    Layout.fillHeight: true
                                    Layout.preferredWidth: 60
                                    Layout.alignment: Qt.AlignHCenter
                                    readonly property real frac: barCol.has ? Math.max(0, Math.min(1, (barCol.outVal - modelData.minDeg) / Math.max(1e-6, modelData.maxDeg - modelData.minDeg))) : 0
                                    Rectangle {
                                        id: barTrack
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 8
                                        height: parent.height
                                        radius: 2
                                        color: Theme.bgDeep
                                    }
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.bottom
                                        width: 8
                                        height: parent.frac * parent.height
                                        radius: 2
                                        color: Theme.barFill
                                    }
                                    TextLabel {
                                        x: parent.width / 2 + 8
                                        y: Math.min(parent.height - 14, (1 - parent.frac) * parent.height - 7)
                                        text: barCol.has ? Math.round(barCol.outVal) : "—"
                                        color: Theme.barText
                                        font.pixelSize: Theme.fontSmall
                                    }
                                }
                                TextLabel { text: Math.round(modelData.minDeg); Layout.alignment: Qt.AlignHCenter; font.pixelSize: Theme.fontSmall; secondary: true }
                            }
                        }
                    }
                }
            }
        }
    }
}
