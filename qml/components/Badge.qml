import QtQuick
import CyberHand

Rectangle {
    id: root
    property string text
    property color fill: Theme.infoBlue
    property color textColor: Theme.textOnAccent
    implicitWidth: label.implicitWidth + 14
    implicitHeight: 20
    radius: Theme.radiusSmall
    color: fill
    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.textColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        font.bold: true
    }
}
