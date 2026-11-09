import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

ColumnLayout {
    id: field

    property string label
    property string value
    property int maximumLength: 64

    signal committed(string text)
    signal edited(string text)

    // Saves what is typed without waiting for the field to lose focus.
    function commit() { if (input.text !== value) committed(input.text) }

    spacing: 6

    onValueChanged: input.text = value

    Text {
        text: field.label
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 14
    }

    T.TextField {
        id: input

        Layout.fillWidth: true
        implicitHeight: 38
        leftPadding: 12
        rightPadding: 12
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.paper
        selectionColor: Theme.signal
        selectedTextColor: Theme.ink
        maximumLength: field.maximumLength
        font.family: Theme.body
        font.pixelSize: 15
        text: field.value
        onEditingFinished: field.commit()
        onTextEdited: field.edited(text)

        background: Rectangle {
            radius: 8
            color: Theme.ink
            border.width: input.activeFocus ? 2 : 1
            border.color: input.activeFocus ? Theme.signal : Theme.line
        }
    }
}
