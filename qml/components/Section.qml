import QtQuick
import QtQuick.Layouts
import CyberHand

// Collapsible section: header bar + column of rows.
ColumnLayout {
    id: root
    property alias title: header.title
    property alias expanded: header.expanded
    property alias headerExtra: header.extra
    default property alias content: body.data
    spacing: 0
    Layout.fillWidth: true

    SectionHeader { id: header; Layout.fillWidth: true }
    ColumnLayout {
        id: body
        visible: header.expanded
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        Layout.topMargin: 6
        Layout.bottomMargin: 8
        spacing: 5
    }
}
