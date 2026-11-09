import QtQuick

// A little pixel spark: the mark for particles on the palette and on the stage.
Item {
    id: burst

    property color color: Theme.signal
    property real cell: 6

    implicitWidth: cell * 5
    implicitHeight: cell * 5

    Repeater {
        model: [[2, 0], [0, 2], [4, 2], [2, 4], [1, 1], [3, 1], [1, 3], [3, 3], [2, 2]]

        Rectangle {
            required property var modelData
            x: modelData[0] * burst.cell
            y: modelData[1] * burst.cell
            width: burst.cell
            height: burst.cell
            color: burst.color
        }
    }
}
