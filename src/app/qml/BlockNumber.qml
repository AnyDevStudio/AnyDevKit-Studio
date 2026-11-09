import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A number typed inside a block. Saves when focus leaves or Return is pressed.
RowLayout {
    id: row

    property string label
    property string value
    property bool whole: false
    property color labelColor: Theme.muted
    property string inputName

    signal committed(string text)

    spacing: 6

    function commit() {
        if (input.text.length === 0 || input.text === "-" || input.text === ".") input.text = value
        else if (input.text !== value) committed(input.text)
    }

    function takeFocus() { input.forceActiveFocus(Qt.TabFocusReason); input.selectAll() }

    onValueChanged: input.text = value

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
        property var commitEdit: function () { row.commit() }

        implicitWidth: row.whole ? 96 : 76
        implicitHeight: 40
        leftPadding: 10
        rightPadding: 10
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.paper
        selectionColor: Theme.signal
        selectedTextColor: Theme.ink
        maximumLength: 12
        font.family: Theme.mono
        font.pixelSize: 16
        text: row.value
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        validator: RegularExpressionValidator {
            regularExpression: row.whole ? /^-?\d{0,10}$/ : /^-?\d{0,7}(\.\d{0,4})?$/
        }
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
