import QtQuick
import CyberHand

// Small history line (e.g. last 10 min) with dashed threshold lines.
Canvas {
    id: sp
    property var points: []        // list of point {x: seconds, y: value}
    property real now: 0           // right edge, same time base as points
    property real span: 600        // seconds shown
    property var thresholds: []    // [{ v: 70, color: "#D1533B" }]
    property color lineColor: Theme.infoBlue
    property real minSpan: 1
    implicitHeight: 34
    implicitWidth: 100

    onPointsChanged: requestPaint()
    onThresholdsChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = Theme.bgDeep
        ctx.fillRect(0, 0, width, height)
        var lo = Infinity, hi = -Infinity
        var i
        for (i = 0; i < points.length; ++i) {
            lo = Math.min(lo, points[i].y)
            hi = Math.max(hi, points[i].y)
        }
        for (i = 0; i < thresholds.length; ++i) {
            if (!isFinite(thresholds[i].v)) continue
            lo = Math.min(lo, thresholds[i].v)
            hi = Math.max(hi, thresholds[i].v)
        }
        if (!isFinite(lo)) return
        var s = Math.max(hi - lo, minSpan)
        var mid = (hi + lo) / 2
        lo = mid - s / 2 - s * 0.12
        hi = mid + s / 2 + s * 0.12
        var t0 = now - span
        function X(t) { return (t - t0) / span * width }
        function Y(v) { return height - (v - lo) / (hi - lo) * height }
        ctx.lineWidth = 1
        for (i = 0; i < thresholds.length; ++i) {
            var th = thresholds[i]
            if (!isFinite(th.v)) continue
            ctx.strokeStyle = th.color
            ctx.setLineDash([3, 3])
            ctx.beginPath()
            ctx.moveTo(0, Math.round(Y(th.v)) + 0.5)
            ctx.lineTo(width, Math.round(Y(th.v)) + 0.5)
            ctx.stroke()
        }
        ctx.setLineDash([])
        if (points.length === 0) return
        ctx.strokeStyle = lineColor
        ctx.lineWidth = 1.5
        ctx.beginPath()
        var started = false
        for (i = 0; i < points.length; ++i) {
            var p = points[i]
            if (p.x < t0 - 1) continue
            if (!started) { ctx.moveTo(X(p.x), Y(p.y)); started = true }
            else ctx.lineTo(X(p.x), Y(p.y))
        }
        ctx.stroke()
        // latest value
        var last = points[points.length - 1]
        ctx.fillStyle = lineColor
        ctx.beginPath()
        ctx.arc(X(last.x), Y(last.y), 2, 0, 2 * Math.PI)
        ctx.fill()
    }
}
