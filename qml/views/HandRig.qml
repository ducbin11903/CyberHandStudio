import QtQuick
import QtQuick3D

// Right hand seen from the back: thumb on the left (-X), fingers along +Y, palm faces -Z.
// angles = [T2 thumb flexion, T1 thumb rotation, I, M, R, P] in degrees.
Node {
    id: root
    property var angles: [0, 0, 0, 0, 0, 0]
    property color color: "#D6D6D6"
    property color jointColor: "#A9A9A9"
    property real opacity: 1.0
    property bool showWrist: true

    function a(i) { var v = angles[i]; return (v === undefined || isNaN(v)) ? 0 : v }

    PrincipledMaterial {
        id: skin
        baseColor: root.color
        roughness: 0.55
        metalness: 0.0
        opacity: root.opacity
        alphaMode: root.opacity < 1 ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: jointMat
        baseColor: root.jointColor
        roughness: 0.45
        opacity: root.opacity
        alphaMode: root.opacity < 1 ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
    }
    PrincipledMaterial {
        id: shell
        baseColor: "#8C8C8C"
        roughness: 0.7
        opacity: root.opacity
        alphaMode: root.opacity < 1 ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
    }

    // palm
    Model {
        source: "#Cube"
        position: Qt.vector3d(0, 45, 0)
        scale: Qt.vector3d(0.84, 0.90, 0.24)
        materials: [skin]
    }
    // knuckle bar
    Model {
        source: "#Cylinder"
        position: Qt.vector3d(3, 88, -1)
        eulerRotation.z: 90
        scale: Qt.vector3d(0.2, 0.74, 0.2)
        materials: [jointMat]
    }
    // wrist / base
    Model {
        visible: root.showWrist
        source: "#Cube"
        position: Qt.vector3d(0, -28, 0)
        scale: Qt.vector3d(0.70, 0.56, 0.32)
        materials: [shell]
    }

    Finger { x: -27; y: 90; angle: root.a(2); k: 1.0;  material: skin; jointMaterial: jointMat }
    Finger { x: -9;  y: 92; angle: root.a(3); k: 1.08; material: skin; jointMaterial: jointMat }
    Finger { x: 9;   y: 90; angle: root.a(4); k: 1.02; material: skin; jointMaterial: jointMat }
    Finger { x: 27;  y: 85; angle: root.a(5); k: 0.84; width: 14; material: skin; jointMaterial: jointMat }

    // thumb: T1 swings it across the palm (opposition), T2 curls it.
    // Coefficients fitted so that the "Pinch" pose brings the thumb tip to the index tip.
    Node {
        position: Qt.vector3d(-33.5, 38, -14)
        eulerRotation: Qt.vector3d(root.a(1) * 0.80, -root.a(1) * 1.95, 39.2 - root.a(1) * 0.18)
        Segment {
            length: 34; w: 18; h: 16
            material: skin; jointMaterial: jointMat
        }
        Node {
            y: 34
            eulerRotation: Qt.vector3d(-root.a(0) * 0.97, 0, root.a(0) * 0.57)
            Segment {
                length: 28; w: 16; h: 14
                material: skin; jointMaterial: jointMat
            }
            Node {
                y: 28
                eulerRotation: Qt.vector3d(root.a(0) * 0.19, 0, -root.a(0) * 0.54)
                Segment {
                    length: 24; w: 15; h: 13; tip: true
                    material: skin; jointMaterial: jointMat
                }
            }
        }
    }
}
