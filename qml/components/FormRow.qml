import QtQuick
import QtQuick.Layouts
import CyberHand

// "Label ........ [controls]" row like the reference app's settings panel.
RowLayout {
    id: root
    property string label
    property int labelWidth: Theme.labelWidth
    default property alias content: inner.data
    spacing: 6
    Layout.fillWidth: true
    Layout.preferredHeight: Theme.rowHeight
    Text {
        text: root.label
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        elide: Text.ElideRight
        Layout.preferredWidth: root.labelWidth
        Layout.alignment: Qt.AlignVCenter
    }
    RowLayout {
        id: inner
        spacing: 6
        Layout.fillWidth: true
    }
}
