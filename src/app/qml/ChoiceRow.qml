import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A short list of options as pills. `options` holds { value, label } objects.
ColumnLayout {
    id: row

    property string label
    property string value
    property var options: []

    signal picked(string value)

    spacing: 8

    Text {
        text: row.label
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 14
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6

        Repeater {
            model: row.options

            T.Button {
                id: choice

                required property var modelData

                readonly property bool chosen: row.value === modelData.value

                implicitWidth: implicitContentWidth + 24
                implicitHeight: 32
                text: modelData.label
                Accessible.role: Accessible.RadioButton
                Accessible.name: row.label + " " + modelData.label
                Accessible.checked: chosen
                onClicked: row.picked(modelData.value)

                background: Rectangle {
                    radius: height / 2
                    color: choice.chosen ? Theme.signal : choice.hovered ? Theme.raised : Theme.ink
                    border.width: 1
                    border.color: choice.chosen ? Theme.signal : Theme.line

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: height / 2
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.paper
                        visible: choice.visualFocus
                    }
                }

                contentItem: Text {
                    text: choice.text
                    color: choice.chosen ? Theme.ink : Theme.paper
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.body
                    font.weight: choice.chosen ? Font.DemiBold : Font.Normal
                    font.pixelSize: 14
                }
            }
        }
    }
}
