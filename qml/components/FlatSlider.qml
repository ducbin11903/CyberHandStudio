import QtQuick
import QtQuick.Controls.Basic
import CyberHand

Slider {
    id: control
    property color fillColor: Theme.textSecondary
    property bool centered: false   // fill from the middle (for -100..100)
    implicitHeight: orientation === Qt.Horizontal ? 20 : 120
    implicitWidth: orientation === Qt.Horizontal ? 120 : 20
    padding: 0
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.45

    background: Rectangle {
        x: control.orientation === Qt.Horizontal ? control.leftPadding : (control.width - width) / 2
        y: control.orientation === Qt.Horizontal ? (control.height - height) / 2 : control.topPadding
        width: control.orientation === Qt.Horizontal ? control.availableWidth : 4
        height: control.orientation === Qt.Horizontal ? 4 : control.availableHeight
        radius: 2
        color: Theme.bgDeep
        Rectangle {
            // vertical sliders fill up from the bottom (visualPosition 1 = bottom)
            readonly property real p0: control.centered ? 0.5 : (control.orientation === Qt.Vertical ? 1 : 0)
            readonly property real p1: control.visualPosition
            readonly property real lo: Math.min(p0, p1)
            readonly property real hi: Math.max(p0, p1)
            radius: 2
            color: control.fillColor
            x: control.orientation === Qt.Horizontal ? lo * parent.width : 0
            width: control.orientation === Qt.Horizontal ? (hi - lo) * parent.width : parent.width
            y: control.orientation === Qt.Horizontal ? 0 : lo * parent.height
            height: control.orientation === Qt.Horizontal ? parent.height : (hi - lo) * parent.height
        }
    }
    handle: Rectangle {
        x: control.orientation === Qt.Horizontal ? control.leftPadding + control.visualPosition * (control.availableWidth - width) : (control.width - width) / 2
        y: control.orientation === Qt.Horizontal ? (control.height - height) / 2 : control.topPadding + control.visualPosition * (control.availableHeight - height)
        width: 12; height: 12; radius: 6
        color: control.pressed ? Theme.textStrong : "#CFCFCF"
    }
}
