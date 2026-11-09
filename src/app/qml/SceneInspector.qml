import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// Properties of the selected item, or of the scene when nothing is selected.
Flickable {
    id: inspector

    required property SceneDocument scene

    readonly property var item: scene.selected
    readonly property bool hasItem: item.type !== undefined
    readonly property bool isText: item.type === "text"

    function set(name, value) { scene.setItemValue(scene.selectedIndex, name, value) }

    contentHeight: column.implicitHeight + 8
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ColumnLayout {
        id: column

        width: inspector.width
        spacing: 16

        Text {
            Layout.fillWidth: true
            text: inspector.hasItem ? (inspector.item.name || "Item") : "Scene"
            elide: Text.ElideRight
            color: Theme.paper
            font.family: Theme.body
            font.weight: Font.DemiBold
            font.pixelSize: 17
        }

        ColorRow {
            Layout.fillWidth: true
            visible: !inspector.hasItem
            label: "Background"
            value: inspector.scene.background
            onPicked: (color) => inspector.scene.setBackground(color)
        }

        Field {
            Layout.fillWidth: true
            visible: inspector.hasItem
            label: "Name"
            value: inspector.hasItem ? inspector.item.name : ""
            onCommitted: (text) => inspector.set("name", text)
        }

        Field {
            Layout.fillWidth: true
            visible: inspector.isText
            label: "Text"
            value: inspector.isText ? inspector.item.text : ""
            onCommitted: (text) => inspector.set("text", text)
        }

        RowLayout {
            Layout.fillWidth: true
            visible: inspector.hasItem
            spacing: 10

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "X"
                value: inspector.hasItem ? String(Math.round(inspector.item.x)) : ""
                maximumLength: 5
                onCommitted: (text) => inspector.set("x", Number(text))
            }

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Y"
                value: inspector.hasItem ? String(Math.round(inspector.item.y)) : ""
                maximumLength: 5
                onCommitted: (text) => inspector.set("y", Number(text))
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: inspector.hasItem && !inspector.isText
            spacing: 10

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Width"
                value: inspector.hasItem ? String(Math.round(inspector.item.width)) : ""
                maximumLength: 5
                onCommitted: (text) => inspector.set("width", Number(text))
            }

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Height"
                value: inspector.hasItem ? String(Math.round(inspector.item.height)) : ""
                maximumLength: 5
                onCommitted: (text) => inspector.set("height", Number(text))
            }
        }

        Field {
            Layout.fillWidth: true
            visible: inspector.isText
            label: "Size (1 to 40)"
            value: inspector.isText ? String(inspector.item.scale) : ""
            maximumLength: 2
            onCommitted: (text) => inspector.set("scale", Number(text))
        }

        ColorRow {
            Layout.fillWidth: true
            visible: inspector.hasItem
            value: inspector.hasItem ? inspector.item.color : ""
            onPicked: (color) => inspector.set("color", color)
        }

        T.Switch {
            id: toggle

            Layout.fillWidth: true
            visible: inspector.hasItem
            implicitHeight: 34
            checked: inspector.hasItem && inspector.item.controllable
            onToggled: inspector.set("controllable", checked)

            indicator: Rectangle {
                width: 44
                height: 26
                radius: 13
                anchors.verticalCenter: parent.verticalCenter
                color: toggle.checked ? Theme.signal : Theme.raised
                border.width: toggle.visualFocus ? 2 : 0
                border.color: Theme.paper

                Rectangle {
                    width: 20
                    height: 20
                    radius: 10
                    y: 3
                    x: toggle.checked ? 21 : 3
                    color: toggle.checked ? Theme.ink : Theme.paper

                    Behavior on x {
                        SpringAnimation { spring: 6; damping: 0.45; duration: Theme.ms(200) }
                    }
                }
            }

            contentItem: Text {
                leftPadding: 54
                verticalAlignment: Text.AlignVCenter
                text: "Moves with the controller"
                color: Theme.paper
                font.family: Theme.body
                font.pixelSize: 15
            }
        }

        PillButton {
            Layout.fillWidth: true
            visible: inspector.hasItem
            text: "Delete"
            onClicked: inspector.scene.removeItem(inspector.scene.selectedIndex)
        }

        Text {
            Layout.fillWidth: true
            visible: inspector.scene.error.length > 0
            text: inspector.scene.error
            color: Theme.bad
            wrapMode: Text.WordWrap
            font.family: Theme.body
            font.pixelSize: 14
        }
    }
}
