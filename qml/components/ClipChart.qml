import QtQuick
import CyberHand

// Small chart of one clip: values over [t0, t1] against the joint range [lo, hi].
Rectangle {
    id: root
    property var values: []
    property real lo: 0
    property real hi: 90
    property real t0: 0
    property real t1: 1
    property real fromValue: NaN
    property color lineColor: Theme.clipSelected
    property real marker: NaN        // playhead time, shown when inside [t0, t1]
    color: Theme.bgDeep
    radius: Theme.radiusSmall
    implicitHeight: 110

    onValuesChanged: canvas.requestPaint()
    onLoChanged: canvas.requestPaint()
    onHiChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.leftMargin: 30
        anchors.rightMargin: 8
        anchors.topMargin: 8
        anchors.bottomMargin: 18
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        function yOf(v) { return height - (v - root.lo) / Math.max(1e-6, root.hi - root.lo) * height }
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = Theme.divider
            ctx.lineWidth = 1
            for (var g = 0; g <= 4; ++g) {
                var gy = Math.round(g / 4 * height) + 0.5
                ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke()
            }
            var v = root.values
            if (!v || v.length < 2)
                return
            ctx.strokeStyle = root.lineColor
            ctx.lineWidth = 1.6
            ctx.beginPath()
            for (var i = 0; i < v.length; ++i) {
                var px = i / (v.length - 1) * width
                var py = yOf(v[i])
                if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
            }
            ctx.stroke()
            if (!isNaN(root.fromValue)) {
                ctx.fillStyle = Theme.textSecondary
                ctx.beginPath(); ctx.arc(0, yOf(root.fromValue), 2.5, 0, 2 * Math.PI); ctx.fill()
            }
        }
        Rectangle {
            visible: !isNaN(root.marker) && root.marker >= root.t0 && root.marker <= root.t1
            x: (root.marker - root.t0) / Math.max(1e-6, root.t1 - root.t0) * parent.width
            width: 1; height: parent.height
            color: Theme.playhead
        }
    }
    Text {
        x: 4; y: canvas.y - 6
        text: Math.round(root.hi) + "°"
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny
    }
    Text {
        x: 4; y: canvas.y + canvas.height - 8
        text: Math.round(root.lo) + "°"
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny
    }
    Text {
        x: canvas.x; y: canvas.y + canvas.height + 3
        text: root.t0.toFixed(2) + " s"
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 8
        y: canvas.y + canvas.height + 3
        text: root.t1.toFixed(2) + " s"
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTiny
    }
}
