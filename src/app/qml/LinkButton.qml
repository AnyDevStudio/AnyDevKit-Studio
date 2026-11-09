import QtQuick
import QtQuick.Templates as T

// A quiet text action for the less common choice. Focus shows the same ring as the buttons.
T.AbstractButton {
    id: control

    implicitWidth: label.implicitWidth
    implicitHeight: Math.max(label.implicitHeight, 28)
    hoverEnabled: true
    padding: 0

    contentItem: Item {
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.hovered ? Theme.paper : Theme.muted
            font.family: Theme.body
            font.pixelSize: 15
            font.underline: true

            Behavior on color {
                ColorAnimation { duration: Theme.ms(120) }
            }
        }
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        radius: 6
        color: "transparent"
        border.width: 2
        border.color: Theme.paper
        visible: control.visualFocus
    }
}
