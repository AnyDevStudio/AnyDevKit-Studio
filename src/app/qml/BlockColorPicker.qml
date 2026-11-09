import QtQuick
import QtQuick.Templates as T

// A color inside a block: a swatch that opens the same colors the scene inspector offers.
T.Button {
    id: picker

    property string value: "#FFD23F"
    signal chosen(string color)

    implicitWidth: 40
    implicitHeight: 40
    Accessible.name: "Color " + value
    onClicked: pop.open()

    background: Rectangle {
        radius: width / 2
        color: picker.value
        border.width: 2
        border.color: picker.hovered ? Theme.paper : Theme.ink

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.paper
            visible: picker.visualFocus
        }
    }

    T.Popup {
        id: pop

        y: picker.height + 8
        width: 240
        padding: 14
        focus: true
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 14
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
        }

        contentItem: ColorRow {
            value: picker.value
            onPicked: (color) => {
                picker.chosen(color)
                if (color.length === 7) pop.close()
            }
        }
    }
}
