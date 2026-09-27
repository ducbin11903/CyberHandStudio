import QtQuick
import CyberHand
import CyberHand.Core

// Big run-state indicator: OFFLINE / CONNECTED / IDLE / READY / RUN / FAULT.
Rectangle {
    id: root
    property string runState: Core.runState
    property bool compact: false
    readonly property color tone: {
        switch (runState) {
        case "RUN": return Theme.accentGreen
        case "READY": return Theme.okGreen
        case "IDLE": return Theme.infoBlue
        case "CONNECTED": return Theme.infoBlue
        case "FAULT": return Theme.liveRed
        default: return Theme.textMuted
        }
    }
    implicitWidth: compact ? 74 : 104
    implicitHeight: compact ? 20 : 26
    radius: height / 2
    color: Theme.alpha(tone, 0.16)
    border.width: 1
    border.color: tone

    Row {
        anchors.centerIn: parent
        spacing: 6
        Rectangle {
            id: dot
            width: root.compact ? 7 : 9
            height: width
            radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: root.tone
            SequentialAnimation on opacity {
                running: root.runState === "RUN" || root.runState === "FAULT"
                loops: Animation.Infinite
                NumberAnimation { to: 0.25; duration: 450 }
                NumberAnimation { to: 1; duration: 450 }
                onRunningChanged: if (!running) dot.opacity = 1
            }
        }
        Text {
            text: root.runState
            color: root.tone
            font.family: Theme.fontFamily
            font.pixelSize: root.compact ? Theme.fontTiny : Theme.fontSmall
            font.bold: true
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
