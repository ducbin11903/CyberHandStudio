import QtQuick
import CyberHand

// Small dark box with a right-aligned value (Actual / Peak style).
Rectangle {
    id: root
    property string text
    property color textColor: Theme.textPrimary
    property int pixelSize: Theme.fontSize
    implicitWidth: 52
    implicitHeight: Theme.controlHeight
    radius: Theme.radiusSmall
    color: Theme.bgSunken
    Text {
        anchors.fill: parent
        anchors.rightMargin: 7
        anchors.leftMargin: 7
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
        text: root.text
        color: root.textColor
        font.family: Theme.fontFamily
        font.pixelSize: root.pixelSize
        elide: Text.ElideLeft
    }
}
