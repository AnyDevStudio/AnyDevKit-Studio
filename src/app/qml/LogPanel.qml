import QtQuick
import QtQuick.Controls

Rectangle {
    id: panel

    required property StudioController studio

    color: Theme.panel
    radius: 8
    clip: true

    Text {
        anchors.centerIn: parent
        visible: logView.count === 0
        text: "The build log appears here."
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 15
    }

    ListView {
        id: logView

        anchors.fill: parent
        anchors.margins: 14
        clip: true
        model: panel.studio.log
        boundsBehavior: Flickable.StopAtBounds
        onCountChanged: positionViewAtEnd()

        ScrollBar.vertical: ScrollBar {}

        delegate: Text {
            required property string display

            readonly property bool problem: /(^|\s)(fatal )?error:|^FAILED:|^ninja: build stopped|^CMake Error/.test(display)

            width: ListView.view.width - 14
            text: display
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: problem ? Theme.bad : Theme.paper
            opacity: problem ? 1 : 0.86
            font.family: Theme.mono
            font.pixelSize: 13
            lineHeight: 1.25
        }
    }
}
