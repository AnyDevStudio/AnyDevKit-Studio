import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// The numbers a game remembers. Names and starting values are edited in place; a variable that is
// still in use is only deleted after the person has seen what uses it.
ColumnLayout {
    id: panel

    required property LogicDocument doc

    readonly property int rowHeight: 36

    spacing: 8

    function addVariable() {
        if (doc.addVariable(newName.text.trim())) newName.text = ""
    }

    function askToDelete(name) {
        const uses = doc.variableUses(name)
        if (uses.length === 0) {
            doc.removeVariable(name)
            return
        }
        confirm.variableName = name
        confirm.uses = uses
        confirm.open()
    }

    Text {
        text: "Variables"
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 14
        Layout.leftMargin: 2
    }

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: 2
        visible: panel.doc.variables.length === 0
        text: "A number your game remembers, like a score. Name one to use it in blocks and on the stage."
        color: Theme.muted
        wrapMode: Text.WordWrap
        font.family: Theme.body
        font.pixelSize: 13
        lineHeight: 1.25
    }

    component RowField: T.TextField {
        id: field

        property string placeholder
        // Lets the Build button save what is typed here, since a click on it does not take the focus.
        property var commitEdit: function () { field.editingFinished() }

        implicitHeight: panel.rowHeight
        leftPadding: 10
        rightPadding: 10
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.paper
        selectionColor: Theme.signal
        selectedTextColor: Theme.ink
        font.family: Theme.body
        font.pixelSize: 15

        background: Rectangle {
            radius: 8
            color: Theme.ink
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? Theme.signal : Theme.line

            Text {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: field.placeholder.length > 0 && field.text.length === 0 && !field.activeFocus
                text: field.placeholder
                color: Theme.muted
                font.family: Theme.body
                font.pixelSize: 14
            }
        }
    }

    Flickable {
        id: rows

        Layout.fillWidth: true
        // Room for a few rows, then the list scrolls so the blocks below stay reachable.
        implicitHeight: Math.min(rowsColumn.implicitHeight, 3 * panel.rowHeight + 2 * 6)
        contentHeight: rowsColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        visible: panel.doc.variables.length > 0

        ColumnLayout {
            id: rowsColumn

            width: rows.width
            spacing: 6

            Repeater {
                // Only the count: a row is rebuilt only when one is added or removed, never while typing.
                model: panel.doc.variables.length

                RowLayout {
                    id: row

                    required property int index
                    readonly property var variable: panel.doc.variables[index] ?? ({ name: "", initial: 0 })

                    Layout.fillWidth: true
                    spacing: 6

                    // Typing ends the fields' bindings, so a change from undo or a rename is copied in.
                    onVariableChanged: {
                        if (!nameField.activeFocus) nameField.text = variable.name
                        if (!initialField.activeFocus) initialField.text = String(variable.initial)
                    }

                    RowField {
                        id: nameField

                        objectName: "variableName_" + row.index
                        Layout.fillWidth: true
                        Accessible.name: "Name of variable " + row.variable.name
                        maximumLength: 24
                        text: row.variable.name
                        onEditingFinished: {
                            if (text === row.variable.name) return
                            if (!panel.doc.renameVariable(row.variable.name, text)) text = row.variable.name
                        }
                        Keys.onEscapePressed: {
                            text = row.variable.name
                            focus = false
                        }
                    }

                    RowField {
                        id: initialField

                        objectName: "variableInitial_" + row.index
                        Layout.preferredWidth: 62
                        Accessible.name: "Value " + row.variable.name + " starts at"
                        font.family: Theme.mono
                        font.pixelSize: 14
                        maximumLength: 11
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: RegularExpressionValidator { regularExpression: /^-?\d{0,10}$/ }
                        text: String(row.variable.initial)
                        onEditingFinished: {
                            if (text === String(row.variable.initial)) return
                            if (text.length === 0 || text === "-" || !panel.doc.setVariableInitial(row.variable.name, Number(text))) text = String(row.variable.initial)
                        }
                    }

                    // One value for the game, one per player, or one per copy of a template. A click moves to the next.
                    T.Button {
                        id: scopeButton

                        readonly property string scope: row.variable.scope ?? "game"
                        readonly property string nextScope: scope === "game" ? "player" : scope === "player" ? "copy" : "game"

                        objectName: "variableScope_" + row.index
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: panel.rowHeight
                        Accessible.name: "Variable " + row.variable.name + " has one value for " + (scope === "game" ? "the game" : scope === "player" ? "each player" : "each copy") + ". Change to " + nextScope
                        onClicked: panel.doc.setVariableScope(row.variable.name, nextScope)

                        contentItem: Text {
                            text: scopeButton.scope === "game" ? "game" : scopeButton.scope === "player" ? "player" : "copy"
                            color: scopeButton.scope === "game" ? Theme.muted : Theme.signal
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.family: Theme.mono
                            font.pixelSize: 12
                        }

                        background: Rectangle {
                            radius: 8
                            color: scopeButton.hovered ? Theme.ink : "transparent"
                            border.width: scopeButton.visualFocus ? 2 : 1
                            border.color: scopeButton.visualFocus ? Theme.paper : Theme.line
                        }
                    }

                    T.Button {
                        id: deleteButton

                        objectName: "variableDelete_" + row.index
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: panel.rowHeight
                        Accessible.name: "Delete variable " + row.variable.name
                        onClicked: panel.askToDelete(row.variable.name)

                        contentItem: Text {
                            text: "×"
                            color: deleteButton.hovered ? Theme.bad : Theme.muted
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 19
                        }

                        background: Rectangle {
                            radius: 8
                            color: deleteButton.hovered ? Theme.ink : "transparent"
                            border.width: deleteButton.visualFocus ? 2 : 0
                            border.color: Theme.paper
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        RowField {
            id: newName

            objectName: "newVariableName"
            Layout.fillWidth: true
            Accessible.name: "New variable name"
            maximumLength: 24
            placeholder: "New name"
            onAccepted: panel.addVariable()
        }

        PillButton {
            objectName: "addVariableButton"
            implicitHeight: panel.rowHeight
            text: "Add"
            enabled: newName.text.trim().length > 0
            onClicked: panel.addVariable()
        }
    }

    // Asks before a variable that is still in use goes away, and shows exactly what will need fixing.
    T.Popup {
        id: confirm

        property string variableName: ""
        property var uses: []
        readonly property int shownUses: 6

        parent: T.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 96 : 520, 520)
        // A template popup does not size itself, and a height of 0 leaves the card without its background.
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        modal: true
        focus: true
        padding: 30
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

        T.Overlay.modal: Rectangle { color: Qt.rgba(0.08, 0.11, 0.23, 0.72) }

        enter: Transition {
            ParallelAnimation {
                SpringAnimation { property: "scale"; from: Theme.reducedMotion ? 1 : 0.94; to: 1; spring: 5; damping: 0.42; duration: Theme.ms(260) }
                NumberAnimation { property: "opacity"; from: Theme.reducedMotion ? 1 : 0; to: 1; duration: Theme.ms(120) }
            }
        }

        background: Rectangle {
            radius: 22
            color: Theme.panel
            border.width: 2
            border.color: Theme.signal
        }

        contentItem: ColumnLayout {
            spacing: 18

            Text {
                Layout.fillWidth: true
                text: "“" + confirm.variableName + "” is still in use."
                color: Theme.paper
                wrapMode: Text.WordWrap
                font.family: Theme.display
                font.pixelSize: 32
                lineHeight: 1.0
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Repeater {
                    model: confirm.uses.slice(0, confirm.shownUses)

                    RowLayout {
                        id: use

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: 10

                        // A block mark or a sample of the pixel font, so blocks and text read apart at a glance.
                        Item {
                            implicitWidth: 34
                            implicitHeight: 34

                            BlockGlyph {
                                anchors.centerIn: parent
                                visible: use.modelData.kind === "block"
                                kind: "variables"
                                tint: Theme.signal
                            }

                            PixelText {
                                anchors.centerIn: parent
                                visible: use.modelData.kind === "text"
                                width: 26
                                height: 10
                                text: "Abc"
                                color: Theme.signal
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: use.modelData.title
                            color: Theme.paper
                            elide: Text.ElideRight
                            font.family: Theme.body
                            font.weight: Font.DemiBold
                            font.pixelSize: 15
                        }

                        Text {
                            text: use.modelData.where
                            color: Theme.muted
                            elide: Text.ElideRight
                            font.family: Theme.body
                            font.pixelSize: 14
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: confirm.uses.length > confirm.shownUses
                    text: "and " + (confirm.uses.length - confirm.shownUses) + " more"
                    color: Theme.muted
                    font.family: Theme.body
                    font.pixelSize: 14
                }
            }

            Text {
                Layout.fillWidth: true
                text: "Delete it and these will need a different variable before the game can build. You can undo this."
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 15
                lineHeight: 1.25
            }

            RowLayout {
                spacing: 10

                PillButton {
                    objectName: "keepVariableButton"
                    primary: true
                    text: "Keep it"
                    onClicked: confirm.close()
                }

                PillButton {
                    objectName: "deleteVariableButton"
                    text: "Delete anyway"
                    onClicked: {
                        panel.doc.removeVariable(confirm.variableName)
                        confirm.close()
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }
    }
}
