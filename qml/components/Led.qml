import QtQuick
import CyberHand

Rectangle {
    property bool on: false
    property color onColor: Theme.ledGreen
    property color offColor: Theme.textMuted
    implicitWidth: 9
    implicitHeight: 9
    radius: width / 2
    color: on ? onColor : offColor
    border.width: on ? 0 : 1
    border.color: Theme.grid
}
