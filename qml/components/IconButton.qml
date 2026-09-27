import QtQuick
import QtQuick.Controls.Basic
import CyberHand

// Transparent square button with a line icon (toolbars).
AbstractButton {
    id: control
    property string iconPath
    property bool iconFilled: false
    property color iconColor: control.checked ? Theme.textStrong : Theme.textPrimary
    property int iconSize: 16
    property string tip: ""
    implicitWidth: 26
    implicitHeight: 26
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.35

    Tip { visible: control.tip.length > 0 && control.hovered; text: control.tip }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down ? Theme.bgPressed : (control.checked ? Theme.bgDeep : (control.hovered ? Theme.bgHover : "transparent"))
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            width: control.iconSize; height: control.iconSize
            path: control.iconPath
            filled: control.iconFilled
            color: control.iconColor
        }
    }
}
