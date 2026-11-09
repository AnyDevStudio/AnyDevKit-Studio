import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ComboBox {
    id: picker

    required property StudioController studio

    function syncSelection() { currentIndex = indexOfValue(studio.selectedEditor) }

    model: studio.editors
    textRole: "name"
    valueRole: "id"
    implicitHeight: 38
    enabled: count > 1

    Component.onCompleted: syncSelection()
    onActivated: studio.selectedEditor = currentValue

    Connections {
        target: picker.studio
        function onEditorChanged() { picker.syncSelection() }
    }

    indicator: Text {
        x: picker.width - width - 14
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.muted
        font.pixelSize: 14
    }

    contentItem: Text {
        leftPadding: 16
        rightPadding: 30
        verticalAlignment: Text.AlignVCenter
        text: picker.displayText
        elide: Text.ElideRight
        color: Theme.paper
        font.family: Theme.body
        font.weight: Font.Medium
        font.pixelSize: 14
    }

    background: Rectangle {
        radius: height / 2
        color: picker.hovered ? Theme.raised : "transparent"
        border.width: 1
        border.color: Theme.line

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.paper
            visible: picker.visualFocus
        }
    }

    delegate: Basic.ItemDelegate {
        id: option

        required property var modelData
        required property int index

        width: picker.width - 12
        height: 34
        highlighted: picker.highlightedIndex === index

        contentItem: Text {
            text: option.modelData.name
            verticalAlignment: Text.AlignVCenter
            color: option.modelData.id === picker.studio.selectedEditor ? Theme.signal : Theme.paper
            font.family: Theme.body
            font.pixelSize: 14
        }

        background: Rectangle {
            radius: 6
            color: option.highlighted ? Theme.raised : "transparent"
        }
    }

    popup: Basic.Popup {
        y: -height - 8
        width: picker.width
        height: Math.min(contentItem.implicitHeight + 12, 280)
        padding: 6

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: picker.popup.visible ? picker.delegateModel : null
            currentIndex: picker.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }

        background: Rectangle {
            radius: 10
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
        }
    }
}
