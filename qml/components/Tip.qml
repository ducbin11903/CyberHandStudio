import QtQuick
import QtQuick.Controls.Basic
import CyberHand

// Dark tooltip matching the theme (the stock Basic tooltip is white).
ToolTip {
    id: tip
    delay: 500
    timeout: 6000
    padding: 6
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSmall
    contentItem: Text {
        text: tip.text
        font: tip.font
        color: Theme.textPrimary
        wrapMode: Text.WordWrap
    }
    background: Rectangle {
        color: Theme.bgDeep
        radius: Theme.radiusSmall
        border.color: Theme.border
    }
}
