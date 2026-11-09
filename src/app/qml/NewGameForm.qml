import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: form

    required property StudioController studio

    signal closeRequested()

    property bool attempted: false
    property bool editingTools: false

    readonly property string gameName: nameField.text
    readonly property string nameProblem: attempted || gameName.length > 0 ? studio.gameNameProblem(gameName) : ""
    readonly property string parentProblem: attempted || parentField.text.length > 0 ? studio.parentFolderProblem(parentField.text) : ""
    readonly property string sdkProblem: attempted || sdkField.text.length > 0 ? studio.sdkFolderProblem(sdkField.text) : ""
    readonly property string dependencyProblem: attempted || dependencyField.text.length > 0 ? studio.dependencyFolderProblem(dependencyField.text) : ""
    readonly property bool toolsReady: studio.sdkRoot.length > 0 && studio.dependencyRoot.length > 0 && studio.sdkFolderProblem(studio.sdkRoot).length === 0 && studio.dependencyFolderProblem(studio.dependencyRoot).length === 0
    readonly property bool showTools: !toolsReady || editingTools
    readonly property string destination: nameProblem.length === 0 && gameName.length > 0 && parentProblem.length === 0 && parentField.text.length > 0 ? parentField.text.replace(/\/+$/, "") + "/" + gameName : ""

    function prepare() {
        attempted = false
        editingTools = false
        nameField.setText("")
        parentField.setText("")
        sdkField.setText(studio.sdkRoot)
        dependencyField.setText(studio.dependencyRoot)
        studio.clearNewGameError()
        if (showTools) sdkField.focusInput()
        else nameField.focusInput()
    }

    function submit() {
        if (showTools) submitTools()
        else submitGame()
    }

    function submitTools() {
        attempted = true
        if (sdkProblem.length > 0) { sdkField.focusInput(); return }
        if (dependencyProblem.length > 0) { dependencyField.focusInput(); return }
        studio.rememberSdkRoot(sdkField.text)
        studio.rememberDependencyRoot(dependencyField.text)
        attempted = false
        editingTools = false
        nameField.focusInput()
    }

    function submitGame() {
        if (studio.creatingGame) return
        attempted = true
        if (nameProblem.length > 0) { nameField.focusInput(); return }
        if (parentProblem.length > 0) { parentField.focusInput(); return }
        studio.createGame(gameName, parentField.text, studio.sdkRoot, studio.dependencyRoot)
    }

    function goBack() {
        if (showTools && toolsReady) {
            attempted = false
            editingTools = false
            sdkField.setText(studio.sdkRoot)
            dependencyField.setText(studio.dependencyRoot)
            nameField.focusInput()
        } else {
            closeRequested()
        }
    }

    function changeTools() {
        attempted = false
        editingTools = true
        sdkField.focusInput()
    }

    function chooseFolder(field, title) {
        folderDialog.target = field
        folderDialog.title = title
        folderDialog.open()
    }

    Keys.onEscapePressed: goBack()

    FolderDialog {
        id: folderDialog

        property Field target

        onAccepted: {
            target.setText(form.studio.localPath(selectedFolder))
            if (target === sdkField) form.studio.rememberSdkRoot(target.text)
            if (target === dependencyField) form.studio.rememberDependencyRoot(target.text)
        }
    }

    Flickable {
        // Wider than the form so the clip does not cut off the keyboard focus rings at its edges.
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight)
        interactive: column.implicitHeight > height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column

            x: 6
            y: Math.max(0, (parent.height - implicitHeight) / 2)
            width: parent.width - 12
            spacing: 18

            Text {
                readonly property bool roomy: form.height > 620

                Layout.fillWidth: true
                Layout.bottomMargin: 4
                text: form.showTools ? (roomy ? "First, find\nyour tools." : "First, find your tools.") : (roomy ? "Name your\ngame." : "Name your game.")
                color: Theme.paper
                font.family: Theme.display
                font.pixelSize: roomy ? Math.min(72, form.width * 0.095, form.height * 0.1) : Math.min(44, form.width * 0.085)
                lineHeight: 0.94
            }

            Text {
                Layout.fillWidth: true
                visible: form.showTools
                text: "Studio remembers these two folders, so you only do this once."
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 16
            }

            Field {
                id: nameField

                Layout.fillWidth: true
                visible: !form.showTools
                label: "Name"
                hint: "English letters, digits and underscores. It becomes the folder and title."
                problem: form.nameProblem
                onAccepted: form.submit()
                onTextChanged: form.studio.clearNewGameError()
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !form.showTools
                spacing: 10

                Field {
                    id: parentField

                    Layout.fillWidth: true
                    label: "Create in"
                    maximumLength: 4096
                    hint: form.destination.length > 0 ? "Creates " + form.destination : "The game gets its own folder inside this one."
                    problem: form.parentProblem
                    onAccepted: form.submit()
                    onTextChanged: form.studio.clearNewGameError()
                }

                PillButton {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 24
                    text: "Choose"
                    onClicked: form.chooseFolder(parentField, "Choose where to create the game")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !form.showTools
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: ["SDK  " + form.studio.sdkRoot, "Deps " + form.studio.dependencyRoot]

                        Text {
                            required property string modelData

                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.muted
                            elide: Text.ElideMiddle
                            font.family: Theme.mono
                            font.pixelSize: 12
                        }
                    }
                }

                PillButton {
                    text: "Change"
                    onClicked: form.changeTools()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: form.showTools
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Field {
                        id: sdkField

                        Layout.fillWidth: true
                        label: "AnyDevKit SDK"
                        maximumLength: 4096
                        hint: "The folder that contains bin/adk."
                        value: form.studio.sdkRoot
                        problem: form.sdkProblem
                        onAccepted: form.submit()
                        onCommitted: (path) => form.studio.rememberSdkRoot(path)
                    }

                    PillButton {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 24
                        text: "Choose"
                        onClicked: form.chooseFolder(sdkField, "Choose the AnyDevKit SDK folder")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Field {
                        id: dependencyField

                        Layout.fillWidth: true
                        label: "Dependencies"
                        maximumLength: 4096
                        hint: "The folder made by adk setup."
                        value: form.studio.dependencyRoot
                        problem: form.dependencyProblem
                        onAccepted: form.submit()
                        onCommitted: (path) => form.studio.rememberDependencyRoot(path)
                    }

                    PillButton {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 24
                        text: "Choose"
                        onClicked: form.chooseFolder(dependencyField, "Choose the dependency folder")
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: !form.showTools && form.studio.newGameError.length > 0
                text: form.studio.newGameError
                color: Theme.bad
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 15
            }

            RowLayout {
                Layout.topMargin: 4
                spacing: 14

                PillButton {
                    primary: true
                    enabled: !form.studio.creatingGame
                    text: form.showTools ? "Continue" : form.studio.creatingGame ? "Creating..." : "Create game"
                    onClicked: form.submit()
                }

                PillButton {
                    enabled: !form.studio.creatingGame
                    text: "Back"
                    onClicked: form.goBack()
                }
            }
        }
    }
}
