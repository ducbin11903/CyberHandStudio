import QtQuick
import QtQuick.Controls.Basic
import CyberHand

Switch {
    id: control
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    padding: 0
    spacing: 8
    focusPolicy: Qt.NoFocus
    implicitHeight: 20
    indicator: Rectangle {
        implicitWidth: 28; implicitHeight: 14
        x: control.text.length ? control.width - width : 0
        anchors.verticalCenter: parent.verticalCenter
        radius: 7
        color: control.checked ? Theme.alpha(Theme.accentGreen, 0.35) : Theme.bgSunken
        border.width: 1
        border.color: control.checked ? Theme.accentGreen : Theme.grid
        Rectangle {
            width: 10; height: 10; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - 2 : 2
            color: control.checked ? Theme.accentGreen : Theme.textSecondary
            Behavior on x { NumberAnimation { duration: 90 } }
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.textPrimary
        verticalAlignment: Text.AlignVCenter
        rightPadding: control.indicator.width + control.spacing
    }
}
