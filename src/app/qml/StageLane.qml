import QtQuick
import QtQuick.Layouts

RowLayout {
    id: lane

    required property var stages

    spacing: 6

    Repeater {
        model: lane.stages

        ColumnLayout {
            id: stage

            required property var modelData
            readonly property string state_: modelData.state
            readonly property color tone: state_ === "failed" ? Theme.bad : state_ === "done" ? Theme.good : state_ === "running" ? Theme.signal : Theme.raised

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 12
                radius: 3
                color: stage.state_ === "running" ? Theme.raised : stage.tone
                clip: true

                Behavior on color {
                    ColorAnimation { duration: Theme.ms(180) }
                }

                // Moving tape stripes while this stage is working.
                Row {
                    id: stripes
                    visible: stage.state_ === "running"
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
                        model: Math.ceil(stage.width / 28) + 2

                        Rectangle {
                            width: 14
                            height: 24
                            y: -6
                            rotation: 30
                            color: Theme.signal
                        }
                    }
                }
            }

            Text {
                text: stage.modelData.title
                color: stage.state_ === "waiting" ? Theme.muted : Theme.paper
                font.family: Theme.body
                font.weight: stage.state_ === "running" ? Font.DemiBold : Font.Normal
                font.pixelSize: 16
            }
        }
    }
}
