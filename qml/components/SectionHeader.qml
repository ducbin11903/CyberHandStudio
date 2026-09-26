import QtQuick
import CyberHand

Rectangle {
    id: root
    property string title
    property bool expanded: true
    property bool collapsible: true
    default property alias extra: extraRow.data
    signal toggled()
    implicitHeight: 24
    color: Theme.bgHeader
    Row {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Icon {
            visible: root.collapsible
            width: 11; height: 11
            anchors.verticalCenter: parent.verticalCenter
            path: root.expanded ? Icons.chevronDown : Icons.chevronRight
            color: Theme.textSecondary
        }
        Text {
            text: root.title
            color: Theme.textStrong
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
        }
    }
    Row {
        id: extraRow
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
    }
    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: extraRow.width + 8
        enabled: root.collapsible
        onClicked: { root.expanded = !root.expanded; root.toggled() }
    }
}
