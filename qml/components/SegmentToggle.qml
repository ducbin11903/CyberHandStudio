import QtQuick
import QtQuick.Layouts
import CyberHand

// Two-or-more segment toggle (e.g. Graph | Bars).
Rectangle {
    id: root
    property var options: []
    property int currentIndex: 0
    signal activated(int index)
    implicitHeight: 22
    implicitWidth: row.implicitWidth + 4
    radius: Theme.radiusSmall
    color: Theme.bgSunken
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: root.options
            Rectangle {
                width: t.implicitWidth + 16
                height: 18
                radius: 2
                color: index === root.currentIndex ? Theme.bgDeep : (ma.containsMouse ? Theme.bgHover : "transparent")
                Text {
                    id: t
                    anchors.centerIn: parent
                    text: modelData
                    color: index === root.currentIndex ? Theme.textStrong : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { root.currentIndex = index; root.activated(index) }
                }
            }
        }
    }
}
