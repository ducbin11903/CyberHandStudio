import QtQuick
import CyberHand

// 0:00:00:00 style clock (h:mm:ss:cs) like the reference transport bar.
Text {
    property real seconds: 0
    function pad(n, w) { var s = "" + Math.floor(n); while (s.length < w) s = "0" + s; return s }
    text: {
        var t = Math.max(0, seconds)
        var h = Math.floor(t / 3600), m = Math.floor(t / 60) % 60, s = Math.floor(t) % 60, cs = Math.floor(t * 100) % 100
        return h + ":" + pad(m, 2) + ":" + pad(s, 2) + ":" + pad(cs, 2)
    }
    color: Theme.textPrimary
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    verticalAlignment: Text.AlignVCenter
}
