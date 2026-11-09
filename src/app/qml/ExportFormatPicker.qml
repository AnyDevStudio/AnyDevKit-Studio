import QtQuick
import QtQuick.Templates as T

// The compact export choice beside the build action. Arrow keys move the choice, like radio buttons.
Rectangle {
    id: picker

    required property StudioController studio

    function move(step) {
        const formats = studio.exportFormats
        const current = formats.findIndex((format) => format.id === studio.exportFormat)
        const next = (current + step + formats.length) % formats.length
        studio.exportFormat = formats[next].id
        segmentButtons.itemAt(next).forceActiveFocus(Qt.TabFocusReason)
    }

    implicitWidth: segments.implicitWidth + 8
    implicitHeight: 38
    radius: height / 2
    color: "transparent"
    border.width: 1
    border.color: Theme.line
    opacity: studio.building ? 0.5 : 1
    Accessible.role: Accessible.Grouping
    Accessible.name: "Export format"

    Row {
        id: segments

        anchors.centerIn: parent
        spacing: 2

        Repeater {
            id: segmentButtons

            model: picker.studio.exportFormats

            T.Button {
                id: segment

                required property var modelData

                readonly property bool chosen: picker.studio.exportFormat === modelData.id

                implicitWidth: label.implicitWidth + 24
                implicitHeight: 30
                enabled: !picker.studio.building
                focusPolicy: Qt.StrongFocus
                text: modelData.label
                Accessible.role: Accessible.RadioButton
                Accessible.name: "Export format " + modelData.label
                Accessible.checked: chosen
                onClicked: picker.studio.exportFormat = modelData.id
                Keys.onLeftPressed: picker.move(-1)
                Keys.onRightPressed: picker.move(1)

                contentItem: Item {
                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: segment.text
                        color: segment.chosen ? Theme.ink : segment.hovered ? Theme.paper : Theme.muted
                        font.family: Theme.body
                        font.weight: segment.chosen ? Font.DemiBold : Font.Normal
                        font.pixelSize: 13
                        font.letterSpacing: 0.4

                        Behavior on color {
                            ColorAnimation { duration: Theme.ms(120) }
                        }
                    }
                }

                background: Rectangle {
                    radius: height / 2
                    color: segment.chosen ? Theme.signal : segment.hovered ? Theme.raised : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.ms(120) }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: height / 2
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.paper
                        visible: segment.visualFocus
                    }
                }
            }
        }
    }
}
