import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Left side of the Timeline: clip library (Motion / Custom). Drag a tile onto a track,
// or click it to drop it on the active track at the playhead.
Rectangle {
    id: root
    readonly property var tl: Core.timeline
    property int tab: 0
    // scene coordinates while dragging
    signal dragStarted(string key, string name, string icon)
    signal dragMoved(real sx, real sy)
    signal dropped(string key, real sx, real sy)
    signal picked(string key)

    radius: Theme.radiusSmall
    color: Theme.bgSunken

    component Tile: Rectangle {
        id: tile
        property string key
        property string name
        property string icon
        property bool removable: false
        signal removeRequested()
        width: parent ? (parent.width - 4) / 2 : 80
        height: 44
        radius: Theme.radiusSmall
        color: ma.pressed ? Theme.bgPressed : (ma.containsMouse ? Theme.bgHover : Theme.bgDeep)
        border.width: 1
        border.color: Theme.divider
        Column {
            anchors.centerIn: parent
            spacing: 2
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 18; height: 18
                path: Icons[tile.icon] || Icons.curve
                color: Theme.clipSelected
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: tile.width - 8
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: tile.name
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
            }
        }
        IconButton {
            visible: tile.removable && ma.containsMouse
            anchors.right: parent.right
            anchors.top: parent.top
            width: 16; height: 16
            iconPath: Icons.close
            onClicked: tile.removeRequested()
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.OpenHandCursor
            preventStealing: true
            property bool dragging: false
            property real px: 0
            property real py: 0
            onPressed: (m) => { dragging = false; px = m.x; py = m.y }
            onPositionChanged: (m) => {
                if (!pressed) return
                if (!dragging && Math.abs(m.x - px) + Math.abs(m.y - py) > 6) {
                    dragging = true
                    root.dragStarted(tile.key, tile.name, tile.icon)
                }
                if (dragging) {
                    var p = mapToItem(null, m.x, m.y)
                    root.dragMoved(p.x, p.y)
                }
            }
            onReleased: (m) => {
                if (dragging) {
                    var p = mapToItem(null, m.x, m.y)
                    root.dropped(tile.key, p.x, p.y)
                } else {
                    root.picked(tile.key)
                }
                dragging = false
            }
        }
        Tip { visible: ma.containsMouse && !ma.pressed; text: qsTr("Kéo vào làn, hoặc bấm để thêm vào làn đang chọn tại playhead") }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        SegmentToggle {
            Layout.fillWidth: true
            options: [qsTr("Motion"), qsTr("Custom")]
            currentIndex: root.tab
            onActivated: (i) => root.tab = i
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: FlatScrollBar {}

            Column {
                id: body
                width: flick.width
                spacing: 6

                // ---- Motion: built-in library grouped ------------------------
                Repeater {
                    model: root.tab === 0 ? ["Linear", "Wave", "Power"] : []
                    Column {
                        width: body.width
                        spacing: 4
                        readonly property string group: modelData
                        Text {
                            text: parent.group
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                        }
                        Grid {
                            width: parent.width
                            columns: 2
                            columnSpacing: 4
                            rowSpacing: 4
                            Repeater {
                                model: root.tl.library.filter(function(e) { return e.group === group })
                                Tile { key: modelData.key; name: modelData.name; icon: modelData.icon }
                            }
                        }
                    }
                }

                // ---- Custom: clips the user saved ----------------------------
                Grid {
                    visible: root.tab === 1
                    width: body.width
                    columns: 2
                    columnSpacing: 4
                    rowSpacing: 4
                    Repeater {
                        model: root.tab === 1 ? root.tl.customClips : []
                        Tile {
                            readonly property var icons: ["curve", "hold", "ramp", "sine", "square", "triangle", "sawtooth", "easeIn"]
                            key: "custom:" + index
                            name: modelData.name
                            icon: icons[modelData.kind] || "curve"
                            removable: true
                            onRemoveRequested: { root.tl.removeCustom(index); Core.saveNow() }
                        }
                    }
                }
                TextLabel {
                    visible: root.tab === 1 && root.tl.customClips.length === 0
                    width: body.width
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    secondary: true
                    font.pixelSize: Theme.fontSmall
                    text: qsTr("Chọn một clip trên timeline rồi bấm \"Lưu Custom\" ở Clip Details để dùng lại.")
                }
            }
        }
    }
}
