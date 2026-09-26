import QtQuick
import QtQuick3D

// Finger with MCP driven by `angle` (deg); PIP/DIP follow through the linkage coupling.
Node {
    id: root
    property real angle: 0
    property real k: 1.0          // length scale
    property real width: 16
    property real pipCoupling: 0.8
    property real dipCoupling: 0.4
    property Material material
    property Material jointMaterial

    Node {
        eulerRotation.x: -root.angle
        Segment {
            length: 36 * root.k; w: root.width; h: 14
            material: root.material; jointMaterial: root.jointMaterial
        }
        Node {
            y: 36 * root.k
            eulerRotation.x: -root.angle * root.pipCoupling
            Segment {
                length: 23.5 * root.k; w: root.width - 1; h: 13
                material: root.material; jointMaterial: root.jointMaterial
            }
            Node {
                y: 23.5 * root.k
                eulerRotation.x: -root.angle * root.dipCoupling
                Segment {
                    length: 21 * root.k; w: root.width - 2; h: 12; tip: true
                    material: root.material; jointMaterial: root.jointMaterial
                }
            }
        }
    }
}
