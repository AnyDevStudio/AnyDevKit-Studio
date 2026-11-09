import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// One project type in the New game form. A type Studio cannot make yet shows, but cannot be focused or picked.
T.AbstractButton {
    id: card

    required property var modelData

    property bool chosen: false

    enabled: modelData.available
    focusPolicy: modelData.available ? Qt.StrongFocus : Qt.NoFocus
    hoverEnabled: modelData.available
    padding: 14
    scale: down ? 0.985 : 1
    text: modelData.title
    Accessible.role: Accessible.RadioButton
    Accessible.name: modelData.note.length > 0 ? modelData.title + ", " + modelData.note.toLowerCase() : modelData.title
    Accessible.description: modelData.description
    Accessible.checked: chosen

    Behavior on scale {
        SpringAnimation { spring: 6; damping: 0.42; duration: Theme.ms(220) }
    }

    background: Rectangle {
        radius: 14
        color: !card.enabled ? "transparent" : card.chosen || card.hovered ? Theme.raised : Theme.panel
        border.width: card.chosen ? 2 : 1
        border.color: card.chosen ? Theme.signal : Theme.line

        Behavior on color {
            ColorAnimation { duration: Theme.ms(140); easing.type: Easing.OutCubic }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: parent.radius + 4
            color: "transparent"
            border.width: 2
            border.color: Theme.paper
            visible: card.visualFocus
        }
    }

    contentItem: RowLayout {
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 50
            Layout.preferredHeight: 50
            radius: 10
            color: card.chosen ? Theme.signal : card.enabled ? Theme.ink : "transparent"
            border.width: card.enabled ? 0 : 1
            border.color: Theme.line
            scale: card.chosen ? 1 : 0.92

            Behavior on color {
                ColorAnimation { duration: Theme.ms(140); easing.type: Easing.OutCubic }
            }

            Behavior on scale {
                SpringAnimation { spring: 5; damping: 0.42; duration: Theme.ms(240) }
            }

            ProjectTypeGlyph {
                anchors.centerIn: parent
                kind: card.modelData.id
                tint: card.chosen ? Theme.ink : card.enabled ? Theme.paper : Theme.muted
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    objectName: "projectTypeTitle"
                    text: card.modelData.title
                    color: card.enabled ? Theme.paper : Theme.muted
                    elide: Text.ElideRight
                    font.family: Theme.display
                    font.pixelSize: 19
                }

                Rectangle {
                    visible: card.modelData.note.length > 0
                    implicitWidth: note.implicitWidth + 16
                    implicitHeight: 22
                    radius: height / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.line

                    Text {
                        id: note

                        anchors.centerIn: parent
                        text: card.modelData.note
                        color: Theme.muted
                        font.family: Theme.body
                        font.pixelSize: 12
                    }
                }

                Rectangle {
                    visible: card.enabled
                    implicitWidth: 20
                    implicitHeight: 20
                    radius: 10
                    color: card.chosen ? Theme.signal : "transparent"
                    border.width: card.chosen ? 0 : 1
                    border.color: Theme.line

                    Rectangle {
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.ink
                        visible: card.chosen
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                objectName: "projectTypeDescription"
                text: card.modelData.description
                color: Theme.muted
                elide: Text.ElideRight
                font.family: Theme.body
                font.pixelSize: 14
            }
        }
    }
}
