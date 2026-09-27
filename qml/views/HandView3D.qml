import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// 3D preview of the hand: solid = measured (or commanded when no feedback), ghost = target.
Card {
    id: root
    readonly property var joints: Core.hand.joints
    readonly property bool anyFeedback: {
        for (var i = 0; i < joints.length; ++i) if (joints[i].hasFeedback) return true
        return false
    }
    property bool showGhost: Core.ui.showGhost !== false
    property bool showFloor: Core.ui.showFloor !== false

    function previewAngles() {
        var a = []
        for (var i = 0; i < joints.length; ++i) a.push(joints[i].previewDeg)
        return a
    }
    // redrawn at the Visual Refresh Rate instead of on every telemetry line
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
        for (var i = 0; i < joints.length; ++i)
            a.push(joints[i].hasFeedback ? joints[i].targetDeg : joints[i].commandDeg)
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
            position: Qt.vector3d(0, 62, 0)
            eulerRotation: Qt.vector3d(-14, 150, 0)
            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(0, 0, 440)
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
            angles: root.shownAngles
        }
        HandRig {
            id: ghost
            visible: root.showGhost && (root.anyFeedback || Core.player.playing)
            angles: root.shownTargets
            color: Theme.infoBlue
            jointColor: Theme.infoBlue
            opacity: 0.22
            showWrist: false
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
            TextLabel { text: root.anyFeedback ? qsTr("Đo được") : qsTr("Lệnh / mô phỏng"); font.pixelSize: Theme.fontSmall }
        }
        Row {
            visible: ghost.visible
            spacing: 5
            Rectangle { width: 10; height: 10; radius: 2; color: Theme.alpha(Theme.infoBlue, 0.5); anchors.verticalCenter: parent.verticalCenter }
            TextLabel { text: qsTr("Đích"); font.pixelSize: Theme.fontSmall }
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
                camera.position = Qt.vector3d(0, 0, 440)
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
                Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.jointColor(index); anchors.verticalCenter: parent.verticalCenter }
                TextLabel { text: modelData.key; width: 18; font.pixelSize: Theme.fontSmall; secondary: true }
                TextLabel {
                    text: Number(modelData.previewDeg).toFixed(1) + "°"
                    width: 44
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Theme.fontSmall
                    color: modelData.hasFeedback ? Theme.textStrong : Theme.textSecondary
                }
            }
        }
    }
}
