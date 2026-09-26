import QtQuick
import QtQuick.Controls.Basic
import CyberHand

TextField {
    id: control
    implicitHeight: Theme.controlHeight
    implicitWidth: 90
    leftPadding: 7
    rightPadding: 7
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.clip
    selectedTextColor: Theme.textStrong
    selectByMouse: true
    opacity: enabled ? 1 : 0.45
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.bgSunken
        border.width: control.activeFocus ? 1 : 0
        border.color: Theme.grid
    }
}
