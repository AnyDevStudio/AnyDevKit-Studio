import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// The sounds a game can play. Importing a file copies it into the project; a sound still used by
// blocks is only deleted after the person has seen what uses it. Each sound shows its waveform,
// and plays it too when this copy of Studio was built with Qt Multimedia.
ColumnLayout {
    id: panel

    required property LogicDocument doc
    required property StudioController studio

    readonly property int rowHeight: 34
    property string previewing: ""
    property var previewData: ({})

    spacing: 8

    function togglePreview(sound) {
        if (previewing === sound.name) {
            previewing = ""
            studio.stopSoundPreview()
            return
        }
        previewData = studio.soundPreview(sound.source)
        previewing = sound.name
        if (previewData.error === undefined && studio.canPlaySoundPreview) studio.playSoundPreview(sound.source)
    }

    function askToDelete(name) {
        if (previewing === name) previewing = ""
        const uses = doc.soundUses(name)
        if (uses.length === 0) {
            doc.removeSound(name)
            return
        }
        confirm.soundName = name
        confirm.uses = uses
        confirm.open()
    }

    function formatDuration(seconds) {
        if (seconds < 60) return seconds.toFixed(1) + " s"
        const minutes = Math.floor(seconds / 60)
        return minutes + ":" + String(Math.floor(seconds - minutes * 60)).padStart(2, "0")
    }

    Text {
        text: "Sounds"
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 14
        Layout.leftMargin: 2
    }

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: 2
        visible: panel.doc.sounds.length === 0
        text: "Effects and music from a WAV file. Import one to use it in blocks."
        color: Theme.muted
        wrapMode: Text.WordWrap
        font.family: Theme.body
        font.pixelSize: 13
        lineHeight: 1.25
    }

    Flickable {
        id: rows

        Layout.fillWidth: true
        implicitHeight: Math.min(rowsColumn.implicitHeight, 4 * panel.rowHeight + 3 * 6)
        contentHeight: rowsColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        visible: panel.doc.sounds.length > 0

        ColumnLayout {
            id: rowsColumn

            width: rows.width
            spacing: 6

            Repeater {
                model: panel.doc.sounds

                ColumnLayout {
                    id: row

                    required property var modelData
                    required property int index
                    readonly property bool expanded: panel.previewing === modelData.name

                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        T.Button {
                            id: previewButton

                            objectName: "soundPreview_" + row.index
                            Layout.preferredWidth: panel.rowHeight
                            Layout.preferredHeight: panel.rowHeight
                            Accessible.name: (panel.studio.canPlaySoundPreview ? "Preview " : "Show waveform of ") + row.modelData.name
                            onClicked: panel.togglePreview(row.modelData)

                            contentItem: Text {
                                text: row.expanded ? "■" : "▶"
                                color: previewButton.hovered ? Theme.signal : Theme.paper
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                            }

                            background: Rectangle {
                                radius: 8
                                color: Theme.ink
                                border.width: previewButton.visualFocus ? 2 : 1
                                border.color: previewButton.visualFocus ? Theme.signal : Theme.line
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            color: Theme.paper
                            elide: Text.ElideRight
                            font.family: Theme.body
                            font.pixelSize: 15
                        }

                        T.Button {
                            id: deleteButton

                            objectName: "soundDelete_" + row.index
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: panel.rowHeight
                            Accessible.name: "Delete sound " + row.modelData.name
                            onClicked: panel.askToDelete(row.modelData.name)

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

                    // The waveform and what the build will do with the sound.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: row.expanded ? previewContents.implicitHeight + 16 : 0
                        visible: row.expanded
                        radius: 8
                        color: Theme.ink
                        border.color: Theme.line
                        border.width: 1

                        ColumnLayout {
                            id: previewContents

                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                visible: row.expanded && panel.previewData.error !== undefined
                                text: panel.previewData.error !== undefined ? panel.previewData.error : ""
                                color: Theme.bad
                                wrapMode: Text.WordWrap
                                font.family: Theme.body
                                font.pixelSize: 13
                            }

                            Row {
                                visible: row.expanded && panel.previewData.error === undefined && panel.previewData.peaks !== undefined
                                spacing: 1
                                height: 34

                                Repeater {
                                    model: row.expanded && panel.previewData.peaks !== undefined ? panel.previewData.peaks : []

                                    Rectangle {
                                        required property var modelData
                                        width: 3
                                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                                        height: Math.max(2, modelData * 34)
                                        radius: 1
                                        color: Theme.signal
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: row.expanded && panel.previewData.error === undefined
                                text: panel.previewData.seconds !== undefined
                                    ? panel.formatDuration(panel.previewData.seconds) + " · " + panel.previewData.channels + (panel.previewData.channels === 1 ? " channel · " : " channels · ") + Math.round(panel.previewData.sampleRate) + " Hz"
                                        + (panel.studio.canPlaySoundPreview ? "" : " · waveform only; this copy of Studio cannot play sound")
                                    : ""
                                color: Theme.muted
                                wrapMode: Text.WordWrap
                                font.family: Theme.body
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }

    PillButton {
        objectName: "importSoundButton"
        implicitHeight: panel.rowHeight
        text: "Import"
        Layout.fillWidth: true
        onClicked: fileDialog.open()
    }

    FileDialog {
        id: fileDialog

        title: "Import a sound"
        nameFilters: panel.studio.canDecodeCompressedSound ? ["Sounds (*.wav *.mp3 *.ogg *.oga)", "All files (*)"] : ["WAV sounds (*.wav)", "All files (*)"]
        onAccepted: panel.studio.importSound(selectedFile)
    }

    T.Popup {
        id: confirm

        property string soundName: ""
        property var uses: []

        parent: T.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 96 : 520, 520)
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
                text: "“" + confirm.soundName + "” is still in use."
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
                    model: confirm.uses.slice(0, 6)

                    RowLayout {
                        id: use

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: 10

                        BlockGlyph {
                            kind: "sound"
                            tint: Theme.signal
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
            }

            Text {
                Layout.fillWidth: true
                text: "Delete it and these will need a different sound before the game can build. You can undo this."
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 15
                lineHeight: 1.25
            }

            RowLayout {
                spacing: 10

                PillButton {
                    objectName: "keepSoundButton"
                    primary: true
                    text: "Keep it"
                    onClicked: confirm.close()
                }

                PillButton {
                    objectName: "deleteSoundButton"
                    text: "Delete anyway"
                    onClicked: {
                        panel.doc.removeSound(confirm.soundName)
                        confirm.close()
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }
    }
}
