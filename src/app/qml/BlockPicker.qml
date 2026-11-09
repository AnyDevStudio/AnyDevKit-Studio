import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A value inside a block chosen from a list: a scene item, a variable, a button or a comparison.
T.Button {
    id: picker

    property var choices: []        // [{id, name}]
    property string current: ""
    property string shown: ""       // what the button says
    property string placeholder: "Choose"
    property string emptyText: "Add an item in the Scene tab first."
    property bool invalid: false
    property real preferredWidth: 170
    signal chosen(string id)

    implicitWidth: preferredWidth
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 28
    Accessible.name: placeholder + ": " + (shown || "nothing chosen")
    onClicked: menu.open()

    contentItem: Text {
        text: picker.shown || picker.placeholder
        color: picker.shown ? (picker.invalid ? Theme.bad : Theme.paper) : Theme.muted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.family: Theme.body
        font.weight: Font.Medium
        font.pixelSize: 15
    }

    indicator: Text {
        x: picker.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.muted
        font.pixelSize: 14
    }

    background: Rectangle {
        radius: 10
        color: Theme.ink
        border.width: picker.invalid ? 2 : picker.hovered ? 1.5 : 1
        border.color: picker.invalid ? Theme.bad : picker.hovered ? Theme.muted : Theme.line

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 13
            color: "transparent"
            border.width: 2
            border.color: Theme.paper
            visible: picker.visualFocus
        }
    }

    T.Popup {
        id: menu

        y: picker.height + 6
        width: Math.max(picker.width, 200)
        padding: 6
        modal: false
        focus: true
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
        implicitHeight: Math.min(column.implicitHeight + 2 * padding, 320)

        onAboutToShow: list.currentIndex = picker.choices.findIndex((choice) => choice.id === picker.current)

        background: Rectangle {
            radius: 12
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
        }

        contentItem: Flickable {
            id: scroller
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: column
                width: scroller.width
                spacing: 2

                Text {
                    visible: picker.choices.length === 0
                    width: parent.width
                    padding: 8
                    text: picker.emptyText
                    color: Theme.muted
                    wrapMode: Text.WordWrap
                    font.family: Theme.body
                    font.pixelSize: 14
                }

                Repeater {
                    id: list
                    property int currentIndex: -1
                    model: picker.choices

                    T.ItemDelegate {
                        id: option

                        required property var modelData
                        required property int index

                        objectName: "option_" + modelData.id
                        width: column.width
                        height: 40
                        text: modelData.name
                        focus: index === list.currentIndex
                        Accessible.name: modelData.name
                        Keys.onReturnPressed: click()
                        Keys.onEnterPressed: click()
                        KeyNavigation.down: list.itemAt(index + 1)
                        KeyNavigation.up: list.itemAt(index - 1)

                        onClicked: {
                            menu.close()
                            picker.chosen(modelData.id)
                        }

                        contentItem: Text {
                            leftPadding: 10
                            text: option.text
                            color: option.modelData.id === picker.current ? Theme.signal : Theme.paper
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            font.family: Theme.body
                            font.weight: option.modelData.id === picker.current ? Font.DemiBold : Font.Normal
                            font.pixelSize: 15
                        }

                        background: Rectangle {
                            radius: 8
                            color: option.highlighted || option.hovered || option.visualFocus ? Theme.raised : "transparent"
                        }
                    }
                }
            }
        }
    }
}
