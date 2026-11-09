import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

ColumnLayout {
    id: field

    property string label
    property string value
    property int maximumLength: 64
    property string hint
    property string problem
    readonly property string text: input.text

    signal committed(string text)
    signal edited(string text)
    signal accepted()

    // Saves what is typed without waiting for the field to lose focus.
    function commit() { if (input.text !== value) committed(input.text) }
    function focusInput() { input.forceActiveFocus() }
    function setText(newText) { value = newText; input.text = newText }

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
        onAccepted: field.accepted()

        background: Rectangle {
            radius: 8
            color: Theme.ink
            border.width: input.activeFocus ? 2 : 1
            border.color: input.activeFocus ? Theme.signal : field.problem.length > 0 ? Theme.bad : Theme.line
        }
    }

    Text {
        Layout.fillWidth: true
        visible: text.length > 0
        text: field.problem || field.hint
        color: field.problem.length > 0 ? Theme.bad : Theme.muted
        wrapMode: Text.WordWrap
        font.family: Theme.body
        font.pixelSize: 13
    }
}
