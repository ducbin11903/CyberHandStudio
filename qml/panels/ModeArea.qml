import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Bottom work area: mode selector + mode toolbar + mode content.
Card {
    id: root
    readonly property var modeKeys: ["text", "timeline", "controller", "stream", "live", "console"]
    readonly property var modeNames: ["Text", "Timeline", "Controller", "Stream", "Live", "Console"]
    property int current: Math.max(0, modeKeys.indexOf(Core.activeMode))

    function setMode(key) {
        var i = modeKeys.indexOf(key)
        if (i >= 0) Core.activeMode = key
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.bgPanel
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 8
                FlatCombo {
                    Layout.preferredWidth: 118
                    model: root.modeNames
                    currentIndex: root.current
                    onActivated: (i) => Core.activeMode = root.modeKeys[i]
                }
                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceComponent: stack.children[root.current] ? stack.children[root.current].toolbar : null
                    onLoaded: item.anchors.verticalCenter = verticalCenter
                }
            }
        }

        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.current
            TextMode {}
            TimelineMode {}
            ControllerMode {}
            StreamMode {}
            LiveMode {}
            ConsoleMode {}
        }
    }
}
