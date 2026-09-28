import QtQuick
import QtQuick3D
import CyberHand
import CyberHand.Core

// Hand from the CAD model (Core.cad): one Model per mesh, posed by the rig's joints.
// angles = [T2, T1, I, M, R, P] in degrees, like HandRig.
Node {
    id: root
    property var angles: [0, 0, 0, 0, 0, 0]
    property bool tinted: false
    property color color: "#D6D6D6"
    property real opacity: 1.0

    eulerRotation: Core.cad.viewEuler
    position: Core.cad.viewPosition

    CadPose {
        id: pose
        model: Core.cad
        keys: Core.hand.keys
        angles: root.angles
    }

    Repeater3D {
        model: Core.cad.meshCount
        Model {
            readonly property int link: Core.cad.meshLink(index)
            geometry: CadGeometry { model: Core.cad; mesh: index }
            rotation: link < pose.rotations.length ? pose.rotations[link] : Qt.quaternion(1, 0, 0, 0)
            position: link < pose.positions.length ? pose.positions[link] : Qt.vector3d(0, 0, 0)
            materials: PrincipledMaterial {
                baseColor: root.tinted ? root.color : Core.cad.meshColor(index)
                roughness: 0.5
                metalness: 0.0
                opacity: root.opacity
                alphaMode: root.opacity < 1 ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
                cullMode: Material.NoCulling
            }
        }
    }
}
