import QtQuick

// Stands in for an image whose file cannot be shown, so the scene never silently loses it.
Item {
    id: placeholder

    required property string message
    required property string fileName

    Rectangle {
        anchors.fill: parent
        color: Theme.ink
        border.width: 2
        border.color: Theme.bad
    }

    Canvas {
        id: hatching

        anchors.fill: parent
        anchors.margins: 2
        opacity: 0.32

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = Theme.bad
            context.lineWidth = 2
            const gap = 18
            for (let start = -height; start < width; start += gap) {
                context.beginPath()
                context.moveTo(start, height)
                context.lineTo(start + height, 0)
                context.stroke()
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 16
        visible: parent.height > 34 && parent.width > 72
        spacing: 2

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: placeholder.message
            color: Theme.paper
            elide: Text.ElideRight
            font.family: Theme.body
            font.weight: Font.DemiBold
            font.pixelSize: 13
        }

        Text {
            width: parent.width
            visible: placeholder.height > 54
            horizontalAlignment: Text.AlignHCenter
            text: placeholder.fileName
            color: Theme.muted
            elide: Text.ElideMiddle
            font.family: Theme.mono
            font.pixelSize: 11
        }
    }

    Text {
        anchors.centerIn: parent
        visible: parent.height <= 34 || parent.width <= 72
        text: "!"
        color: Theme.bad
        font.family: Theme.display
        font.pixelSize: Math.max(10, Math.min(parent.height, parent.width) * 0.7)
    }
}
