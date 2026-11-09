import QtQuick
import AnyDevKitStudio

// The 16:9 stage: what is drawn here is what the PS5 runtime draws.
Item {
    id: area_

    required property SceneDocument scene

    readonly property alias stage: stage

    function takeFocus() { stage.forceActiveFocus() }

    Rectangle {
        anchors.centerIn: stage
        width: stage.width + 2
        height: stage.height + 2
        color: "transparent"
        border.width: 1
        border.color: Theme.line
    }

    Rectangle {
        id: stage

        readonly property real unit: width / 1920

        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height * 16 / 9)
        height: width * 9 / 16
        color: area_.scene.background
        clip: true
        activeFocusOnTab: true

        Keys.onPressed: (event) => {
            const index = area_.scene.selectedIndex
            if (index < 0) return
            const step = (event.modifiers & Qt.ShiftModifier) ? 40 : 8
            if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) area_.scene.removeItem(index)
            else if (event.key === Qt.Key_Left) area_.scene.nudgeItem(index, -step, 0)
            else if (event.key === Qt.Key_Right) area_.scene.nudgeItem(index, step, 0)
            else if (event.key === Qt.Key_Up) area_.scene.nudgeItem(index, 0, -step)
            else if (event.key === Qt.Key_Down) area_.scene.nudgeItem(index, 0, step)
            else return
            event.accepted = true
        }

        TapHandler {
            onTapped: {
                area_.scene.selectedIndex = -1
                stage.forceActiveFocus()
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.width: 2
            border.color: Theme.paper
            visible: stage.activeFocus
            z: 2000
        }

        Repeater {
            model: area_.scene

            Item {
                id: stageItem

                required property int index
                required property string itemType
                required property real itemX
                required property real itemY
                required property real itemWidth
                required property real itemHeight
                required property color itemColor
                required property string itemText
                required property var itemParticles

                readonly property bool selected: area_.scene.selectedIndex === index
                readonly property bool isParticles: itemType === "particles"

                x: itemX * stage.unit
                y: itemY * stage.unit
                width: itemWidth * stage.unit
                height: itemHeight * stage.unit

                Rectangle {
                    anchors.fill: parent
                    visible: stageItem.itemType === "rectangle"
                    color: stageItem.itemColor
                }

                PixelText {
                    anchors.fill: parent
                    visible: stageItem.itemType === "text"
                    text: stageItem.itemText
                    color: stageItem.itemColor
                }

                Loader {
                    active: stageItem.isParticles
                    x: -stageItem.x
                    y: -stageItem.y
                    width: stage.width
                    height: stage.height

                    sourceComponent: ParticlePreview {
                        id: preview

                        settings: stageItem.itemParticles
                        area: Qt.rect(stageItem.itemX, stageItem.itemY, stageItem.itemWidth, stageItem.itemHeight)
                        background: area_.scene.background
                        still: Theme.reducedMotion

                        // Particles only run while the Scene tab is showing and motion is allowed.
                        FrameAnimation {
                            running: !Theme.reducedMotion && area_.visible
                            onTriggered: preview.advance(frameTime)
                        }
                    }
                }

                // Particles have no solid shape to click on, and Snow or Rain cover the whole stage,
                // so they are picked and dragged by this small mark instead of by their area.
                PixelBurst {
                    anchors.centerIn: parent
                    cell: 4
                    visible: stageItem.isParticles
                    color: stageItem.selected || emitterGrab.containsMouse ? Theme.signal : Qt.rgba(1, 1, 1, 0.55)
                }

                MouseArea {
                    id: emitterGrab

                    property real grabX: 0
                    property real grabY: 0

                    anchors.centerIn: parent
                    width: stageItem.isParticles ? Math.min(parent.width, 44) : parent.width
                    height: stageItem.isParticles ? Math.min(parent.height, 44) : parent.height
                    hoverEnabled: stageItem.isParticles
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    preventStealing: true

                    onPressed: (mouse) => {
                        area_.scene.selectedIndex = stageItem.index
                        stage.forceActiveFocus()
                        grabX = mouse.x
                        grabY = mouse.y
                        area_.scene.beginChange()
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        const point = mapToItem(stage, mouse.x, mouse.y)
                        area_.scene.moveItem(stageItem.index, (point.x - grabX) / stage.unit, (point.y - grabY) / stage.unit)
                    }
                    onReleased: area_.scene.endChange()
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    color: "transparent"
                    border.width: 3
                    border.color: Theme.ink
                    visible: stageItem.selected

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.paper
                    }
                }

                Rectangle {
                    width: 14
                    height: 14
                    radius: 3
                    x: parent.width - 7
                    y: parent.height - 7
                    color: Theme.signal
                    border.width: 2
                    border.color: Theme.ink
                    visible: stageItem.selected && (stageItem.itemType === "rectangle" || stageItem.isParticles)

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.SizeFDiagCursor
                        preventStealing: true

                        onPressed: area_.scene.beginChange()
                        onPositionChanged: (mouse) => {
                            if (!pressed) return
                            const point = mapToItem(stage, mouse.x, mouse.y)
                            area_.scene.resizeItem(stageItem.index, point.x / stage.unit - stageItem.itemX, point.y / stage.unit - stageItem.itemY)
                        }
                        onReleased: area_.scene.endChange()
                    }
                }
            }
        }

        SafeAreaFrame {
            anchors.fill: parent
            tickColor: Qt.rgba(1, 1, 1, 0.28)
            z: 1000
        }

        Text {
            anchors.centerIn: parent
            visible: area_.scene.count === 0
            text: "Drag a shape or text here"
            color: Qt.rgba(1, 1, 1, 0.6)
            font.family: Theme.body
            font.pixelSize: 18
        }
    }
}
