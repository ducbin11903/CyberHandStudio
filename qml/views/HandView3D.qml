import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// 3D preview of the hand: solid = measured, ghost = target.
// Simulating (not connected, or the active source is not sent): solid = commands, ghost = real hand.
Card {
    id: root
    readonly property var joints: Core.hand.joints
    readonly property bool anyFeedback: {
        for (var i = 0; i < joints.length; ++i) if (joints[i].hasFeedback) return true
        return false
    }
    property bool showGhost: Core.ui.showGhost !== false
    property bool showFloor: Core.ui.showFloor !== false
    // CAD model (Visual -> Mô hình 3D) or the block hand
    readonly property bool useCad: Core.cad.loaded && Core.ui.model3d !== "blocks"

    readonly property bool sim: Core.simulating
    function previewAngles() {
        var a = []
        for (var i = 0; i < joints.length; ++i) a.push(root.sim ? joints[i].commandDeg : joints[i].previewDeg)
        return a
    }
    // redrawn at the Visual Refresh Rate instead of on every telemetry line
    readonly property bool ghostOn: root.showGhost && (root.anyFeedback || Core.player.playing)
    property var shownAngles: [0, 0, 0, 0, 0, 0]
    property var shownTargets: [0, 0, 0, 0, 0, 0]
    Component.onCompleted: { shownAngles = previewAngles(); shownTargets = targetAngles() }
    Timer {
        interval: Math.max(7, Math.round(1000 / Core.refreshHz))
        running: true
        repeat: true
        onTriggered: {
            var a = root.previewAngles(), t = root.targetAngles()
            if (JSON.stringify(a) !== JSON.stringify(root.shownAngles)) root.shownAngles = a
            if (JSON.stringify(t) !== JSON.stringify(root.shownTargets)) root.shownTargets = t
        }
    }
    function targetAngles() {
        var a = []
        for (var i = 0; i < joints.length; ++i) {
            var j = joints[i]
            a.push(!j.hasFeedback ? j.commandDeg : (root.sim ? j.measuredDeg : j.targetDeg))
        }
        return a
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: camera
        renderMode: View3D.Offscreen

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: Theme.bgViewport
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: orbitOrigin
            position: Qt.vector3d(0, root.useCad ? 76 : 62, 0)
            eulerRotation: Qt.vector3d(-14, 150, 0)
            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(0, 0, root.useCad ? 520 : 440)
                clipNear: 5
                clipFar: 3000
                fieldOfView: 40
            }
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-35, -25, 0)
            brightness: 1.25
            castsShadow: false
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(20, 150, 0)
            brightness: 0.45
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-80, 0, 0)
            brightness: 0.25
        }

        Model {
            visible: root.showFloor
            source: "#Rectangle"
            y: -57
            eulerRotation.x: -90
            scale: Qt.vector3d(4, 4, 1)
            materials: PrincipledMaterial { baseColor: "#3F3F3F"; roughness: 0.9 }
        }

        HandRig {
            id: hand
            visible: !root.useCad
            angles: root.shownAngles
        }
        HandRig {
            id: ghost
            visible: !root.useCad && root.ghostOn
            angles: root.shownTargets
            color: Theme.infoBlue
            jointColor: Theme.infoBlue
            opacity: 0.22
            showWrist: false
        }
        Loader3D {
            active: root.useCad
            sourceComponent: CadHandRig { angles: root.shownAngles }
        }
        Loader3D {
            active: root.useCad && root.ghostOn
            sourceComponent: CadHandRig {
                angles: root.shownTargets
                tinted: true
                color: Theme.infoBlue
                opacity: 0.22
            }
        }
    }

    OrbitCameraController {
        id: orbit
        anchors.fill: parent
        origin: orbitOrigin
        camera: camera
    }

    // overlay: legend + tools
    RowLayout {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 12
        Row {
            spacing: 5
            Rectangle { width: 10; height: 10; radius: 2; color: "#D6D6D6"; anchors.verticalCenter: parent.verticalCenter }
            TextLabel { text: root.sim ? qsTr("Mô phỏng (lệnh)") : qsTr("Đo được"); font.pixelSize: Theme.fontSmall }
        }
        Row {
            visible: root.ghostOn
            spacing: 5
            Rectangle { width: 10; height: 10; radius: 2; color: Theme.alpha(Theme.infoBlue, 0.5); anchors.verticalCenter: parent.verticalCenter }
            TextLabel { text: root.sim && root.anyFeedback ? qsTr("Tay thật") : qsTr("Đích"); font.pixelSize: Theme.fontSmall }
        }
    }
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        spacing: 2
        IconButton {
            iconPath: Icons.eye
            checkable: true
            checked: root.showGhost
            tip: qsTr("Bóng mờ đích")
            onToggled: Core.setUiValue("showGhost", checked)
        }
        IconButton {
            iconPath: Icons.camera
            tip: qsTr("Đặt lại góc nhìn")
            onClicked: {
                orbitOrigin.eulerRotation = Qt.vector3d(-14, 150, 0)
                camera.position = Qt.vector3d(0, 0, root.useCad ? 520 : 440)
            }
        }
    }
    // joint angle chips
    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 3
        Repeater {
            model: root.joints
            Row {
                spacing: 6
                opacity: modelData.offPort ? 0.45 : 1.0
                Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.jointColor(index); anchors.verticalCenter: parent.verticalCenter }
                TextLabel { text: modelData.key; width: 18; font.pixelSize: Theme.fontSmall; secondary: true }
                TextLabel {
                    text: Number(root.sim ? modelData.commandDeg : modelData.previewDeg).toFixed(1) + "°"
                    width: 44
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Theme.fontSmall
                    color: modelData.hasFeedback && !root.sim ? Theme.textStrong : Theme.textSecondary
                }
                TextLabel {
                    visible: modelData.offPort
                    text: qsTr("chưa kết nối")
                    muted: true
                    font.pixelSize: Theme.fontTiny
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
