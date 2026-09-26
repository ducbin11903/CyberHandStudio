import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand

Popup {
    id: root
    property string message
    property string okText: qsTr("Đồng ý")
    property var onOk: null
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 340
    padding: 14
    background: Rectangle { color: Theme.bgPanel; radius: Theme.radius; border.color: Theme.border }
    Overlay.modal: Rectangle { color: Theme.alpha(Theme.bgWindow, 0.55) }
    function ask(msg, cb, ok) { message = msg; onOk = cb; okText = ok || qsTr("Đồng ý"); open() }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        TextLabel { text: root.message; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FlatButton { text: qsTr("Hủy"); onClicked: root.close() }
            FlatButton { text: root.okText; danger: true; onClicked: { if (root.onOk) root.onOk(); root.close() } }
        }
    }
}
