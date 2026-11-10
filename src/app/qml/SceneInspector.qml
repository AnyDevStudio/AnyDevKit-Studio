import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// Properties of the selected item, or of the scene when nothing is selected.
Flickable {
    id: inspector

    required property SceneDocument scene

    property string tilemapTool: ""
    property int paintTile: 0
    property bool solidMode: false
    property string previewAnimation: ""

    signal replaceImageRequested()
    signal tilemapToolPicked(string tool)
    signal paintTilePicked(int tile)
    signal solidModeToggled(bool solid)
    signal previewAnimationPicked(string name)

    readonly property var item: scene.selected
    readonly property bool hasItem: item.type !== undefined
    readonly property bool isText: item.type === "text"
    readonly property bool isParticles: item.type === "particles"
    readonly property bool isImage: item.type === "image"
    readonly property bool isTilemap: item.type === "tilemap"
    readonly property bool isSheet: isImage && item.frameWidth > 0
    readonly property bool canHavePhysics: item.type === "rectangle" || isImage
    readonly property string physicsMode: hasItem ? (item.physics || "") : ""
    readonly property bool isBody: physicsMode === "body"

    function set(name, value) { scene.setItemValue(scene.selectedIndex, name, value) }

    // A small pill for the actions inside an animation's row.
    component MiniButton: T.Button {
        id: mini

        implicitHeight: 26
        implicitWidth: implicitContentWidth + 18
        leftPadding: 9
        rightPadding: 9
        focusPolicy: Qt.TabFocus

        contentItem: Text {
            text: mini.text
            color: Theme.paper
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.body
            font.pixelSize: 13
        }

        background: Rectangle {
            radius: height / 2
            color: mini.hovered ? Theme.line : Theme.raised
            border.width: mini.visualFocus ? 2 : 0
            border.color: Theme.paper
        }
    }

    // The same switch the Loop and controller settings use, for a yes or no about the item.
    component ToggleRow: T.Switch {
        id: rowSwitch

        signal toggledTo(bool checked)
        onToggled: rowSwitch.toggledTo(checked)
        implicitHeight: 34

        indicator: Rectangle {
            width: 44
            height: 26
            radius: 13
            anchors.verticalCenter: parent.verticalCenter
            color: rowSwitch.checked ? Theme.signal : Theme.raised
            border.width: rowSwitch.visualFocus ? 2 : 0
            border.color: Theme.paper

            Rectangle {
                width: 20
                height: 20
                radius: 10
                y: 3
                x: rowSwitch.checked ? 21 : 3
                color: rowSwitch.checked ? Theme.ink : Theme.paper

                Behavior on x {
                    SpringAnimation { spring: 6; damping: 0.45; duration: Theme.ms(200) }
                }
            }
        }

        contentItem: Text {
            leftPadding: 54
            verticalAlignment: Text.AlignVCenter
            text: rowSwitch.text
            color: Theme.paper
            font.family: Theme.body
            font.pixelSize: 15
        }
    }

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

        // The world can be larger than the 1920x1080 screen. The stage pans and zooms over it, and
        // the camera blocks move through it while the game runs.
        RowLayout {
            Layout.fillWidth: true
            visible: !inspector.hasItem
            spacing: 10

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "World width"
                value: String(inspector.scene.worldWidth)
                maximumLength: 5
                onCommitted: (text) => inspector.scene.setWorldSize(Number(text), inspector.scene.worldHeight)
            }

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "World height"
                value: String(inspector.scene.worldHeight)
                maximumLength: 5
                onCommitted: (text) => inspector.scene.setWorldSize(inspector.scene.worldWidth, Number(text))
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !inspector.hasItem
            spacing: 10

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Camera X"
                value: String(Math.round(inspector.scene.cameraX))
                maximumLength: 5
                onCommitted: (text) => inspector.scene.setCameraStart(Number(text), inspector.scene.cameraY)
            }

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Camera Y"
                value: String(Math.round(inspector.scene.cameraY))
                maximumLength: 5
                onCommitted: (text) => inspector.scene.setCameraStart(inspector.scene.cameraX, Number(text))
            }
        }

        Field {
            Layout.fillWidth: true
            visible: !inspector.hasItem
            label: "Camera tilt %"
            value: String(inspector.scene.tilt)
            maximumLength: 2
            onCommitted: (text) => inspector.scene.setTilt(Number(text))
        }

        Text {
            Layout.fillWidth: true
            visible: !inspector.hasItem && inspector.scene.tilt > 0
            text: "2.5D. The game leans the ground away from the viewer. Turn on Stands upright for trees, people and houses."
            color: Theme.muted
            wrapMode: Text.WordWrap
            font.family: Theme.body
            font.pixelSize: 13
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
            visible: inspector.hasItem && !inspector.isText && !inspector.isTilemap
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

        // Sprite sheet: frames cut out of the picture, and named animations of those frames.
        RowLayout {
            Layout.fillWidth: true
            visible: inspector.isImage
            spacing: 10

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Frame width"
                value: inspector.isImage ? String(inspector.item.frameWidth) : "0"
                maximumLength: 4
                onCommitted: (text) => inspector.set("frameWidth", Number(text))
            }

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Frame height"
                value: inspector.isImage ? String(inspector.item.frameHeight) : "0"
                maximumLength: 4
                onCommitted: (text) => inspector.set("frameHeight", Number(text))
            }
        }

        Text {
            Layout.fillWidth: true
            visible: inspector.isImage
            text: "Set a frame size to cut this picture into frames. 0 is one plain picture."
            color: Theme.muted
            wrapMode: Text.WordWrap
            font.family: Theme.body
            font.pixelSize: 13
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: inspector.isSheet
            spacing: 8

            Text {
                text: "Animations"
                color: Theme.muted
                font.family: Theme.body
                font.pixelSize: 14
            }

            Repeater {
                model: inspector.isSheet ? inspector.item.animations : []

                Rectangle {
                    id: animationRow

                    required property var modelData
                    readonly property bool previewing: inspector.previewAnimation === modelData.name

                    Layout.fillWidth: true
                    implicitHeight: animationColumn.implicitHeight + 16
                    radius: 8
                    color: previewing ? Theme.raised : Theme.ink
                    border.width: 1
                    border.color: previewing ? Theme.signal : Theme.line

                    ColumnLayout {
                        id: animationColumn

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                text: animationRow.modelData.name + (inspector.item.animation === animationRow.modelData.name ? "  (plays first)" : "")
                                color: Theme.paper
                                elide: Text.ElideRight
                                font.family: Theme.body
                                font.pixelSize: 14
                            }

                            MiniButton {
                                text: animationRow.previewing ? "Stop" : "Play"
                                onClicked: inspector.previewAnimationPicked(animationRow.previewing ? "" : animationRow.modelData.name)
                            }

                            MiniButton {
                                text: "Use first"
                                visible: inspector.item.animation !== animationRow.modelData.name
                                onClicked: inspector.set("animation", animationRow.modelData.name)
                            }

                            MiniButton {
                                text: "Remove"
                                onClicked: inspector.scene.removeAnimation(inspector.scene.selectedIndex, animationRow.modelData.name)
                            }
                        }

                        Field {
                            Layout.fillWidth: true
                            label: "Frames"
                            value: animationRow.modelData.frames
                            onCommitted: (text) => inspector.scene.setAnimationFrames(inspector.scene.selectedIndex, animationRow.modelData.name, text.split(",").map((number) => Number(number.trim())))
                        }

                        Field {
                            Layout.fillWidth: true
                            label: "Frames per second"
                            value: String(animationRow.modelData.fps)
                            maximumLength: 2
                            onCommitted: (text) => inspector.scene.setAnimationFps(inspector.scene.selectedIndex, animationRow.modelData.name, Number(text))
                        }
                    }
                }
            }

            Field {
                id: newAnimationName

                Layout.fillWidth: true
                label: "New animation"
                hint: "A name like walk or jump, then press Return."
                onAccepted: {
                    if (text.length > 0 && inspector.scene.addAnimation(inspector.scene.selectedIndex, text)) setText("")
                }
            }
        }

        ToggleRow {
            Layout.fillWidth: true
            visible: inspector.isImage
            text: "Sharp pixels"
            checked: inspector.isImage && inspector.item.pixelated
            onToggledTo: (checked) => inspector.set("pixelated", checked)
        }

        ToggleRow {
            Layout.fillWidth: true
            visible: inspector.isImage
            text: "Flip left to right"
            checked: inspector.isImage && inspector.item.flipX
            onToggledTo: (checked) => inspector.set("flipX", checked)
        }

        // Tilemap: the tools and the palette that paint on the stage.
        ColumnLayout {
            Layout.fillWidth: true
            visible: inspector.isTilemap
            spacing: 8

            ChoiceRow {
                Layout.fillWidth: true
                label: "Tool"
                value: inspector.tilemapTool
                options: [
                    { value: "", label: "Select" },
                    { value: "paint", label: "Paint" },
                    { value: "erase", label: "Erase" },
                    { value: "fill", label: "Fill" }
                ]
                onPicked: (tool) => inspector.tilemapToolPicked(tool)
            }

            TilePalette {
                Layout.fillWidth: true
                scene: inspector.scene
                source: inspector.scene.selectedTilesetUrl
                tileWidth: inspector.isTilemap ? inspector.item.tileWidth : 16
                tileHeight: inspector.isTilemap ? inspector.item.tileHeight : 16
                selectedTile: inspector.paintTile
                solidMode: inspector.solidMode
                solidTiles: inspector.isTilemap ? inspector.scene.solidTiles(inspector.scene.selectedIndex) : []
                onPicked: (tile) => {
                    if (inspector.solidMode) inspector.scene.setTileSolid(inspector.scene.selectedIndex, tile, !inspector.scene.isTileSolid(inspector.scene.selectedIndex, tile))
                    else {
                        inspector.paintTilePicked(tile)
                        if (inspector.tilemapTool === "") inspector.tilemapToolPicked("paint")
                    }
                }
            }

            ToggleRow {
                Layout.fillWidth: true
                text: "Mark tiles solid"
                checked: inspector.solidMode
                onToggledTo: (checked) => inspector.solidModeToggled(checked)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Field {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: "Tile width"
                    value: inspector.isTilemap ? String(inspector.item.tileWidth) : ""
                    maximumLength: 3
                    onCommitted: (text) => inspector.set("tileWidth", Number(text))
                }

                Field {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: "Tile height"
                    value: inspector.isTilemap ? String(inspector.item.tileHeight) : ""
                    maximumLength: 3
                    onCommitted: (text) => inspector.set("tileHeight", Number(text))
                }

                Field {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: "Scale"
                    value: inspector.isTilemap ? String(inspector.item.tileScale) : ""
                    maximumLength: 2
                    onCommitted: (text) => inspector.set("tileScale", Number(text))
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Field {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: "Columns"
                    value: inspector.isTilemap ? String(inspector.item.columns) : ""
                    maximumLength: 3
                    onCommitted: (text) => inspector.set("columns", Number(text))
                }

                Field {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: "Rows"
                    value: inspector.isTilemap ? String(inspector.item.rows) : ""
                    maximumLength: 3
                    onCommitted: (text) => inspector.set("rows", Number(text))
                }
            }
        }

        ToggleRow {
            Layout.fillWidth: true
            visible: inspector.hasItem
            text: "Stays on screen"
            enabled: inspector.physicsMode === ""
            checked: inspector.hasItem && inspector.item.screenFixed
            onToggledTo: (checked) => inspector.set("screenFixed", checked)
        }

        ToggleRow {
            Layout.fillWidth: true
            visible: inspector.isImage && inspector.scene.tilt > 0 && !inspector.item.screenFixed
            text: "Stands upright"
            checked: inspector.isImage && inspector.item.upright
            onToggledTo: (checked) => inspector.set("upright", checked)
        }

        RowLayout {
            Layout.fillWidth: true
            visible: inspector.isImage && inspector.scene.tilt > 0 && inspector.item.upright
            spacing: 10

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Picture width"
                value: inspector.isImage ? String(inspector.item.drawWidth) : "0"
                maximumLength: 5
                onCommitted: (text) => inspector.set("drawWidth", Number(text))
            }

            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: "Picture height"
                value: inspector.isImage ? String(inspector.item.drawHeight) : "0"
                maximumLength: 5
                onCommitted: (text) => inspector.set("drawHeight", Number(text))
            }
        }

        // Templates: one item the game stamps out as many copies of, such as a kind of enemy or a dropped coin.
        ColumnLayout {
            Layout.fillWidth: true
            visible: inspector.hasItem && !inspector.isTilemap && !inspector.isParticles
            spacing: 10

            Field {
                Layout.fillWidth: true
                label: "Copies"
                value: inspector.hasItem ? String(inspector.item.copies ?? 0) : "0"
                maximumLength: 3
                hint: (inspector.item.copies ?? 0) > 0
                      ? "A template. The game keeps " + inspector.item.copies + " hidden copies, and Spawn a copy brings one out."
                      : "0 is one ordinary item. Type a number to make it a template that blocks can spawn copies of."
                onCommitted: (text) => inspector.set("copies", Number(text))
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: "Follows"
                    color: Theme.muted
                    font.family: Theme.body
                    font.pixelSize: 14
                }

                Item { Layout.fillWidth: true }

                BlockPicker {
                    objectName: "followsPicker"
                    preferredWidth: 190
                    placeholder: "Nothing"
                    current: inspector.hasItem ? (inspector.item.follows ?? "") : ""
                    shown: {
                        const target = inspector.hasItem ? (inspector.item.follows ?? "") : ""
                        if (target.length === 0) return ""
                        const found = inspector.scene.itemChoices(-1).find((choice) => choice.id === target)
                        return found ? found.name : "Missing item"
                    }
                    choices: [{ id: "", name: "Nothing" }].concat(inspector.scene.itemChoices(inspector.scene.selectedIndex))
                    emptyText: "Add another item first."
                    onChosen: (id) => inspector.set("follows", id)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: inspector.hasItem && (inspector.item.follows ?? "").length > 0
                spacing: 10

                Field {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: "Offset x"
                    value: inspector.hasItem ? String(inspector.item.followX ?? 0) : "0"
                    maximumLength: 7
                    onCommitted: (text) => inspector.set("followX", Number(text))
                }

                Field {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: "Offset y"
                    value: inspector.hasItem ? String(inspector.item.followY ?? 0) : "0"
                    maximumLength: 7
                    onCommitted: (text) => inspector.set("followY", Number(text))
                }
            }
        }

        // Physics: how the item behaves in a platformer. Words a beginner knows, and good defaults.
        ColumnLayout {
            Layout.fillWidth: true
            visible: inspector.canHavePhysics
            spacing: 10

            ChoiceRow {
                Layout.fillWidth: true
                label: "Physics"
                value: inspector.physicsMode
                options: [
                    { value: "", label: "None" },
                    { value: "solid", label: "Static solid" },
                    { value: "body", label: "Moving body" }
                ]
                onPicked: (mode) => inspector.set("physics", mode)
            }

            Text {
                Layout.fillWidth: true
                visible: inspector.physicsMode === "solid"
                text: "A wall or platform. Moving bodies stop at it."
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 13
            }

            SliderRow {
                Layout.fillWidth: true
                visible: inspector.isBody
                label: "Gravity"
                suffix: "%"
                from: 0
                to: 300
                stepSize: 5
                value: inspector.isBody ? inspector.item.gravityScale : 100
                onCommitted: (value) => inspector.set("gravityScale", value)
            }

            ToggleRow {
                Layout.fillWidth: true
                visible: inspector.isBody
                text: "Platformer controls"
                checked: inspector.isBody && inspector.item.platformer
                onToggledTo: (checked) => inspector.set("platformer", checked)
            }

            ToggleRow {
                Layout.fillWidth: true
                visible: inspector.isBody
                text: "Top-down controls"
                checked: inspector.isBody && inspector.item.topDown
                onToggledTo: (checked) => inspector.set("topDown", checked)
            }

            Text {
                Layout.fillWidth: true
                visible: inspector.isBody && inspector.item.topDown
                text: "Walks in every direction on the stick or D-pad. Walls stop it and gravity is off."
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 13
            }

            Text {
                Layout.fillWidth: true
                visible: inspector.isBody && inspector.item.platformer
                text: "Left and right on the stick or D-pad, jump on Cross."
                color: Theme.muted
                wrapMode: Text.WordWrap
                font.family: Theme.body
                font.pixelSize: 13
            }

            ChoiceRow {
                Layout.fillWidth: true
                visible: inspector.isBody && (inspector.item.platformer || inspector.item.topDown)
                label: "Controlled by"
                value: inspector.isBody ? String(inspector.item.player) : "1"
                options: [
                    { value: "1", label: "Player 1" },
                    { value: "2", label: "Player 2" },
                    { value: "3", label: "Player 3" },
                    { value: "4", label: "Player 4" }
                ]
                onPicked: (value) => inspector.set("player", Number(value))
            }

            SliderRow {
                Layout.fillWidth: true
                visible: inspector.isBody && (inspector.item.platformer || inspector.item.topDown)
                label: "Speed"
                from: 60
                to: 3000
                stepSize: 30
                value: inspector.isBody ? inspector.item.moveSpeed : 600
                onCommitted: (value) => inspector.set("moveSpeed", value)
            }

            SliderRow {
                Layout.fillWidth: true
                visible: inspector.isBody && inspector.item.platformer
                label: "Jump height"
                from: 16
                to: 2000
                stepSize: 8
                value: inspector.isBody ? inspector.item.jumpHeight : 260
                onCommitted: (value) => inspector.set("jumpHeight", value)
            }
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
            visible: inspector.hasItem && !inspector.isImage && !inspector.isTilemap && (!inspector.isParticles || inspector.item.palette === "color")
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
            visible: inspector.hasItem && inspector.physicsMode === ""
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
