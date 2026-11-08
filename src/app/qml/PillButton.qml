import QtQuick
import QtQuick.Templates as T

T.Button {
    id: control

    property bool primary: false

    implicitWidth: label.implicitWidth + (primary ? 56 : 36)
    implicitHeight: primary ? 52 : 38
    scale: down ? 0.96 : 1

    Behavior on scale {
        SpringAnimation { spring: 6; damping: 0.42; duration: Theme.ms(220) }
    }

    contentItem: Item {
        Text {
            id: label
            anchors.centerIn: parent
            text: control.text
            color: control.primary ? Theme.ink : control.hovered ? Theme.paper : Theme.muted
            opacity: control.enabled ? 1 : 0.45
            font.family: Theme.body
            font.weight: Font.DemiBold
            font.pixelSize: control.primary ? 17 : 14
        }
    }

    background: Rectangle {
        radius: height / 2
        color: control.primary ? (control.hovered ? "#FFDD6B" : Theme.signal) : control.hovered ? Theme.raised : "transparent"
        border.width: control.primary ? 0 : 1
        border.color: Theme.line
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.paper
            visible: control.visualFocus
        }
    }
}
