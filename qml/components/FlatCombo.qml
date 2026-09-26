import QtQuick
import QtQuick.Controls.Basic
import CyberHand

ComboBox {
    id: control
    implicitHeight: Theme.controlHeight
    implicitWidth: 120
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    leftPadding: 7
    rightPadding: 22
    opacity: enabled ? 1 : 0.45

    delegate: ItemDelegate {
        width: ListView.view ? ListView.view.width : control.width
        height: 24
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole]) : modelData
            color: control.currentIndex === index ? Theme.textStrong : Theme.textPrimary
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            leftPadding: 7
        }
        background: Rectangle { color: highlighted ? Theme.bgHover : "transparent" }
    }

    indicator: Icon {
        x: control.width - width - 6
        y: (control.height - height) / 2
        width: 12; height: 12
        path: Icons.chevronDown
        color: Theme.textSecondary
    }

    contentItem: TextField {
        text: control.editable ? control.editText : control.displayText
        readOnly: !control.editable
        enabled: control.editable
        font: control.font
        color: Theme.textPrimary
        selectionColor: Theme.clip
        verticalAlignment: Text.AlignVCenter
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        background: null
        onTextEdited: if (control.editable) control.editText = text
        onAccepted: if (control.editable) control.accepted()
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.hovered && !control.editable ? Theme.bgHover : Theme.bgSunken
    }

    popup: Popup {
        y: control.height + 2
        width: Math.max(control.width, 140)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: FlatScrollBar {}
        }
        background: Rectangle {
            color: Theme.bgDeep
            radius: Theme.radiusSmall
            border.color: Theme.border
        }
    }
}
