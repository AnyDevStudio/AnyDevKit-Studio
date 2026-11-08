import QtQuick
import QtQuick.Controls

ListView {
    id: list

    required property StudioController studio

    function openCurrent() {
        const entry = model[currentIndex]
        if (entry && !entry.isDirectory) studio.openFile(entry.path)
    }

    model: studio.files
    clip: true
    activeFocusOnTab: true
    keyNavigationEnabled: true
    boundsBehavior: Flickable.StopAtBounds
    Keys.onReturnPressed: openCurrent()
    Keys.onEnterPressed: openCurrent()

    ScrollBar.vertical: ScrollBar {}

    delegate: Rectangle {
        id: row

        required property var modelData
        required property int index

        width: ListView.view.width
        height: 30
        radius: 4
        color: hover.hovered ? Theme.raised : "transparent"
        border.width: list.activeFocus && ListView.isCurrentItem ? 2 : 0
        border.color: Theme.paper

        HoverHandler { id: hover }

        TapHandler {
            onTapped: {
                list.currentIndex = row.index
                list.openCurrent()
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 10 + row.modelData.depth * 16
            anchors.rightMargin: 10
            text: row.modelData.name
            elide: Text.ElideMiddle
            color: row.modelData.isDirectory ? Theme.muted : Theme.paper
            font.family: Theme.body
            font.weight: row.modelData.isDirectory ? Font.Medium : Font.Normal
            font.pixelSize: 14
        }
    }
}
