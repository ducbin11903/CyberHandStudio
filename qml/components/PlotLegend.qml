import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand

// Channel legend with colored checkboxes (reference-app style).
Rectangle {
    id: root
    property var names: []
    property var labels: []          // optional display labels
    property var colors: []
    property var hidden: []
    signal toggled(string name, bool visible)
    implicitWidth: 150
    implicitHeight: col.implicitHeight + 12
    radius: Theme.radiusSmall
    color: Theme.bgSunken

    Flickable {
        anchors.fill: parent
        anchors.margins: 6
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: col
            width: parent.width
            spacing: 1
            Repeater {
                model: root.names
                FlatCheck {
                    width: col.width
                    text: root.labels.length > index ? root.labels[index] : modelData
                    boxColor: root.colors.length > index ? root.colors[index] : Theme.textPrimary
                    checked: root.hidden.indexOf(modelData) < 0
                    font.pixelSize: Theme.fontSmall
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: parent.boxColor
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 21
                        elide: Text.ElideRight
                    }
                    onToggled: root.toggled(modelData, checked)
                }
            }
        }
    }
}
