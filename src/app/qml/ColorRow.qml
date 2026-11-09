import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

ColumnLayout {
    id: row

    property string label: "Color"
    property string value

    readonly property var swatches: ["#151C3A", "#F3F1E2", "#FFD23F", "#FF8373", "#67E3C8", "#5B8DEF", "#B57BFF", "#000000"]

    signal picked(string color)

    spacing: 8

    Text {
        text: row.label
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 14
    }

    Flow {
        Layout.fillWidth: true
        spacing: 8

        Repeater {
            model: row.swatches

            T.Button {
                id: swatch

                required property string modelData

                implicitWidth: 30
                implicitHeight: 30
                Accessible.name: "Color " + modelData
                onClicked: row.picked(modelData)

                background: Rectangle {
                    radius: width / 2
                    color: swatch.modelData
                    border.width: 2
                    border.color: swatch.visualFocus ? Theme.paper : swatch.modelData.toUpperCase() === row.value.toUpperCase() ? Theme.signal : Theme.line
                }
            }
        }
    }

    Field {
        Layout.fillWidth: true
        label: "Hex"
        value: row.value
        maximumLength: 7
        onCommitted: (text) => row.picked(text)
    }
}
