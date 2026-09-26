import QtQuick
import QtQuick.Controls.Basic
import CyberHand

// Flat dark button; `accent` = the green Connect style.
Button {
    id: control
    property bool accent: false
    property bool danger: false
    property string iconPath: ""
    property bool iconFilled: false
    property color textColor: accent ? Theme.textOnAccent : (danger ? Theme.liveRed : Theme.textPrimary)
    property string tip: ""

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(implicitHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: text.length ? 10 : 4
    rightPadding: text.length ? 10 : 4
    topPadding: 0
    bottomPadding: 0
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.4

    ToolTip.visible: tip.length > 0 && hovered
    ToolTip.text: tip
    ToolTip.delay: 500

    contentItem: Row {
        spacing: 6
        Icon {
            visible: control.iconPath.length > 0
            path: control.iconPath
            filled: control.iconFilled
            color: control.textColor
            width: 14; height: 14
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text.length > 0
            text: control.text
            font: control.font
            color: control.textColor
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: {
            if (control.accent)
                return control.down ? Qt.darker(Theme.accentGreen, 1.15) : (control.hovered ? Theme.accentGreenHover : Theme.accentGreen)
            if (control.checked)
                return Theme.bgDeep
            return control.down ? Theme.bgPressed : (control.hovered ? Theme.bgHover : Theme.bgSunken)
        }
        border.width: control.checked ? 1 : 0
        border.color: Theme.border
    }
}
