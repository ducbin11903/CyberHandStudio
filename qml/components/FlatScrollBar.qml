import QtQuick
import QtQuick.Controls.Basic
import CyberHand

ScrollBar {
    id: control
    implicitWidth: 8
    implicitHeight: 8
    padding: 1
    contentItem: Rectangle {
        radius: 3
        color: control.pressed ? Theme.textSecondary : Theme.grid
        opacity: control.policy === ScrollBar.AlwaysOn || (control.active && control.size < 1.0) ? 0.9 : 0.0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
    background: null
}
