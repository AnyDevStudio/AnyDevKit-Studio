import QtQuick
import QtQuick.Layouts

// One line of the setup checklist: a badge, a title, what is happening now, and live progress.
RowLayout {
    id: row

    required property int number
    required property string title
    required property string state_
    required property string detail
    required property real progress

    spacing: 14

    Rectangle {
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 30
        Layout.preferredHeight: 30
        radius: width / 2
        color: row.state_ === "done" ? Theme.good : row.state_ === "failed" ? Theme.bad : "transparent"
        border.width: row.state_ === "waiting" ? 1 : 2
        border.color: row.state_ === "waiting" ? Theme.line : row.state_ === "running" ? Theme.signal : "transparent"

        Behavior on color {
            ColorAnimation { duration: Theme.ms(160) }
        }

        // The step number until it finishes, a mark afterwards.
        Text {
            anchors.centerIn: parent
            visible: row.state_ === "waiting" || row.state_ === "running"
            text: row.number
            color: row.state_ === "running" ? Theme.signal : Theme.muted
            font.family: Theme.mono
            font.pixelSize: 14
        }

        Item {
            anchors.centerIn: parent
            width: 14
            height: 10
            visible: row.state_ === "done"

            Rectangle {
                width: 7
                height: 2.5
                radius: 1
                x: 0
                y: 5
                rotation: 45
                color: Theme.ink
            }

            Rectangle {
                width: 12
                height: 2.5
                radius: 1
                x: 3.5
                y: 3
                rotation: -48
                color: Theme.ink
            }
        }

        Item {
            anchors.centerIn: parent
            width: 12
            height: 12
            visible: row.state_ === "failed"

            Repeater {
                model: [45, -45]

                Rectangle {
                    required property int modelData
                    anchors.centerIn: parent
                    width: 14
                    height: 2.5
                    radius: 1
                    rotation: modelData
                    color: Theme.ink
                }
            }
        }

        // A soft ring breathing around the step that is working.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.signal
            opacity: 0
            visible: row.state_ === "running" && !Theme.reducedMotion

            SequentialAnimation on opacity {
                running: row.state_ === "running" && !Theme.reducedMotion
                loops: Animation.Infinite

                NumberAnimation { to: 0.5; duration: 620; easing.type: Easing.OutSine }
                NumberAnimation { to: 0; duration: 620; easing.type: Easing.InSine }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 3

        Text {
            Layout.fillWidth: true
            text: row.title
            color: row.state_ === "waiting" ? Theme.muted : Theme.paper
            font.family: Theme.body
            font.weight: row.state_ === "running" ? Font.DemiBold : Font.Normal
            font.pixelSize: 17
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: row.detail
            color: row.state_ === "failed" ? Theme.bad : Theme.muted
            font.family: row.state_ === "failed" ? Theme.body : Theme.mono
            font.pixelSize: row.state_ === "failed" ? 14 : 13
            wrapMode: Text.WordWrap
            maximumLineCount: row.state_ === "failed" ? 6 : 2
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }

        // The same moving tape as a running build, solid once the amount is known.
        Rectangle {
            id: bar

            Layout.fillWidth: true
            Layout.topMargin: 3
            visible: row.state_ === "running"
            implicitHeight: 5
            radius: 2.5
            color: Theme.raised
            clip: true

            Rectangle {
                height: parent.height
                radius: parent.radius
                color: Theme.signal
                visible: row.progress >= 0
                width: parent.width * Math.max(0, Math.min(1, row.progress))

                Behavior on width {
                    NumberAnimation { duration: Theme.ms(220); easing.type: Easing.OutCubic }
                }
            }

            Row {
                id: stripes

                visible: row.progress < 0
                height: parent.height
                spacing: 14

                NumberAnimation on x {
                    running: stripes.visible && !Theme.reducedMotion
                    from: -28
                    to: 0
                    duration: 520
                    loops: Animation.Infinite
                }

                Repeater {
                    model: Math.ceil(bar.width / 28) + 2

                    Rectangle {
                        width: 14
                        height: 14
                        y: -4
                        rotation: 30
                        color: Theme.signal
                    }
                }
            }
        }
    }
}
