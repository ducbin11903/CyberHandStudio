import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Software update: current / latest build, what changed, download, restart into the new build.
Popup {
    id: root
    readonly property var up: Core.updater
    readonly property bool busy: up.state === Updater.Checking || up.state === Updater.Downloading
                                 || up.state === Updater.Extracting
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 16
    closePolicy: busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    background: Rectangle { color: Theme.bgPanel; radius: Theme.radius; border.color: Theme.border }
    Overlay.modal: Rectangle { color: Theme.alpha(Theme.bgWindow, 0.55) }

    function openAndCheck() {
        open()
        if (!busy && up.state !== Updater.Ready)
            up.check(false)
    }

    component InfoRow: RowLayout {
        property alias title: t.text
        property alias value: v.text
        Layout.fillWidth: true
        spacing: 10
        TextLabel { id: t; Layout.preferredWidth: 90; secondary: true }
        TextLabel { id: v; Layout.fillWidth: true; font.family: Theme.monoFamily }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        TextLabel { text: qsTr("Cập nhật phần mềm"); font.bold: true; font.pixelSize: Theme.fontSize + 2 }

        InfoRow { title: qsTr("Đang dùng"); value: root.up.currentText }
        InfoRow {
            title: qsTr("Mới nhất")
            value: root.up.state === Updater.Checking ? qsTr("đang kiểm tra…")
                   : (root.up.latestText.length ? root.up.latestText : "—")
        }

        TextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            visible: text.length > 0
            color: root.up.state === Updater.Error ? Theme.liveRed
                   : (root.up.available ? Theme.accentGreen : Theme.textPrimary)
            text: {
                switch (root.up.state) {
                case Updater.UpToDate: return qsTr("Đang dùng bản mới nhất.")
                case Updater.Available: return qsTr("Có bản mới.")
                case Updater.Ready: return qsTr("Đã tải xong. App sẽ đóng, chép file mới rồi tự mở lại.")
                case Updater.Error: return root.up.errorString
                }
                return ""
            }
        }
        TextLabel {
            Layout.fillWidth: true
            visible: root.up.installNote.length > 0
            text: root.up.installNote
            muted: true
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }

        // what changed since the running build
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(180, notesList.contentHeight + 12)
            visible: root.up.available && root.up.notes.length > 0
            color: Theme.bgSunken
            radius: Theme.radiusSmall
            ListView {
                id: notesList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: root.up.notes
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: FlatScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Text {
                    width: ListView.view.width
                    text: modelData
                    color: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
            }
        }

        // download / extract progress
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.up.state === Updater.Downloading || root.up.state === Updater.Extracting
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                radius: 3
                color: Theme.bgSunken
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, root.up.progress))
                    height: parent.height
                    radius: 3
                    color: Theme.accentGreen
                }
            }
            TextLabel { text: root.up.progressText; secondary: true; font.pixelSize: Theme.fontSmall }
        }

        TextLabel {
            Layout.fillWidth: true
            visible: root.up.state === Updater.Ready && Core.ctrl.connected
            text: qsTr("Đang kết nối: mọi motor về duty 0 trước khi app đóng.")
            color: Theme.warnAmber
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            FlatCheck {
                text: qsTr("Tự kiểm tra khi mở app")
                checked: root.up.autoCheck
                onToggled: root.up.autoCheck = checked
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTr("Releases")
                tip: root.up.releasePage
                onClicked: root.up.openReleasePage()
            }
            FlatButton {
                text: qsTr("Kiểm tra")
                iconPath: Icons.refresh
                enabled: !root.busy
                onClicked: root.up.check(false)
            }
            FlatButton {
                visible: root.up.available && root.up.canInstall && root.up.state !== Updater.Ready
                enabled: !root.busy
                accent: true
                text: qsTr("Tải về")
                iconPath: Icons.download
                onClicked: root.up.download()
            }
            FlatButton {
                visible: root.up.state === Updater.Ready
                accent: true
                text: qsTr("Khởi động lại & cập nhật")
                iconPath: Icons.refresh
                onClicked: root.up.installAndRestart()
            }
            FlatButton { text: qsTr("Đóng"); enabled: !root.busy; onClicked: root.close() }
        }
    }
}
