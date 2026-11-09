import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import AnyDevKitStudio

Item {
    id: view

    required property SceneDocument scene
    required property StudioController studio

    // Painting a tilemap: which tool is on and which tile from the palette it lays down. Editor only.
    property string tilemapTool: ""
    property int paintTileIndex: 0
    property bool solidMode: false
    // Which animation of the selected sprite the stage plays; empty means its starting animation.
    property string previewAnimation: ""

    Connections {
        target: view.scene
        function onSelectionChanged() { view.previewAnimation = "" }
    }

    // The stage is limited by its width at most window sizes, so the height left over below it
    // goes to the layers. Room stays for the notice banner below the stage.
    readonly property real layerPanelHeight: {
        const stageHeight = Math.max(0, middle.width - 56) * 9 / 16
        return Math.round(Math.min(300, Math.max(168, middle.height - stageHeight - 76 - 64)))
    }

    // An image first needs a file, so it goes through the picker instead of appearing straight away.
    function place(kind, centerX, centerY) {
        if (kind === "image") imagePicker.chooseNew(centerX, centerY)
        else if (kind === "tilemap") imagePicker.chooseTileset()
        else scene.addItem(kind, centerX, centerY)
    }

    ImagePicker {
        id: imagePicker
        studio: view.studio
    }

    component Tile: Rectangle {
        id: tile

        required property string kind
        required property string label

        Layout.fillWidth: true
        implicitHeight: 104
        radius: 10
        color: hover.containsMouse ? Theme.raised : Theme.ink
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: "Add " + label
        border.width: activeFocus ? 2 : 1
        border.color: activeFocus ? Theme.paper : Theme.line
        Keys.onReturnPressed: view.place(kind, 960, 540)
        Keys.onSpacePressed: view.place(kind, 960, 540)

        Rectangle {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -12
            width: 58
            height: 34
            color: Theme.signal
            visible: tile.kind === "rectangle"
        }

        PixelText {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -12
            width: 68
            height: 28
            text: "Abc"
            color: Theme.signal
            visible: tile.kind === "text"
        }

        Grid {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -12
            visible: tile.kind === "tilemap"
            columns: 3
            spacing: 2

            Repeater {
                model: 6

                Rectangle {
                    required property int index

                    width: 16
                    height: 16
                    color: Theme.signal
                    opacity: index === 4 ? 0.3 : 1
                }
            }
        }

        PixelBurst {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -12
            cell: 8
            visible: tile.kind === "particles"
        }

        ImageGlyph {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -12
            width: 58
            height: 34
            visible: tile.kind === "image"
            cutout: tile.color
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: tile.label
            color: Theme.paper
            font.family: Theme.body
            font.pixelSize: 14
        }

        MouseArea {
            id: hover

            property point start

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            preventStealing: true

            function viewPoint(mouse) { return mapToItem(view, mouse.x, mouse.y) }

            onPressed: (mouse) => {
                start = Qt.point(mouse.x, mouse.y)
                ghost.kind = tile.kind
                const point = viewPoint(mouse)
                ghost.x = point.x - ghost.width / 2
                ghost.y = point.y - ghost.height / 2
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                if (Math.abs(mouse.x - start.x) + Math.abs(mouse.y - start.y) > 6) ghost.visible = true
                const point = viewPoint(mouse)
                ghost.x = point.x - ghost.width / 2
                ghost.y = point.y - ghost.height / 2
            }
            onReleased: (mouse) => {
                const dragged = ghost.visible
                ghost.visible = false
                if (!dragged) {
                    view.place(tile.kind, 960, 540)
                    return
                }
                const stage = stageArea.stage
                const point = mapToItem(stage, mouse.x, mouse.y)
                if (point.x >= 0 && point.y >= 0 && point.x <= stage.width && point.y <= stage.height) {
                    view.place(tile.kind, stageArea.worldX(point.x), stageArea.worldY(point.y))
                    stageArea.takeFocus()
                }
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        onActivated: view.scene.undo()
    }

    Shortcut {
        sequences: ["Ctrl+D"]
        enabled: view.visible && view.scene.selectedIndex >= 0 && !(view.Window.activeFocusItem instanceof TextInput)
        onActivated: view.scene.duplicateItem(view.scene.selectedIndex)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Text {
                    text: "Drag onto the stage"
                    color: Theme.paper
                    font.family: Theme.body
                    font.weight: Font.DemiBold
                    font.pixelSize: 15
                }

                Tile { kind: "rectangle"; label: "Shape" }
                Tile { kind: "text"; label: "Text" }
                Tile { kind: "particles"; label: "Particles" }
                Tile { kind: "image"; label: "Image" }
                Tile { kind: "tilemap"; label: "Tilemap" }

                Item { Layout.fillHeight: true }

                PillButton {
                    Layout.fillWidth: true
                    text: "Undo"
                    enabled: view.scene.canUndo
                    onClicked: view.scene.undo()
                }
            }
        }

        ColumnLayout {
            id: middle

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SceneStrip {
                Layout.fillWidth: true
                Layout.topMargin: 12
                scene: view.scene
            }

            SceneStage {
                id: stageArea

                scene: view.scene
                tilemapTool: view.tilemapTool
                paintTile: view.paintTileIndex
                previewAnimation: view.previewAnimation
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                Layout.topMargin: 8
                Layout.bottomMargin: 4
                onImageFileDropped: (file, centerX, centerY) => view.studio.addSceneImage(file, centerX, centerY)
            }

            LayerPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: view.layerPanelHeight
                scene: view.scene
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 280
            color: Theme.panel

            SceneInspector {
                anchors.fill: parent
                anchors.margins: 18
                scene: view.scene
                tilemapTool: view.tilemapTool
                paintTile: view.paintTileIndex
                solidMode: view.solidMode
                previewAnimation: view.previewAnimation
                onReplaceImageRequested: imagePicker.chooseReplacement(view.scene.selectedIndex)
                onTilemapToolPicked: (tool) => view.tilemapTool = tool
                onPaintTilePicked: (tile) => view.paintTileIndex = tile
                onSolidModeToggled: (solid) => view.solidMode = solid
                onPreviewAnimationPicked: (name) => view.previewAnimation = name
            }
        }
    }

    Rectangle {
        id: ghost

        property string kind: "rectangle"

        width: 110
        height: 64
        visible: false
        color: kind === "rectangle" ? Theme.signal : "transparent"
        opacity: 0.86
        z: 3000

        PixelText {
            anchors.fill: parent
            anchors.margins: 8
            visible: ghost.kind === "text"
            text: "Text"
            color: Theme.signal
        }

        PixelBurst {
            anchors.centerIn: parent
            cell: 8
            visible: ghost.kind === "particles"
        }

        ImageGlyph {
            anchors.fill: parent
            visible: ghost.kind === "image" || ghost.kind === "tilemap"
        }
    }
}
