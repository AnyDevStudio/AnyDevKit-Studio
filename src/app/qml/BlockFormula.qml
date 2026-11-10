import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A formula typed inside a block, such as "hp - max(1, atk - def)". Saves when focus leaves or Return is
// pressed; a formula the document refuses goes back to the saved one and the document says why.
RowLayout {
    id: row

    property string label
    property string value
    property color labelColor: Theme.muted
    property string inputName

    signal committed(string text)

    spacing: 6

    function commit() {
        if (input.text.trim() !== value) committed(input.text)
        input.text = Qt.binding(() => row.value)
    }

    function takeFocus() { input.forceActiveFocus(Qt.TabFocusReason); input.selectAll() }

    Text {
        visible: row.label.length > 0
        text: row.label
        color: row.labelColor
        font.family: Theme.mono
        font.pixelSize: 15
    }

    T.TextField {
        id: input

        objectName: row.inputName
        Accessible.name: row.label.length > 0 ? "Formula " + row.label : "Formula"

        implicitWidth: 252
        implicitHeight: 40
        leftPadding: 10
        rightPadding: 10
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.paper
        selectionColor: Theme.signal
        selectedTextColor: Theme.ink
        maximumLength: 400
        font.family: Theme.mono
        font.pixelSize: 15
        text: row.value
        placeholderText: "hp - 1"
        placeholderTextColor: Theme.muted
        onEditingFinished: row.commit()
        Keys.onEscapePressed: {
            text = row.value
            focus = false
        }

        background: Rectangle {
            radius: 10
            color: Theme.ink
            border.width: input.activeFocus ? 2 : 1
            border.color: input.activeFocus ? Theme.signal : Theme.line
        }
    }
}
