import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand

// Small modal asking for one line of text.
Popup {
    id: root
    property string title
    property string placeholder
    property alias text: field.text
    property string okText: qsTr("OK")
    signal accepted(string text)
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 340
    padding: 14
    background: Rectangle { color: Theme.bgPanel; radius: Theme.radius; border.color: Theme.border }
    Overlay.modal: Rectangle { color: Theme.alpha(Theme.bgWindow, 0.55) }
    onOpened: { field.forceActiveFocus(); field.selectAll() }

    function ask(t, initial, ph) { title = t; field.text = initial || ""; placeholder = ph || ""; open() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        TextLabel { text: root.title; font.bold: true; color: Theme.textStrong }
        FlatField {
            id: field
            Layout.fillWidth: true
            placeholderText: root.placeholder
            onAccepted: okBtn.clicked()
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { text: qsTr("Hủy"); onClicked: root.close() }
            FlatButton {
                id: okBtn
                text: root.okText
                accent: true
                enabled: field.text.trim().length > 0
                onClicked: { root.accepted(field.text.trim()); root.close() }
            }
        }
    }
}
