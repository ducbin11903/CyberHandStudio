import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import CyberHand
import CyberHand.Core

ApplicationWindow {
    id: win
    visible: true
    width: Math.min(1500, Screen.desktopAvailableWidth - 40)
    height: Math.min(940, Screen.desktopAvailableHeight - 60)
    minimumWidth: 1100
    minimumHeight: 640
    title: "CyberHand Studio " + Core.version + (Core.config.current.length ? " — " + Core.config.current : "")
    color: Theme.bgWindow
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize

    property bool leftOpen: true

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap

        // ---- icon strip --------------------------------------------------
        Card {
            Layout.preferredWidth: 38
            Layout.fillHeight: true
            ColumnLayout {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 6
                spacing: 4
                IconButton { iconPath: Icons.menu; checkable: true; checked: win.leftOpen; tip: qsTr("Ẩn/hiện bảng cài đặt"); onClicked: win.leftOpen = !win.leftOpen }
                IconButton { iconPath: Icons.pencil; tip: qsTr("Timeline"); checked: Core.activeMode === "timeline"; onClicked: Core.activeMode = "timeline" }
                IconButton { iconPath: Icons.plusCircle; tip: qsTr("Text / quỹ đạo"); checked: Core.activeMode === "text"; onClicked: Core.activeMode = "text" }
                IconButton { iconPath: Icons.gamepad; tip: qsTr("Controller"); checked: Core.activeMode === "controller"; onClicked: Core.activeMode = "controller" }
                IconButton { iconPath: Icons.wifi; tip: qsTr("Stream"); checked: Core.activeMode === "stream"; onClicked: Core.activeMode = "stream" }
                IconButton { iconPath: Icons.activity; tip: qsTr("Live log"); checked: Core.activeMode === "live"; onClicked: Core.activeMode = "live" }
                IconButton { iconPath: Icons.terminal; tip: qsTr("Console"); checked: Core.activeMode === "console"; onClicked: Core.activeMode = "console" }
                IconButton {
                    visible: Core.ctrl.connectionMode === 2 || Core.activeMode === "canmon"
                    iconPath: Icons.canbus
                    tip: qsTr("CAN monitor")
                    checked: Core.activeMode === "canmon"
                    onClicked: Core.activeMode = "canmon"
                }
            }
            ColumnLayout {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 6
                spacing: 4
                IconButton { iconPath: Icons.sliders; tip: qsTr("Manual Control"); checked: manualWin.visible; onClicked: manualWin.toggle() }
                IconButton { iconPath: Icons.target; tip: qsTr("Calibration"); checked: calibWin.visible; onClicked: calibWin.toggle() }
                IconButton { iconPath: Icons.table; tip: qsTr("Data Monitor"); checked: dataWin.visible; onClicked: dataWin.toggle() }
                Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 1; Layout.alignment: Qt.AlignHCenter; color: Theme.divider }
                IconButton {
                    iconPath: Icons.download
                    tip: Core.updater.available ? qsTr("Có bản mới — Cập nhật") : qsTr("Cập nhật phần mềm")
                    onClicked: updateDlg.openAndCheck()
                    Rectangle {
                        visible: Core.updater.available
                        width: 7; height: 7; radius: 3.5
                        color: Theme.accentGreen
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 3
                    }
                }
                IconButton {
                    iconPath: Icons.settings
                    tip: qsTr("Mở thư mục cấu hình")
                    onClicked: Core.config.openFolder()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gap

            // ---- update: a newer build on GitHub / result after the restart ----------
            Rectangle {
                id: updBanner
                property bool dismissed: false
                readonly property bool after: Core.updater.startMessage.length > 0
                visible: after || (Core.updater.available && !dismissed)
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: Theme.radius
                color: after && Core.updater.startFailed ? Theme.liveRed : Theme.barFill
                Timer {
                    running: updBanner.after && !Core.updater.startFailed
                    interval: 10000
                    onTriggered: Core.updater.clearStartMessage()
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 10
                    Icon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        path: updBanner.after ? (Core.updater.startFailed ? Icons.alert : Icons.check) : Icons.download
                        color: Theme.textStrong
                    }
                    Text {
                        Layout.fillWidth: true
                        text: updBanner.after ? Core.updater.startMessage : qsTr("Có bản mới: %1").arg(Core.updater.latestText)
                        color: Theme.textStrong
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize + 1
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    FlatButton {
                        visible: !updBanner.after
                        text: qsTr("Cập nhật")
                        iconPath: Icons.download
                        onClicked: updateDlg.openAndCheck()
                    }
                    IconButton {
                        iconPath: Icons.close
                        tip: qsTr("Ẩn")
                        onClicked: updBanner.after ? Core.updater.clearStartMessage() : (updBanner.dismissed = true)
                    }
                }
            }

            // ---- board protection banner (firmware switched all motors off) -----
            Repeater {
                model: ["otemp", "vbat"]
                Rectangle {
                    id: banner
                    readonly property int st: modelData === "otemp" ? Core.board.otempState : Core.board.vbatState
                    readonly property string msg: modelData === "otemp" ? Core.board.otempText : Core.board.vbatText
                    visible: st > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: Theme.radius
                    color: st === 2 ? Theme.liveRed : Theme.barFill
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 10
                        Icon {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            path: banner.st === 2 ? Icons.alert : Icons.check
                            color: Theme.textStrong
                        }
                        Text {
                            Layout.fillWidth: true
                            text: banner.msg
                            color: Theme.textStrong
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize + 2
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: banner.st === 2
                            text: modelData === "otemp"
                                  ? (isNaN(Core.board.tOn) ? "" : qsTr("chạy lại khi < %1 °C").arg(Core.board.tOn.toFixed(1)))
                                  : (isNaN(Core.board.vOn) ? "" : qsTr("chạy lại khi ≥ %1 V").arg(Core.board.vOn.toFixed(2)))
                            color: Theme.textStrong
                            opacity: 0.8
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }
                        // safe again: the user presses Enable (the app never sends e by itself)
                        FlatButton {
                            visible: banner.st === 1
                            text: qsTr("Enable")
                            iconPath: Icons.power
                            lockReason: Core.board.blockReason
                            onClicked: Core.ctrl.enable()
                        }
                    }
                }
            }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Vertical
                handle: Rectangle {
                    implicitHeight: Theme.gap
                    color: SplitHandle.pressed ? Theme.grid : (SplitHandle.hovered ? Theme.bgPanel : Theme.bgWindow)
                }

                RowLayout {
                    SplitView.preferredHeight: win.height * 0.5
                    SplitView.minimumHeight: 300
                    spacing: Theme.gap
                    LeftPanel {
                        visible: win.leftOpen
                        Layout.preferredWidth: 420
                        Layout.fillHeight: true
                    }
                    HandView3D {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    StatusPanel {
                        Layout.preferredWidth: 280
                        Layout.fillHeight: true
                    }
                }

                ModeArea {
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 260
                }
            }
        }
    }

    ManualControlWindow { id: manualWin; transientParent: null }
    CalibrationWindow { id: calibWin; transientParent: null }
    DataMonitorWindow { id: dataWin; transientParent: null }
    UpdateDialog { id: updateDlg }
    // for --eval in screenshots / tests
    function openTool(name) {
        if (name === "manual") manualWin.show()
        else if (name === "calibration") calibWin.show()
        else if (name === "datamonitor") dataWin.show()
    }

    // E-STOP: CAN frame 0x000 / D to every port, from anywhere in the app
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: Core.ctrl.connected
        onActivated: Core.estop()
    }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: (Core.activeMode === "text" || Core.activeMode === "timeline")
                 && !(win.activeFocusItem && win.activeFocusItem.hasOwnProperty("cursorPosition"))
        onActivated: Core.player.toggle()
    }

    Component.onCompleted: {
        if (Screen.desktopAvailableWidth < 1600 || Screen.desktopAvailableHeight < 1000)
            win.showMaximized()
        if (StartMode && StartMode.length)
            Core.activeMode = StartMode
        else if (Core.ui.mode)
            Core.activeMode = Core.ui.mode
    }
    Connections {
        target: Core
        function onActiveModeChanged() { Core.setUiValue("mode", Core.activeMode) }
    }
}
