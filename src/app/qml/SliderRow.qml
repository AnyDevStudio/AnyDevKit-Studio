import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A labelled slider that saves when the thumb is released or a key is pressed, not on every move.
ColumnLayout {
    id: row

    property string label
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property string suffix

    signal committed(real value)

    // Back to the saved value afterwards, so a change the scene refused does not stay on the slider.
    function commit() {
        if (Math.round(slider.value) !== Math.round(value)) committed(Math.round(slider.value))
        slider.value = Qt.binding(() => row.value)
    }

    spacing: 4

    RowLayout {
        Layout.fillWidth: true

        Text {
            Layout.fillWidth: true
            text: row.label
            color: Theme.muted
            font.family: Theme.body
            font.pixelSize: 14
        }

        Text {
            text: Math.round(slider.value) + row.suffix
            color: Theme.paper
            font.family: Theme.mono
            font.pixelSize: 13
        }
    }

    T.Slider {
        id: slider

        Layout.fillWidth: true
        implicitHeight: 28
        from: row.from
        to: row.to
        stepSize: row.stepSize
        value: row.value
        snapMode: T.Slider.SnapAlways
        live: true
        Accessible.name: row.label

        onMoved: if (!pressed) row.commit()
        onPressedChanged: if (!pressed) row.commit()

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.raised

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.signal
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 18
            height: 18
            radius: 9
            color: slider.pressed ? Theme.paper : Theme.signal
            border.width: slider.visualFocus ? 3 : 1
            border.color: slider.visualFocus ? Theme.paper : Theme.ink
        }
    }
}
