import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import AnyDevKitStudio

Item {
    id: gameView

    required property StudioController studio
    readonly property GamePageDocument page: studio.gamePage
    readonly property bool canEdit: !studio.building && !page.busy

    // What is typed but not saved yet, so the preview follows every keystroke.
    property var typedName: null
    readonly property string shownName: typedName !== null ? typedName : page.title

    function commitPendingName() { nameField.commit() }

    Connections {
        target: gameView.studio
        function onProjectChanged() { gameView.typedName = null }
    }

    FileDialog {
        id: picker

        property string targetKind

        title: "Choose a picture"
        nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: gameView.page.chooseImage(targetKind, selectedFile)
    }

    component ImageSlot: RowLayout {
        id: slot

        required property string kind
        required property string name
        required property string pixels
        required property string hint
        required property url image
        property bool wide: true
        property bool removable: false

        signal removeClicked()

        spacing: 12

        // A fixed column keeps the text of every row aligned, whatever the picture's shape.
        Item {
            Layout.preferredWidth: 96
            Layout.preferredHeight: 54
            Layout.alignment: Qt.AlignTop

            Rectangle {
                width: slot.wide ? 96 : 54
                height: 54
                radius: 6
                color: Theme.ink
                border.width: 1
                border.color: Theme.line

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: slot.image
                    cache: false
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 240
                    visible: slot.image.toString().length > 0

                    Behavior on opacity { NumberAnimation { duration: Theme.ms(180) } }
                    opacity: status === Image.Ready ? 1 : 0
                }

                Text {
                    anchors.centerIn: parent
                    visible: slot.image.toString().length === 0
                    text: "Empty"
                    color: Theme.line
                    font.family: Theme.mono
                    font.pixelSize: 11
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            RowLayout {
                spacing: 8

                Text {
                    text: slot.name
                    color: Theme.paper
                    font.family: Theme.body
                    font.weight: Font.DemiBold
                    font.pixelSize: 16
                }

                Text {
                    text: slot.pixels
                    color: Theme.signal
                    font.family: Theme.mono
                    font.pixelSize: 12
                }
            }

            Text {
                Layout.fillWidth: true
                text: slot.hint
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 13
                lineHeight: 1.2
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: 6

            PillButton {
                Layout.alignment: Qt.AlignRight
                text: "Choose"
                enabled: gameView.canEdit
                Accessible.name: "Choose a picture for the " + slot.name.toLowerCase()
                onClicked: {
                    picker.targetKind = slot.kind
                    picker.open()
                }
            }

            PillButton {
                Layout.alignment: Qt.AlignRight
                visible: slot.removable
                text: "Remove"
                enabled: gameView.canEdit
                Accessible.name: "Remove the " + slot.name.toLowerCase()
                onClicked: slot.removeClicked()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 440
            color: Theme.panel

            Flickable {
                id: formScroll

                anchors.fill: parent
                contentHeight: form.implicitHeight + 56
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: form

                    x: 26
                    y: 28
                    width: formScroll.width - 52
                    spacing: 18

                    Text {
                        Layout.fillWidth: true
                        text: "On the console"
                        color: Theme.paper
                        wrapMode: Text.WordWrap
                        font.family: Theme.display
                        font.pixelSize: 30
                        lineHeight: 0.96
                    }

                    Field {
                        id: nameField

                        Layout.fillWidth: true
                        label: "Game name"
                        value: gameView.page.title
                        enabled: !gameView.studio.building
                        onCommitted: (text) => gameView.page.setTitle(text)
                        onEdited: (text) => gameView.typedName = text
                    }

                    ImageSlot {
                        Layout.fillWidth: true
                        kind: "icon"
                        name: "Icon"
                        pixels: "512 x 512"
                        hint: "Studio crops the picture to a square from its center."
                        image: gameView.page.iconUrl
                        wide: false
                    }

                    ImageSlot {
                        Layout.fillWidth: true
                        kind: "background"
                        name: "Background"
                        pixels: "3840 x 2160"
                        hint: "Fills the screen behind your game."
                        image: gameView.page.backgroundUrl
                    }

                    ImageSlot {
                        Layout.fillWidth: true
                        kind: "splash"
                        name: "Splash"
                        pixels: "3840 x 2160"
                        hint: gameView.page.splashUrl.toString().length > 0 ? "Shows while your game loads." : "Optional. Your background is used until you add one."
                        image: gameView.page.splashUrl
                        removable: gameView.page.splashUrl.toString().length > 0
                        onRemoveClicked: gameView.page.removeSplash()
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: gameView.page.error.length > 0 ? gameView.page.error : gameView.studio.building ? "A build is running. Changes unlock when it finishes." : gameView.page.status
                        color: gameView.page.error.length > 0 ? Theme.bad : Theme.good
                        wrapMode: Text.WordWrap
                        font.family: Theme.body
                        font.weight: Font.Medium
                        font.pixelSize: 14
                        lineHeight: 1.2
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: stageArea

                anchors.fill: parent
                anchors.margins: 30

                readonly property real captionSpace: 40
                readonly property real stageWidth: Math.min(width, (height - captionSpace) * 16 / 9)

                Rectangle {
                    id: bezel

                    x: (stageArea.width - width) / 2
                    y: Math.max(0, (stageArea.height - height - stageArea.captionSpace) / 2)
                    width: stageArea.stageWidth
                    height: width * 9 / 16
                    radius: 14
                    color: Theme.raised

                    ConsolePreview {
                        anchors.fill: parent
                        anchors.margins: 5
                        name: gameView.shownName
                        icon: gameView.page.iconUrl
                        background: gameView.page.backgroundUrl
                    }
                }

                Text {
                    anchors.top: bezel.bottom
                    anchors.left: bezel.left
                    anchors.right: bezel.right
                    anchors.topMargin: 12
                    text: "A sketch of your game on a home screen. The console draws the real one in its own way."
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    font.family: Theme.body
                    font.pixelSize: 13
                }
            }
        }
    }
}
