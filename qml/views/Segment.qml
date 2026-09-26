import QtQuick
import QtQuick3D

// One phalanx: rounded body from the local origin along +Y, joint knuckle at the origin.
Node {
    id: root
    property real length: 30
    property real w: 16
    property real h: 14
    property bool tip: false
    property Material material
    property Material jointMaterial

    Model {
        source: "#Cylinder"
        position: Qt.vector3d(0, root.length / 2, 0)
        scale: Qt.vector3d(root.w / 100, (root.length - root.h * 0.5) / 100, root.h / 100)
        materials: [root.material]
    }
    Model {
        source: "#Sphere"
        scale: Qt.vector3d(root.w * 1.02 / 100, root.h * 1.02 / 100, root.h * 1.02 / 100)
        materials: [root.jointMaterial]
    }
    Model {
        visible: root.tip
        source: "#Sphere"
        position: Qt.vector3d(0, root.length - root.h * 0.25, 0)
        scale: Qt.vector3d(root.w / 100, root.h / 100, root.h / 100)
        materials: [root.material]
    }
}
