import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Set up PS5 tools": the first thing someone sees before their SDK and dependencies exist.
Item {
    id: view

    required property StudioController studio

    readonly property SetupFlow flow: studio.setup
    readonly property string phase: flow.phase
    readonly property bool roomy: height > 640
    readonly property var missingTools: flow.tools.filter((tool) => !tool.ok && !tool.checking)
    readonly property string toolSummary: flow.tools.filter((tool) => tool.ok && !tool.checking).map((tool) => tool.name + " " + tool.version).join(" · ")

    signal finished()
    signal useExistingFolders()

    Component.onCompleted: {
        if (!flow.running) flow.checkTools()
        primaryButton.forceActiveFocus()
    }

    Connections {
        target: view.flow
        function onFinished() { view.finished() }
    }

    function primaryAction() {
        if (phase === "running") flow.cancel()
        else if (phase === "needsTools") flow.checkTools()
        else flow.start()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Math.max(40, parent.width * 0.06)
        spacing: Math.max(32, parent.width * 0.05)

        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.54
            Layout.maximumWidth: parent.width * 0.54

            // Centred when it fits; when a long list of missing tools does not, the checklist
            // scrolls and the button stays on screen below it.
            ColumnLayout {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Math.min(parent.height, implicitHeight)
                spacing: 18

                Flickable {
                    id: scroller

                    // Wider than the column so the clip does not cut off focus rings at its edges.
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    Layout.preferredHeight: column.implicitHeight
                    Layout.leftMargin: -6
                    Layout.rightMargin: -6
                    contentWidth: width
                    contentHeight: column.implicitHeight
                    interactive: column.implicitHeight > height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: column

                        x: 6
                        width: scroller.width - 12
                        spacing: 16

                        Text {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 2
                            text: view.roomy ? "Set up\nPS5 tools." : "Set up PS5 tools."
                            color: Theme.paper
                            font.family: Theme.display
                            font.pixelSize: view.roomy ? Math.min(68, column.width * 0.1, view.height * 0.09) : Math.min(40, column.width * 0.08)
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 24
                            lineHeight: 0.94
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 520
                            text: "Studio downloads AnyDevKit and the PS5 libraries into its own folder. It takes a few minutes, and you only do this once."
                            color: Theme.muted
                            font.family: Theme.body
                            font.pixelSize: view.roomy ? 17 : 15
                            lineHeight: 1.25
                            wrapMode: Text.WordWrap
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: view.flow.steps

                                ColumnLayout {
                                    required property var modelData
                                    required property int index

                                    Layout.fillWidth: true
                                    spacing: 8

                                    SetupStepRow {
                                        Layout.fillWidth: true
                                        number: parent.index + 1
                                        title: parent.modelData.title
                                        state_: parent.modelData.state
                                        detail: parent.modelData.id === "tools" && parent.modelData.state === "done" && view.toolSummary.length > 0 ? view.toolSummary : parent.modelData.detail
                                        progress: parent.modelData.progress
                                    }

                                    // What to install, shown right under the step that needs it.
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 44
                                        visible: parent.modelData.id === "tools" && view.missingTools.length > 0
                                        spacing: 10

                                        Text {
                                            Layout.fillWidth: true
                                            visible: view.flow.toolsHint.length > 0
                                            text: view.flow.toolsHint
                                            color: Theme.signal
                                            font.family: Theme.body
                                            font.pixelSize: 14
                                            wrapMode: Text.WordWrap
                                        }

                                        Repeater {
                                            model: view.missingTools

                                            ColumnLayout {
                                                id: toolEntry

                                                required property var modelData

                                                Layout.fillWidth: true
                                                spacing: 4

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: toolEntry.modelData.problem
                                                    color: Theme.paper
                                                    font.family: Theme.body
                                                    font.pixelSize: 14
                                                    wrapMode: Text.WordWrap
                                                }

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 8

                                                    Rectangle {
                                                        Layout.fillWidth: true
                                                        Layout.preferredHeight: 38
                                                        radius: 8
                                                        color: Theme.ink
                                                        border.width: 1
                                                        border.color: Theme.line

                                                        Text {
                                                            anchors.fill: parent
                                                            anchors.leftMargin: 12
                                                            anchors.rightMargin: 12
                                                            text: toolEntry.modelData.command
                                                            verticalAlignment: Text.AlignVCenter
                                                            elide: Text.ElideRight
                                                            color: Theme.paper
                                                            font.family: Theme.mono
                                                            font.pixelSize: 13
                                                            textFormat: Text.PlainText
                                                        }
                                                    }

                                                    PillButton {
                                                        id: copyButton

                                                        text: copied.running ? "Copied" : "Copy"
                                                        Accessible.name: "Copy the command for " + toolEntry.modelData.name
                                                        onClicked: {
                                                            view.studio.copyText(toolEntry.modelData.command)
                                                            copied.restart()
                                                        }

                                                        Timer { id: copied; interval: Theme.reducedMotion ? 2200 : 1400 }
                                                    }

                                                    PillButton {
                                                        text: "Check again"
                                                        Accessible.name: "Check " + toolEntry.modelData.name + " again"
                                                        onClicked: view.flow.checkTool(toolEntry.modelData.id)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.topMargin: 2
                    spacing: 18

                    PillButton {
                        id: primaryButton

                        primary: view.phase !== "running"
                        enabled: view.phase !== "checking"
                        text: view.phase === "checking" ? "Checking..."
                            : view.phase === "needsTools" ? "Check again"
                            : view.phase === "running" ? "Cancel"
                            : view.phase === "failed" ? "Retry"
                            : view.phase === "cancelled" ? "Continue"
                            : "Set up PS5 tools"
                        onClicked: view.primaryAction()
                    }

                    LinkButton {
                        enabled: !view.flow.running
                        opacity: enabled ? 1 : 0.5
                        text: "Use existing folders"
                        onClicked: view.useExistingFolders()
                    }
                }
            }
        }

        // The console's screen shows what is happening, inside the same safe-area frame as the welcome screen.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: screen

                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height * 16 / 9)
                height: width * 9 / 16
                color: Theme.panel
                radius: 6
                clip: true

                SafeAreaFrame { anchors.fill: parent }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Math.max(16, screen.width * 0.08)
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: "$ adk setup --target ps5"
                        color: Theme.signal
                        font.family: Theme.mono
                        font.pixelSize: Math.max(11, screen.width * 0.026)
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    ListView {
                        id: output

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: view.flow.log
                        boundsBehavior: Flickable.StopAtBounds
                        onCountChanged: positionViewAtEnd()

                        delegate: Text {
                            required property string display

                            readonly property bool problem: /^Stopped:|(^|\s)(fatal )?error:|Error:/.test(display)

                            width: ListView.view.width
                            text: display
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            color: problem ? Theme.bad : Theme.paper
                            opacity: problem ? 1 : 0.86
                            font.family: Theme.mono
                            font.pixelSize: Math.max(11, screen.width * 0.024)
                            lineHeight: 1.2
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: output.count === 0
                            text: "Progress appears here."
                            color: Theme.muted
                            font.family: Theme.body
                            font.pixelSize: Math.max(12, screen.width * 0.03)
                        }
                    }
                }
            }
        }
    }
}
