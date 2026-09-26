import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand

// Time + |◀ ▶ ▶| + speed + loop mode, bound to a Player.
RowLayout {
    id: root
    property var player
    spacing: 4

    TimeLabel { seconds: root.player ? root.player.position : 0; Layout.preferredWidth: 74 }
    IconButton { iconPath: Icons.skipBack; iconFilled: false; tip: qsTr("Về đầu"); onClicked: root.player.toStart() }
    IconButton {
        iconPath: root.player && root.player.playing ? Icons.pause : Icons.play
        iconFilled: true
        tip: root.player && root.player.playing ? qsTr("Tạm dừng (Space)") : qsTr("Phát (Space)")
        onClicked: root.player.toggle()
    }
    IconButton { iconPath: Icons.skipForward; tip: qsTr("Về cuối"); onClicked: root.player.toEnd() }
    IconButton { iconPath: Icons.stop; iconFilled: true; iconSize: 12; tip: qsTr("Dừng"); onClicked: root.player.stop() }
    NumberField {
        Layout.preferredWidth: 52
        value: root.player ? root.player.speed : 1
        decimals: 2; from: 0.05; to: 10
        onCommitted: (v) => root.player.speed = v
    }
    TextLabel { text: "x"; secondary: true }
    FlatCombo {
        Layout.preferredWidth: 88
        model: ["Once", "Loop", "Bounce"]
        currentIndex: root.player ? root.player.loopMode : 0
        onActivated: (i) => root.player.loopMode = i
    }
    IconButton {
        iconPath: Icons.plug
        checkable: true
        checked: root.player ? root.player.output : false
        iconColor: checked ? Theme.accentGreen : Theme.textMuted
        tip: checked ? qsTr("Đang gửi lệnh tới tay") : qsTr("Chỉ mô phỏng, không gửi tới tay")
        onToggled: root.player.output = checked
    }
}
