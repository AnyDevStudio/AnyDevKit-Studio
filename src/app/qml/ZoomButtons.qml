import QtQuick
import QtQuick.Templates as T
import AnyDevKitStudio

// The small row of zoom buttons in the corner of a canvas. Each entry is { label, hint, act }.
Row {
    id: row

    required property var buttons

    spacing: 4

    Repeater {
        model: row.buttons

        T.Button {
            id: viewButton

            required property var modelData

            implicitWidth: Math.max(26, viewLabel.implicitWidth + 12)
            implicitHeight: 24
            focusPolicy: Qt.TabFocus
            Accessible.name: modelData.hint
            onClicked: modelData.act()

            contentItem: Text {
                id: viewLabel

                text: viewButton.modelData.label
                color: Theme.paper
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.body
                font.pixelSize: 13
            }

            background: Rectangle {
                radius: 12
                color: viewButton.hovered ? Theme.raised : Theme.panel
                border.width: viewButton.visualFocus ? 2 : 1
                border.color: viewButton.visualFocus ? Theme.paper : Theme.line
                opacity: 0.92
            }
        }
    }
}
