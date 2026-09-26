import QtQuick
import QtQuick.Shapes
import CyberHand

// Line icon from Icons.qml path data (24x24 viewbox), recolorable.
Item {
    id: root
    property string path
    property color color: Theme.textPrimary
    property real stroke: 1.7
    property bool filled: false
    implicitWidth: 16
    implicitHeight: 16

    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        layer.smooth: true
        ShapePath {
            strokeWidth: root.filled ? 0 : root.stroke * root.width / 16
            strokeColor: root.filled ? "transparent" : root.color
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            scale: Qt.size(root.width / 24, root.height / 24)
            PathSvg { path: root.path }
        }
    }
}
