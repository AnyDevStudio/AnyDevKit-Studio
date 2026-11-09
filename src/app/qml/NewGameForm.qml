import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: form

    required property StudioController studio

    signal closeRequested()

    property bool attempted: false
    property bool editingTools: false
    property bool choosingType: true
    property string selectedType: studio.defaultProjectType

    readonly property string gameName: nameField.text
    readonly property var selectedInfo: studio.projectTypes.find((type) => type.id === selectedType) ?? ({})
    readonly property string noun: selectedInfo.noun ?? "game"
    readonly property string nameProblem: attempted || gameName.length > 0 ? studio.gameNameProblem(gameName, noun) : ""
    readonly property string parentProblem: attempted || parentField.text.length > 0 ? studio.parentFolderProblem(parentField.text, noun) : ""
    readonly property string sdkProblem: attempted || sdkField.text.length > 0 ? studio.sdkFolderProblem(sdkField.text) : ""
    readonly property string dependencyProblem: attempted || dependencyField.text.length > 0 ? studio.dependencyFolderProblem(dependencyField.text) : ""
    readonly property bool toolsReady: studio.sdkRoot.length > 0 && studio.dependencyRoot.length > 0 && studio.sdkFolderProblem(studio.sdkRoot).length === 0 && studio.dependencyFolderProblem(studio.dependencyRoot).length === 0
    readonly property bool showTools: !choosingType && (!toolsReady || editingTools)
    readonly property bool showDetails: !choosingType && !showTools
    readonly property string page: choosingType ? "type" : showTools ? "tools" : "details"
    readonly property real cardHeight: Math.max(78, Math.min(96, form.height * 0.15))
    readonly property string destination: nameProblem.length === 0 && gameName.length > 0 && parentProblem.length === 0 && parentField.text.length > 0 ? parentField.text.replace(/\/+$/, "") + "/" + gameName : ""

    onPageChanged: {
        scroller.contentY = 0
        pageEntrance.restart()
    }

    function prepare() {
        attempted = false
        editingTools = false
        choosingType = true
        selectedType = studio.defaultProjectType
        nameField.setText("")
        parentField.setText(studio.projectsFolder())
        sdkField.setText(studio.sdkRoot)
        dependencyField.setText(studio.dependencyRoot)
        studio.clearNewGameError()
        focusChosenCard()
    }

    // The chosen card takes focus without its ring, because no key has been pressed yet.
    function focusChosenCard() {
        for (let index = 0; index < typeCards.count; ++index) {
            const card = typeCards.itemAt(index)
            if (card.modelData.id === selectedType) card.forceActiveFocus()
        }
    }

    // Arrow keys move through the types that can be chosen and choose as they go, like radio buttons.
    function moveChoice(delta) {
        const cards = []
        for (let index = 0; index < typeCards.count; ++index) {
            if (typeCards.itemAt(index).enabled) cards.push(typeCards.itemAt(index))
        }
        if (cards.length === 0) return
        const here = cards.findIndex((card) => card.activeFocus)
        const start = here >= 0 ? here : cards.findIndex((card) => card.modelData.id === selectedType)
        const next = cards[(start + delta + cards.length) % cards.length]
        selectedType = next.modelData.id
        next.forceActiveFocus(Qt.TabFocusReason)
    }

    function continueFromType() {
        choosingType = false
        if (showTools) sdkField.focusInput()
        else nameField.focusInput()
    }

    function submit() {
        if (choosingType) continueFromType()
        else if (showTools) submitTools()
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
        studio.createGame(gameName, parentField.text, studio.sdkRoot, studio.dependencyRoot, selectedType)
    }

    function goBack() {
        if (studio.creatingGame) return
        if (choosingType) {
            closeRequested()
        } else if (showTools && toolsReady) {
            attempted = false
            editingTools = false
            sdkField.setText(studio.sdkRoot)
            dependencyField.setText(studio.dependencyRoot)
            nameField.focusInput()
        } else {
            attempted = false
            editingTools = false
            studio.clearNewGameError()
            choosingType = true
            focusChosenCard()
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

    ParallelAnimation {
        id: pageEntrance

        NumberAnimation { target: column; property: "opacity"; from: 0; to: 1; duration: Theme.ms(200); easing.type: Easing.OutCubic }
        NumberAnimation { target: columnShift; property: "x"; from: 14; to: 0; duration: Theme.ms(240); easing.type: Easing.OutCubic }
    }

    Flickable {
        id: scroller

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
            transform: Translate { id: columnShift }

            Text {
                readonly property bool roomy: form.height > 620

                Layout.fillWidth: true
                Layout.bottomMargin: 4
                text: form.choosingType ? (roomy ? "What are\nyou making?" : "What are you making?")
                    : form.showTools ? (roomy ? "First, find\nyour tools." : "First, find your tools.")
                    : (roomy ? "Name your\n" + form.noun + "." : "Name your " + form.noun + ".")
                color: Theme.paper
                font.family: Theme.display
                font.pixelSize: roomy ? Math.min(72, form.width * 0.095, form.height * 0.1) : Math.min(44, form.width * 0.085)
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 24
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

            ColumnLayout {
                Layout.fillWidth: true
                visible: form.choosingType
                spacing: 10

                Repeater {
                    id: typeCards

                    model: form.studio.projectTypes

                    ProjectTypeCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: form.cardHeight
                        chosen: form.selectedType === modelData.id
                        onClicked: form.selectedType = modelData.id
                        Keys.onUpPressed: form.moveChoice(-1)
                        Keys.onLeftPressed: form.moveChoice(-1)
                        Keys.onDownPressed: form.moveChoice(1)
                        Keys.onRightPressed: form.moveChoice(1)
                        Keys.onReturnPressed: {
                            form.selectedType = modelData.id
                            form.submit()
                        }
                        Keys.onEnterPressed: {
                            form.selectedType = modelData.id
                            form.submit()
                        }
                    }
                }
            }

            Field {
                id: nameField

                Layout.fillWidth: true
                visible: form.showDetails
                label: "Name"
                hint: "English letters, digits and underscores. It becomes the folder and title."
                problem: form.nameProblem
                onAccepted: form.submit()
                onTextChanged: form.studio.clearNewGameError()
            }

            RowLayout {
                Layout.fillWidth: true
                visible: form.showDetails
                spacing: 10

                Field {
                    id: parentField

                    Layout.fillWidth: true
                    label: "Create in"
                    maximumLength: 4096
                    hint: form.destination.length > 0 ? "Creates " + form.destination : "The " + form.noun + " gets its own folder inside this one."
                    problem: form.parentProblem
                    onAccepted: form.submit()
                    onTextChanged: form.studio.clearNewGameError()
                }

                PillButton {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 24
                    text: "Choose"
                    onClicked: form.chooseFolder(parentField, "Choose where to create the " + form.noun)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: form.showDetails
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
                visible: form.showDetails && form.studio.newGameError.length > 0
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
                    text: !form.showDetails ? "Continue" : form.studio.creatingGame ? "Creating..." : "Create " + form.noun
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
