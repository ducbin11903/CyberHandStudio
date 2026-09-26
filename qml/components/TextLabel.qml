import QtQuick
import CyberHand

Text {
    property bool secondary: false
    property bool muted: false
    color: muted ? Theme.textMuted : (secondary ? Theme.textSecondary : Theme.textPrimary)
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
}
