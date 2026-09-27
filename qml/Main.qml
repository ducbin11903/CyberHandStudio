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
            }
            IconButton {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 6
                iconPath: Icons.settings
                tip: qsTr("Mở thư mục cấu hình")
                onClicked: Core.config.openFolder()
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
