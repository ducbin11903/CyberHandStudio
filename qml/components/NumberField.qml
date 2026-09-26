import QtQuick
import QtQuick.Controls.Basic
import CyberHand

// Numeric field: shows `value`, emits committed(v) on Enter / focus loss.
FlatField {
    id: control
    property real value: 0
    property int decimals: 1
    property real from: -1e9
    property real to: 1e9
    property string unit: ""
    signal committed(real v)

    function fmt(v) {
        var s = Number(v).toFixed(decimals)
        if (s.indexOf(".") >= 0) s = s.replace(/0+$/, "").replace(/\.$/, "")
        return s
    }
    text: fmt(value)
    horizontalAlignment: TextInput.AlignRight
    rightPadding: unit.length ? unitText.implicitWidth + 12 : 7
    validator: DoubleValidator { bottom: control.from; top: control.to; notation: DoubleValidator.StandardNotation }
    onValueChanged: if (!activeFocus) text = fmt(value)
    onEditingFinished: {
        var v = Number(text.replace(",", "."))
        if (isNaN(v)) { text = fmt(value); return }
        v = Math.max(from, Math.min(to, v))
        text = fmt(v)
        committed(v)
    }
    onActiveFocusChanged: if (!activeFocus) text = fmt(value)

    Text {
        id: unitText
        visible: control.unit.length > 0
        text: control.unit
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        anchors.right: parent.right
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
    }
}
