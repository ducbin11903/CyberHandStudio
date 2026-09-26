import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Edit a macro: name + script.
Popup {
    id: root
    property int row: -1
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, parent ? parent.width - 80 : 620)
    height: Math.min(520, parent ? parent.height - 80 : 520)
    padding: 14
    background: Rectangle { color: Theme.bgPanel; radius: Theme.radius; border.color: Theme.border }
    Overlay.modal: Rectangle { color: Theme.alpha(Theme.bgWindow, 0.55) }

    function edit(r) {
        row = r
        nameField.text = r >= 0 ? Core.macros.name(r) : qsTr("Macro mới")
        script.text = r >= 0 ? Core.macros.script(r) : "# mỗi dòng là một lệnh CLI\n# @wait <ms>, @waitfor <text> [ms], @repeat <N> ... @end, @pose <tên>, @log <text>\ne\n"
        open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            TextLabel { text: qsTr("Tên"); Layout.preferredWidth: 40 }
            FlatField { id: nameField; Layout.fillWidth: true }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgSunken
            radius: Theme.radiusSmall
            ScrollView {
                anchors.fill: parent
                anchors.margins: 4
                ScrollBar.vertical: FlatScrollBar {}
                TextArea {
                    id: script
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSize
                    color: Theme.textPrimary
                    selectionColor: Theme.clip
                    selectByMouse: true
                    background: null
                    wrapMode: TextEdit.NoWrap
                }
            }
        }
        TextLabel {
            muted: true
            font.pixelSize: Theme.fontSmall
            text: qsTr("@wait 1000 · @waitfor HOME: 20000 · @repeat 20 … @end · @pose Fist · @log text · # comment")
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            FlatButton {
                text: qsTr("Chạy thử")
                iconPath: Icons.play
                iconFilled: true
                onClicked: Core.macros.runText(script.text, nameField.text)
            }
            Item { Layout.fillWidth: true }
            FlatButton { text: qsTr("Hủy"); onClicked: root.close() }
            FlatButton {
                text: qsTr("Lưu")
                accent: true
                onClicked: {
                    if (root.row >= 0) Core.macros.update(root.row, nameField.text, script.text)
                    else Core.macros.add(nameField.text, script.text)
                    Core.saveNow()
                    root.close()
                }
            }
        }
    }
}
