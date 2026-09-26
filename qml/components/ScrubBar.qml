import QtQuick
import QtQuick.Layouts
import CyberHand

// "0.00 / 10.00 s" + thin track with an orange position marker; drag to seek.
RowLayout {
    id: root
    property real position: 0
    property real duration: 1
    signal seek(real t)
    spacing: 8
    Layout.fillWidth: true
    Layout.preferredHeight: 22

    Rectangle {
        Layout.preferredWidth: 56
        Layout.preferredHeight: 20
        radius: Theme.radiusSmall
        color: Theme.bgSunken
        TextLabel { anchors.centerIn: parent; text: root.position.toFixed(2); font.pixelSize: Theme.fontSmall }
    }
    TextLabel { text: "/ " + root.duration.toFixed(2); secondary: true; font.pixelSize: Theme.fontSmall }
    TextLabel { text: "s"; secondary: true; font.pixelSize: Theme.fontSmall }
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 20
        Rectangle {
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 8
            radius: 4
            color: Theme.bgSunken
            Rectangle {
                height: parent.height; radius: 4
                width: Math.max(0, Math.min(1, root.position / Math.max(1e-6, root.duration))) * parent.width
                color: Theme.alpha(Theme.playhead, 0.18)
            }
        }
        Rectangle {
            width: 4; height: 16; radius: 1
            color: Theme.playhead
            anchors.verticalCenter: parent.verticalCenter
            x: Math.max(0, Math.min(1, root.position / Math.max(1e-6, root.duration))) * (parent.width - width)
        }
        MouseArea {
            anchors.fill: parent
            function go(mx) { root.seek(Math.max(0, Math.min(1, mx / width)) * root.duration) }
            onPressed: (m) => go(m.x)
            onPositionChanged: (m) => { if (pressed) go(m.x) }
        }
    }
}
