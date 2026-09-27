import QtQuick
import QtQuick.Window
import CyberHand

// Separate tool window (Manual control, Calibration, Data monitor) in the app theme.
Window {
    id: w
    default property alias content: body.data
    color: Theme.bgWindow
    flags: Qt.Window
    minimumWidth: 360
    minimumHeight: 240

    function toggle() {
        if (visible) {
            close()
        } else {
            show()
            raise()
            requestActivate()
        }
    }

    Rectangle {
        id: body
        anchors.fill: parent
        anchors.margins: Theme.gap
        color: Theme.bgPanel
        radius: Theme.radius
        clip: true
    }
}
