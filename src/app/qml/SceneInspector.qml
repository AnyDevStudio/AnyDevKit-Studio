import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// Properties of the selected item, or of the scene when nothing is selected.
Flickable {
    id: inspector

    required property SceneDocument scene

    signal replaceImageRequested()

    readonly property var item: scene.selected
    readonly property bool hasItem: item.type !== undefined
    readonly property bool isText: item.type === "text"
    readonly property bool isParticles: item.type === "particles"
    readonly property bool isImage: item.type === "image"

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

        Item {
            Layout.fillWidth: true
            visible: inspector.isText
            implicitHeight: textField.implicitHeight

            Field {
                id: textField

                anchors.left: parent.left
                anchors.right: parent.right
                label: "Text"
                maximumLength: 128 // {{ and }} count as one character, so the raw text can run longer than 64
                value: inspector.isText ? inspector.item.text : ""
                onCommitted: (text) => inspector.set("text", text)
            }

            BlockPicker {
                objectName: "insertVariableButton"
                anchors.right: parent.right
                implicitHeight: 24
                preferredWidth: 148
                placeholder: "Insert variable"
                choices: Array.from(inspector.scene.variableNames).map((name) => ({ id: name, name: name }))
                emptyText: "Add a variable on the Logic tab first."
                onChosen: (name) => textField.insertAtCursor("{" + name + "}")
            }
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

        ChoiceRow {
            Layout.fillWidth: true
            visible: inspector.isParticles
            label: "Effect"
            value: inspector.isParticles ? inspector.item.preset : ""
            options: [
                { value: "fireworks", label: "Fireworks" },
                { value: "fountain", label: "Fountain" },
                { value: "sparks", label: "Sparks" },
                { value: "snow", label: "Snow" },
                { value: "rain", label: "Rain" }
            ]
            onPicked: (value) => inspector.set("preset", value)
        }

        ChoiceRow {
            Layout.fillWidth: true
            visible: inspector.isParticles
            label: "Colors"
            value: inspector.isParticles ? inspector.item.palette : ""
            options: [
                { value: "color", label: "One color" },
                { value: "festival", label: "Festival" },
                { value: "fire", label: "Fire" },
                { value: "ice", label: "Ice" }
            ]
            onPicked: (value) => inspector.set("palette", value)
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: inspector.isImage
            spacing: 6

            Text {
                text: "Image file"
                color: Theme.muted
                font.family: Theme.body
                font.pixelSize: 14
            }

            Text {
                Layout.fillWidth: true
                text: inspector.isImage ? inspector.item.source : ""
                color: Theme.paper
                elide: Text.ElideMiddle
                font.family: Theme.mono
                font.pixelSize: 13
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: inspector.isImage ? inspector.item.imageProblem : ""
                color: Theme.bad
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 14
            }

            PillButton {
                Layout.fillWidth: true
                Layout.topMargin: 4
                primary: inspector.isImage && inspector.item.imageProblem.length > 0
                text: "Replace image"
                onClicked: inspector.replaceImageRequested()
            }
        }

        ColorRow {
            Layout.fillWidth: true
            visible: inspector.hasItem && !inspector.isImage && (!inspector.isParticles || inspector.item.palette === "color")
            value: inspector.hasItem ? inspector.item.color : ""
            onPicked: (color) => inspector.set("color", color)
        }

        // The limits here are the ones validateScene checks, in docs/scene-format.md.
        SliderRow {
            Layout.fillWidth: true
            visible: inspector.isParticles
            label: "Amount"
            from: 10
            to: 8000
            stepSize: 10
            value: inspector.isParticles ? inspector.item.amount : 0
            onCommitted: (value) => inspector.set("amount", value)
        }

        SliderRow {
            Layout.fillWidth: true
            visible: inspector.isParticles
            label: "Spread"
            suffix: "%"
            value: inspector.isParticles ? inspector.item.spread : 0
            onCommitted: (value) => inspector.set("spread", value)
        }

        SliderRow {
            Layout.fillWidth: true
            visible: inspector.isParticles
            label: "Speed"
            suffix: "%"
            from: 10
            to: 300
            stepSize: 5
            value: inspector.isParticles ? inspector.item.speed : 100
            onCommitted: (value) => inspector.set("speed", value)
        }

        SliderRow {
            Layout.fillWidth: true
            visible: inspector.isParticles
            label: "Gravity"
            suffix: "%"
            from: -300
            to: 300
            stepSize: 5
            value: inspector.isParticles ? inspector.item.gravity : 100
            onCommitted: (value) => inspector.set("gravity", value)
        }

        SliderRow {
            Layout.fillWidth: true
            visible: inspector.isParticles
            label: "Pixel size"
            from: 2
            to: 32
            value: inspector.isParticles ? inspector.item.pixelSize : 8
            onCommitted: (value) => inspector.set("pixelSize", value)
        }

        T.Switch {
            id: loopToggle

            Layout.fillWidth: true
            visible: inspector.isParticles
            implicitHeight: 34
            checked: inspector.isParticles && inspector.item.looping
            onToggled: inspector.set("looping", checked)

            indicator: Rectangle {
                width: 44
                height: 26
                radius: 13
                anchors.verticalCenter: parent.verticalCenter
                color: loopToggle.checked ? Theme.signal : Theme.raised
                border.width: loopToggle.visualFocus ? 2 : 0
                border.color: Theme.paper

                Rectangle {
                    width: 20
                    height: 20
                    radius: 10
                    y: 3
                    x: loopToggle.checked ? 21 : 3
                    color: loopToggle.checked ? Theme.ink : Theme.paper

                    Behavior on x {
                        SpringAnimation { spring: 6; damping: 0.45; duration: Theme.ms(200) }
                    }
                }
            }

            contentItem: Text {
                leftPadding: 54
                verticalAlignment: Text.AlignVCenter
                text: loopToggle.checked ? "Keeps going" : "Plays once"
                color: Theme.paper
                font.family: Theme.body
                font.pixelSize: 15
            }
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
