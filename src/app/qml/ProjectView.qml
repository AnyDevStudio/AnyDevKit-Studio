import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

Item {
    id: view

    required property StudioController studio

    property string mode: "scene"

    Component.onCompleted: mode = studio.openingTab
    onModeChanged: if (mode === "logic") logicView.focusCanvas()

    // The hidden scene editor keeps its own undo shortcut, so on this tab undo is claimed here first.
    Keys.onShortcutOverride: (event) => { if (mode === "logic" && event.matches(StandardKey.Undo)) event.accepted = true }
    Keys.onPressed: (event) => {
        if (mode === "logic" && event.matches(StandardKey.Undo)) {
            studio.logic.undo()
            event.accepted = true
        }
    }
    readonly property string revealLabel: Qt.platform.os === "osx" ? "Show in Finder" : "Show in folder"

    Connections {
        target: view.studio
        function onBuildChanged() { if (view.studio.building || view.studio.buildState === "failed" || view.studio.buildState === "blocked") view.mode = "build" }
        function onLogicBlockedBuild() {
            view.mode = "logic"
            logicView.showFirstProblem()
        }
    }

    component ModeTab: T.Button {
        id: tab

        required property string target

        implicitWidth: label.implicitWidth + 32
        implicitHeight: 38
        onClicked: view.mode = target

        contentItem: Item {
            Text {
                id: label
                anchors.centerIn: parent
                text: tab.text
                color: view.mode === tab.target ? Theme.ink : tab.hovered ? Theme.paper : Theme.muted
                font.family: Theme.body
                font.weight: Font.DemiBold
                font.pixelSize: 15
            }
        }

        background: Rectangle {
            radius: height / 2
            color: view.mode === tab.target ? Theme.paper : "transparent"

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: height / 2
                color: "transparent"
                border.width: 2
                border.color: Theme.paper
                visible: tab.visualFocus
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 96
            color: Theme.ink

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: view.width < 1100 ? 12 : 24

                ColumnLayout {
                    Layout.fillWidth: true
                    // Room for the start of the name even when the window is at its smallest.
                    Layout.minimumWidth: 96
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: view.studio.titleName || view.studio.projectName
                        color: Theme.paper
                        elide: Text.ElideRight
                        font.family: Theme.display
                        font.pixelSize: 36
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 20
                    }

                    Text {
                        Layout.fillWidth: true
                        text: view.studio.titleId + "   Version " + view.studio.contentVersion
                        color: Theme.muted
                        elide: Text.ElideRight
                        font.family: Theme.body
                        font.pixelSize: 14
                    }
                }

                RowLayout {
                    spacing: 4

                    ModeTab { target: "scene"; text: "Scene" }
                    ModeTab { target: "logic"; text: "Logic" }
                    ModeTab { target: "game"; text: "Game page" }
                    ModeTab { target: "build"; text: "Code and build" }
                }

                ExportFormatPicker {
                    studio: view.studio
                    Layout.alignment: Qt.AlignVCenter
                }

                PillButton {
                    objectName: "buildButton"
                    primary: true
                    visible: !view.studio.building
                    enabled: !view.studio.gamePage.busy
                    text: view.studio.buildActionLabel
                    onClicked: {
                        gamePageView.commitPendingName()
                        logicView.commitPendingEdits()
                        view.studio.build()
                    }
                }

                PillButton {
                    visible: view.studio.building
                    text: "Cancel build"
                    onClicked: view.studio.cancelBuild()
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: view.mode === "scene" ? 0 : view.mode === "logic" ? 1 : view.mode === "game" ? 2 : 3

            Item {
                SceneView {
                    anchors.fill: parent
                    visible: view.studio.scene.loaded
                    scene: view.studio.scene
                    studio: view.studio
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 96, 620)
                    visible: !view.studio.scene.loaded
                    spacing: 22

                    Text {
                        Layout.fillWidth: true
                        text: "Design this game by dragging."
                        color: Theme.paper
                        wrapMode: Text.WordWrap
                        font.family: Theme.display
                        font.pixelSize: 52
                        lineHeight: 0.96
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Studio adds its game runtime to this project. Files it replaces are copied to .studio-backup first, and your scene builds with the same button."
                        color: Theme.muted
                        wrapMode: Text.WordWrap
                        font.family: Theme.body
                        font.pixelSize: 18
                        lineHeight: 1.3
                    }

                    PillButton {
                        primary: true
                        text: "Add the scene editor"
                        onClicked: view.studio.addSceneEditor()
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: view.studio.scene.error.length > 0
                        text: view.studio.scene.error
                        color: Theme.bad
                        wrapMode: Text.WordWrap
                        font.family: Theme.body
                        font.pixelSize: 15
                    }
                }
            }

            LogicView {
                id: logicView
                studio: view.studio
            }

            GamePageView {
                id: gamePageView
                studio: view.studio
            }

            RowLayout {
                spacing: 0

                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 272
                    color: Theme.panel

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Text {
                            text: "Files"
                            color: Theme.paper
                            font.family: Theme.body
                            font.weight: Font.DemiBold
                            font.pixelSize: 17
                            Layout.leftMargin: 10
                        }

                        FileList {
                            studio: view.studio
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }

                        Text {
                            text: "Open code with"
                            color: Theme.muted
                            font.family: Theme.body
                            font.pixelSize: 14
                            Layout.leftMargin: 10
                            Layout.topMargin: 4
                        }

                        EditorPicker {
                            studio: view.studio
                            Layout.fillWidth: true
                        }

                        PillButton {
                            visible: view.studio.selectedEditorName.length > 0
                            text: "Open project in " + view.studio.selectedEditorName
                            Layout.fillWidth: true
                            onClicked: view.studio.openProjectInEditor()
                        }

                        Text {
                            visible: view.studio.editorError.length > 0
                            text: view.studio.editorError
                            color: Theme.bad
                            wrapMode: Text.WordWrap
                            font.family: Theme.body
                            font.pixelSize: 14
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                        }

                        PillButton {
                            text: "Open another project"
                            enabled: !view.studio.building
                            Layout.fillWidth: true
                            onClicked: view.studio.closeProject()
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 28
                    spacing: 22

                    StageLane {
                        stages: view.studio.stages
                        Layout.fillWidth: true
                    }

                    PkgKeysPanel {
                        studio: view.studio
                        visible: view.studio.exportFormat === "pkg"
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        visible: view.studio.buildMessage.length > 0

                        Text {
                            Layout.fillWidth: true
                            text: view.studio.buildState === "done" && view.studio.outputName ? view.studio.outputName + " is ready, " + view.studio.outputSize + "." : view.studio.buildMessage
                            color: view.studio.buildState === "done" ? Theme.good : view.studio.buildState === "failed" || view.studio.buildState === "blocked" ? Theme.bad : Theme.muted
                            wrapMode: Text.WordWrap
                            font.family: Theme.body
                            font.weight: Font.Medium
                            font.pixelSize: 17
                        }

                        PillButton {
                            visible: view.studio.buildState === "done"
                            text: view.revealLabel
                            onClicked: view.studio.revealOutput()
                        }

                        PillButton {
                            visible: view.studio.buildState === "failed"
                            text: "Copy log"
                            onClicked: view.studio.copyLog()
                        }
                    }

                    LogPanel {
                        studio: view.studio
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
        }
    }
}
