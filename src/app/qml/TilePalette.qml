import QtQuick
import AnyDevKitStudio

// The tileset as a grid of tiles. A click picks the tile to paint with, or, in solid mode, marks it solid.
Item {
    id: palette

    required property SceneDocument scene
    required property url source
    required property int tileWidth
    required property int tileHeight
    property int selectedTile: 0
    property bool solidMode: false
    property var solidTiles: []

    signal picked(int tile)

    readonly property int columns: tileset.sourceWidth > 0 && tileWidth > 0 ? Math.floor(tileset.sourceWidth / tileWidth) : 0
    readonly property int rows: tileset.sourceHeight > 0 && tileHeight > 0 ? Math.floor(tileset.sourceHeight / tileHeight) : 0
    readonly property real cell: columns > 0 ? Math.max(14, Math.floor(width / columns)) : 16

    implicitHeight: Math.min(cell * rows, 260) + 26
    activeFocusOnTab: true

    Keys.onPressed: (event) => {
        if (columns === 0) return
        let tile = selectedTile
        if (event.key === Qt.Key_Left) tile -= 1
        else if (event.key === Qt.Key_Right) tile += 1
        else if (event.key === Qt.Key_Up) tile -= columns
        else if (event.key === Qt.Key_Down) tile += columns
        else return
        event.accepted = true
        if (tile >= 0 && tile < columns * rows) picked(tile)
    }

    Rectangle {
        anchors.fill: parent
        anchors.bottomMargin: 26
        anchors.margins: -2
        color: "transparent"
        border.width: 2
        border.color: Theme.paper
        radius: 3
        visible: palette.activeFocus
    }

    Flickable {
        id: scroller

        anchors.fill: parent
        anchors.bottomMargin: 26
        contentWidth: width
        contentHeight: palette.cell * palette.rows
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        PixelImage {
            id: tileset

            width: palette.cell * palette.columns
            height: palette.cell * palette.rows
            source: palette.source
        }

        // Faint cell lines, so single tiles read against the picture.
        Repeater {
            model: palette.rows + 1
            Rectangle {
                required property int index
                x: 0
                y: index * palette.cell
                width: tileset.width
                height: 1
                color: Qt.rgba(1, 1, 1, 0.14)
            }
        }
        Repeater {
            model: palette.columns + 1
            Rectangle {
                required property int index
                x: index * palette.cell
                y: 0
                width: 1
                height: tileset.height
                color: Qt.rgba(1, 1, 1, 0.14)
            }
        }

        // Solid tiles carry a tint and a corner tick.
        Repeater {
            model: palette.solidTiles

            Rectangle {
                required property var modelData

                x: (modelData % palette.columns) * palette.cell
                y: Math.floor(modelData / palette.columns) * palette.cell
                width: palette.cell
                height: palette.cell
                color: Qt.rgba(1, 0.82, 0.25, 0.32)

                Rectangle {
                    x: parent.width - 8
                    width: 8
                    height: 8
                    color: Theme.signal
                }
            }
        }

        Rectangle {
            x: (palette.selectedTile % Math.max(palette.columns, 1)) * palette.cell - 2
            y: Math.floor(palette.selectedTile / Math.max(palette.columns, 1)) * palette.cell - 2
            width: palette.cell + 4
            height: palette.cell + 4
            color: "transparent"
            border.width: 2
            border.color: Theme.signal
            visible: palette.columns > 0 && !palette.solidMode
        }

        MouseArea {
            width: tileset.width
            height: tileset.height
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                const column = Math.floor(mouse.x / palette.cell)
                const row = Math.floor(mouse.y / palette.cell)
                if (column >= 0 && column < palette.columns && row >= 0 && row < palette.rows) palette.picked(row * palette.columns + column)
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        text: palette.solidMode ? "Click tiles to mark them solid" : "Tile " + palette.selectedTile
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 13
    }
}
