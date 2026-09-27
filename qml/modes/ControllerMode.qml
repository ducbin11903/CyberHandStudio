import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import CyberHand
import CyberHand.Core

// CONTROLLER mode: map a game controller to joints, poses or commands.
Item {
    id: root
    readonly property var gp: Core.gamepad
    readonly property var live: gp.live
    property var openGroups: ({ "Joysticks": true, "Triggers": true })

    function v(id) { var x = live[id]; return x === undefined ? 0 : x }

    property Component toolbar: Component {
        RowLayout {
            spacing: 6
            FlatButton { text: qsTr("Save"); onClicked: Core.saveNow() }
            Icon { path: Icons.check; color: Theme.okGreen; width: 14; height: 14 }
            VSep {}
            Icon { path: Icons.gamepad; width: 18; height: 18; color: root.gp.connected ? Theme.hotspot : Theme.textSecondary }
            TextLabel {
                text: !root.gp.available ? qsTr("Bản build này không có SDL2 — chỉ mô phỏng bằng chuột")
                      : (root.gp.connected ? root.gp.deviceName : qsTr("No Controller Connected…"))
                secondary: !root.gp.connected
            }
            VSep {}
            FlatSwitch {
                text: qsTr("Gửi tới tay")
                checked: root.gp.output
                onToggled: root.gp.output = checked
            }
            FormRow {
                Layout.fillWidth: false
                label: qsTr("Deadzone"); labelWidth: 60
                NumberField { Layout.preferredWidth: 60; value: root.gp.deadzone; decimals: 2; from: 0; to: 0.9; onCommitted: (x) => root.gp.deadzone = x }
            }
            FlatButton { text: qsTr("Mặc định"); onClicked: root.gp.resetMapping() }
            Item { Layout.fillWidth: true }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // ---- mapping list ---------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 560
            Layout.fillHeight: true
            color: Theme.bgPanel
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                SectionHeader { Layout.fillWidth: true; title: qsTr("Controller Mapping"); collapsible: false }
                Flickable {
                    id: flick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: mapCol.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: FlatScrollBar {}
                    ColumnLayout {
                        id: mapCol
                        width: flick.width
                        spacing: 0
                        Repeater {
                            model: root.gp.groups
                            ColumnLayout {
                                id: grp
                                readonly property string groupName: modelData
                                readonly property bool open: root.openGroups[groupName] === true
                                Layout.fillWidth: true
                                spacing: 0
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 26
                                    color: "transparent"
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: 8
                                        spacing: 6
                                        Icon { width: 11; height: 11; path: grp.open ? Icons.chevronDown : Icons.chevronRight; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                                        TextLabel { text: grp.groupName }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            var o = Object.assign({}, root.openGroups)
                                            o[grp.groupName] = !grp.open
                                            root.openGroups = o
                                        }
                                    }
                                }
                                Repeater {
                                    model: root.gp
                                    RowLayout {
                                        id: mrow
                                        readonly property int row: index
                                        readonly property int act: model.action
                                        readonly property int jnt: model.joint
                                        readonly property string poseName: model.pose
                                        readonly property string cmd: model.command
                                        readonly property real gain: model.gain
                                        readonly property bool inv: model.invert
                                        readonly property int kind: model.kind
                                        readonly property real val: model.value
                                        readonly property string grpName: model.group
                                        readonly property string lbl: model.label
                                        visible: grp.open && grpName === grp.groupName
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 26
                                        Layout.leftMargin: 8
                                        Layout.rightMargin: 8
                                        spacing: 6
                                        Rectangle {
                                            Layout.preferredWidth: 4; Layout.preferredHeight: 16
                                            radius: 2
                                            color: Math.abs(mrow.val) > 0.2 ? Theme.hotspot : "transparent"
                                        }
                                        TextLabel { text: mrow.lbl; Layout.preferredWidth: 104 }
                                        FlatCombo {
                                            Layout.preferredWidth: 104
                                            model: root.gp.actionNames
                                            currentIndex: mrow.act
                                            onActivated: (i) => root.gp.setAction(mrow.row, i)
                                        }
                                        FlatCombo {
                                            visible: mrow.act === 1 || mrow.act === 2
                                            Layout.fillWidth: true
                                            model: Core.hand.names
                                            currentIndex: mrow.jnt
                                            onActivated: (i) => root.gp.setJoint(mrow.row, i)
                                        }
                                        FlatCombo {
                                            visible: mrow.act === 3
                                            Layout.fillWidth: true
                                            model: Core.poses.names
                                            currentIndex: Core.poses.names.indexOf(mrow.poseName)
                                            onActivated: (i) => root.gp.setPose(mrow.row, textAt(i))
                                        }
                                        FlatField {
                                            visible: mrow.act === 4
                                            Layout.fillWidth: true
                                            placeholderText: qsTr("lệnh CLI, vd H")
                                            text: mrow.cmd
                                            onEditingFinished: root.gp.setCommand(mrow.row, text)
                                        }
                                        Item { visible: mrow.act === 0; Layout.fillWidth: true }
                                        NumberField {
                                            visible: mrow.act === 1
                                            Layout.preferredWidth: 76
                                            value: mrow.gain; decimals: 0; from: 1; to: 1000; unit: "°/s"
                                            onCommitted: (x) => root.gp.setGain(mrow.row, x)
                                        }
                                        FlatCheck {
                                            visible: mrow.kind !== 2 && mrow.act !== 0
                                            text: qsTr("Đảo")
                                            checked: mrow.inv
                                            onToggled: root.gp.setInvert(mrow.row, checked)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- controller drawing ----------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: pad
                width: 400; height: 270
                anchors.centerIn: parent
                scale: Math.min(parent.width / width, parent.height / height) * 0.92

                Shape {
                    anchors.fill: parent
                    layer.enabled: true
                    layer.samples: 4
                    ShapePath {
                        strokeColor: Theme.textPrimary
                        strokeWidth: 2
                        fillColor: "transparent"
                        joinStyle: ShapePath.RoundJoin
                        PathSvg { path: "M112 78 C82 78 64 92 54 124 L28 206 C20 238 48 258 74 242 L124 204 L276 204 L326 242 C352 258 380 238 372 206 L346 124 C336 92 318 78 288 78 Z" }
                    }
                    ShapePath {
                        strokeColor: Theme.textPrimary; strokeWidth: 2; fillColor: "transparent"
                        PathSvg { path: "M98 18 h44 a8 8 0 0 1 8 8 v24 a8 8 0 0 1 -8 8 h-44 a8 8 0 0 1 -8 -8 v-24 a8 8 0 0 1 8 -8 z M258 18 h44 a8 8 0 0 1 8 8 v24 a8 8 0 0 1 -8 8 h-44 a8 8 0 0 1 -8 -8 v-24 a8 8 0 0 1 8 -8 z" }
                    }
                    ShapePath {
                        strokeColor: Theme.textPrimary; strokeWidth: 2; fillColor: "transparent"
                        PathSvg { path: "M92 66 L140 62 L144 76 L100 80 Z M308 66 L260 62 L256 76 L300 80 Z" }
                    }
                    ShapePath {
                        strokeColor: Theme.textPrimary; strokeWidth: 2; fillColor: "transparent"
                        PathSvg { path: "M142 160 h14 v-14 h16 v14 h14 v16 h-14 v14 h-16 v-14 h-14 z" }
                    }
                }

                component Stick: Item {
                    id: st
                    property string xId
                    property string yId
                    property string pressId
                    width: 56; height: 56
                    readonly property real vx: root.v(xId)
                    readonly property real vy: root.v(yId)
                    Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.color: Theme.textPrimary; border.width: 2 }
                    Rectangle { anchors.centerIn: parent; width: 32; height: 32; radius: 16; color: "transparent"; border.color: Theme.textSecondary; border.width: 1.5 }
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        color: Theme.hotspot
                        visible: Math.abs(st.vx) > 0.05 || Math.abs(st.vy) > 0.05 || root.v(st.pressId) > 0.5
                        x: st.width / 2 - 6 + st.vx * 18
                        y: st.height / 2 - 6 - st.vy * 18
                    }
                    MouseArea {
                        anchors.fill: parent
                        function upd(m) {
                            var dx = (m.x - width / 2) / 20, dy = -(m.y - height / 2) / 20
                            root.gp.simulate(st.xId, Math.max(-1, Math.min(1, dx)))
                            root.gp.simulate(st.yId, Math.max(-1, Math.min(1, dy)))
                        }
                        onPressed: (m) => upd(m)
                        onPositionChanged: (m) => { if (pressed) upd(m) }
                        onReleased: { root.gp.simulate(st.xId, 0); root.gp.simulate(st.yId, 0) }
                        onDoubleClicked: { root.gp.simulate(st.pressId, 1); root.gp.simulate(st.pressId, 0) }
                    }
                }
                component PadButton: Rectangle {
                    id: pb
                    property string inputId
                    property string label
                    width: 22; height: 22; radius: 11
                    color: root.v(inputId) > 0.5 ? Theme.hotspot : "transparent"
                    border.color: Theme.textPrimary
                    border.width: 1.5
                    TextLabel { anchors.centerIn: parent; text: pb.label; font.pixelSize: Theme.fontTiny; font.bold: true }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: root.gp.simulate(pb.inputId, 1)
                        onReleased: root.gp.simulate(pb.inputId, 0)
                    }
                }
                component Hot: Rectangle {
                    property string inputId
                    width: 12; height: 12; radius: 6
                    color: Theme.hotspot
                    visible: Math.abs(root.v(inputId)) > 0.2
                }

                Stick { x: 102; y: 96; xId: "LX"; yId: "LY"; pressId: "LS" }
                Stick { x: 218; y: 140; xId: "RX"; yId: "RY"; pressId: "RS" }
                PadButton { x: 264; y: 78; inputId: "Y"; label: "Y" }
                PadButton { x: 242; y: 100; inputId: "X"; label: "X" }
                PadButton { x: 286; y: 100; inputId: "B"; label: "B" }
                PadButton { x: 264; y: 122; inputId: "A"; label: "A" }
                PadButton { x: 172; y: 96; width: 16; height: 16; inputId: "Back"; label: "" }
                PadButton { x: 212; y: 96; width: 16; height: 16; inputId: "Start"; label: "" }
                // d-pad hit areas
                Repeater {
                    model: [{ id: "DUp", x: 157, y: 147 }, { id: "DDown", x: 157, y: 177 }, { id: "DLeft", x: 143, y: 162 }, { id: "DRight", x: 171, y: 162 }]
                    Rectangle {
                        x: modelData.x; y: modelData.y
                        width: 14; height: 14; radius: 2
                        color: root.v(modelData.id) > 0.5 ? Theme.hotspot : "transparent"
                        MouseArea { anchors.fill: parent; onPressed: root.gp.simulate(modelData.id, 1); onReleased: root.gp.simulate(modelData.id, 0) }
                    }
                }
                // shoulders and triggers
                Hot { x: 112; y: 64; inputId: "LB" }
                Hot { x: 276; y: 64; inputId: "RB" }
                MouseArea { x: 92; y: 60; width: 52; height: 20; onPressed: root.gp.simulate("LB", 1); onReleased: root.gp.simulate("LB", 0) }
                MouseArea { x: 256; y: 60; width: 52; height: 20; onPressed: root.gp.simulate("RB", 1); onReleased: root.gp.simulate("RB", 0) }
                Rectangle {
                    x: 90; y: 18; width: 60; height: 40; radius: 8
                    color: Theme.alpha(Theme.hotspot, root.v("LT") * 0.8)
                    MouseArea {
                        anchors.fill: parent
                        onPressed: (m) => root.gp.simulate("LT", 1 - m.y / height)
                        onPositionChanged: (m) => { if (pressed) root.gp.simulate("LT", Math.max(0, Math.min(1, 1 - m.y / height))) }
                        onReleased: root.gp.simulate("LT", 0)
                    }
                }
                Rectangle {
                    x: 250; y: 18; width: 60; height: 40; radius: 8
                    color: Theme.alpha(Theme.hotspot, root.v("RT") * 0.8)
                    MouseArea {
                        anchors.fill: parent
                        onPressed: (m) => root.gp.simulate("RT", 1 - m.y / height)
                        onPositionChanged: (m) => { if (pressed) root.gp.simulate("RT", Math.max(0, Math.min(1, 1 - m.y / height))) }
                        onReleased: root.gp.simulate("RT", 0)
                    }
                }
            }
            TextLabel {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                muted: true
                font.pixelSize: Theme.fontSmall
                text: qsTr("Không có tay cầm vẫn thử được: kéo cần analog, bấm nút, kéo LT/RT trên hình")
            }
        }
    }
}
