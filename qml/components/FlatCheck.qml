import QtQuick
import QtQuick.Controls.Basic
import CyberHand

CheckBox {
    id: control
    property color boxColor: Theme.textPrimary
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    spacing: 7
    padding: 0
    implicitHeight: 20
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.45
    indicator: Rectangle {
        width: 14; height: 14
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        radius: 2
        color: control.checked ? control.boxColor : Theme.bgSunken
        border.width: control.checked ? 0 : 1
        border.color: Theme.grid
        Icon {
            anchors.centerIn: parent
            width: 12; height: 12
            visible: control.checked
            path: Icons.check
            stroke: 2.4
            color: Theme.bgDeep
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.textPrimary
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.text.length ? control.indicator.width + control.spacing : control.indicator.width
    }
}
