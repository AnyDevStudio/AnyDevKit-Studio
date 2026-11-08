import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: view

    required property StudioController studio

    FolderDialog {
        id: folderDialog
        title: "Choose an AnyDevKit project"
        onAccepted: view.studio.openFolder(selectedFolder)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Math.max(40, parent.width * 0.06)
        spacing: Math.max(32, parent.width * 0.05)

        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.54
            Layout.maximumWidth: parent.width * 0.54
            spacing: 26

            Item { Layout.fillHeight: true }

            Text {
                Layout.fillWidth: true
                text: "Make something\nfor PS5."
                color: Theme.paper
                font.family: Theme.display
                font.pixelSize: Math.min(104, view.width * 0.053)
                lineHeight: 0.94
                wrapMode: Text.NoWrap
            }

            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                text: "Open a folder made with adk init. Studio shows the project and builds its PS5 package."
                color: Theme.muted
                font.family: Theme.body
                font.pixelSize: 19
                lineHeight: 1.3
                wrapMode: Text.WordWrap
            }

            RowLayout {
                spacing: 18

                PillButton {
                    primary: true
                    text: "Open project folder"
                    onClicked: folderDialog.open()
                }

                Text {
                    text: "or drop it anywhere here"
                    color: Theme.muted
                    font.family: Theme.body
                    font.pixelSize: 15
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                visible: view.studio.openError.length > 0
                text: view.studio.openError
                color: Theme.bad
                font.family: Theme.body
                font.pixelSize: 15
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillHeight: true }
        }

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

                // The same bouncing title that adk init generates, running where a new project will.
                Text {
                    id: bouncing

                    property real vx: 0.17
                    property real vy: 0.11
                    property real px: 0.3
                    property real py: 0.34

                    x: px * (screen.width - width)
                    y: py * (screen.height - height)
                    text: "MyApp"
                    color: Theme.signal
                    font.family: Theme.display
                    font.pixelSize: Math.max(20, screen.width * 0.09)
                }

                FrameAnimation {
                    running: !Theme.reducedMotion
                    onTriggered: {
                        bouncing.px += bouncing.vx * frameTime
                        bouncing.py += bouncing.vy * frameTime * 1.7
                        if (bouncing.px <= 0 || bouncing.px >= 1) bouncing.vx = -bouncing.vx
                        if (bouncing.py <= 0 || bouncing.py >= 1) bouncing.vy = -bouncing.vy
                        bouncing.px = Math.min(1, Math.max(0, bouncing.px))
                        bouncing.py = Math.min(1, Math.max(0, bouncing.py))
                    }
                }
            }
        }
    }
}
