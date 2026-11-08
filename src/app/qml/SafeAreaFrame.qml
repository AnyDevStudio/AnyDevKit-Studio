import QtQuick

// A television title-safe frame: corner ticks at the 90% safe area of a 16:9 picture.
Item {
    id: frame

    property color tickColor: Theme.line
    property real tick: Math.max(14, width * 0.045)

    readonly property real insetX: width * 0.05
    readonly property real insetY: height * 0.05

    Repeater {
        model: [[0, 0], [1, 0], [0, 1], [1, 1]]

        Item {
            required property var modelData

            x: frame.insetX + modelData[0] * (frame.width - 2 * frame.insetX - width)
            y: frame.insetY + modelData[1] * (frame.height - 2 * frame.insetY - height)
            width: frame.tick
            height: frame.tick

            Rectangle {
                width: parent.width
                height: 2
                y: parent.modelData[1] * (parent.height - height)
                color: frame.tickColor
            }

            Rectangle {
                width: 2
                height: parent.height
                x: parent.modelData[0] * (parent.width - width)
                color: frame.tickColor
            }
        }
    }
}
