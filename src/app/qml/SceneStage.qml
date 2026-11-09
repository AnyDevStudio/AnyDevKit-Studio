import QtQuick
import AnyDevKitStudio

// The 16:9 stage: what is drawn here is what the PS5 runtime draws.
Item {
    id: area_

    required property SceneDocument scene

    readonly property alias stage: stage

    signal imageFileDropped(url file, real centerX, real centerY)

    function takeFocus() { stage.forceActiveFocus() }

    function imageFiles(urls) {
        return Array.from(urls).filter((file) => /^file:.*\.(png|jpe?g|webp)$/i.test(String(file)))
    }

    Timer {
        interval: 2500
        repeat: true
        running: area_.visible
        triggeredOnStart: true
        onTriggered: area_.scene.recheckImages()
    }

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
                required property string itemSource
                required property url itemImageUrl
                required property string itemImageProblem
                required property real itemX
                required property real itemY
                required property real itemWidth
                required property real itemHeight
                required property color itemColor
                required property string itemText
                required property var itemParticles

                readonly property bool selected: area_.scene.selectedIndex === index
                readonly property bool isParticles: itemType === "particles"
                readonly property bool isImage: itemType === "image"

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
                // so they are picked and dragged by this small mark instead of by their area_.
                PixelBurst {
                    anchors.centerIn: parent
                    cell: 4
                    visible: stageItem.isParticles
                    color: stageItem.selected || emitterGrab.containsMouse ? Theme.signal : Qt.rgba(1, 1, 1, 0.55)
                }

                Image {
                    id: picture

                    anchors.fill: parent
                    visible: stageItem.isImage && status !== Image.Error
                    source: stageItem.itemImageUrl
                    fillMode: Image.Stretch
                    asynchronous: true
                    mipmap: true
                }

                ImagePlaceholder {
                    anchors.fill: parent
                    visible: stageItem.isImage && (stageItem.itemImageProblem.length > 0 || picture.status === Image.Error)
                    message: stageItem.itemImageProblem.length > 0 ? "Image file missing" : "Image cannot be read"
                    fileName: stageItem.itemSource.split("/").pop()
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
                    visible: stageItem.selected && (stageItem.itemType === "rectangle" || stageItem.isParticles || stageItem.isImage)

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.SizeFDiagCursor
                        preventStealing: true

                        onPressed: area_.scene.beginChange()
                        onPositionChanged: (mouse) => {
                            if (!pressed) return
                            const point = mapToItem(stage, mouse.x, mouse.y)
                            const keepAspect = stageItem.isImage && !(mouse.modifiers & Qt.ShiftModifier)
                            area_.scene.resizeItem(stageItem.index, point.x / stage.unit - stageItem.itemX, point.y / stage.unit - stageItem.itemY, keepAspect)
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
            text: "Drag a shape, text or image here"
            color: Qt.rgba(1, 1, 1, 0.6)
            font.family: Theme.body
            font.pixelSize: 18
        }

        DropArea {
            id: fileDrop

            anchors.fill: parent
            onEntered: (drag) => { drag.accepted = area_.imageFiles(drag.urls).length > 0 }
            onDropped: (drop) => {
                const files = area_.imageFiles(drop.urls)
                if (files.length === 0) return
                drop.accept(Qt.CopyAction)
                // Several files land in a small cascade, so none hides another.
                files.forEach((file, order) => area_.imageFileDropped(file, (drop.x + order * 28) / stage.unit, (drop.y + order * 28) / stage.unit))
                stage.forceActiveFocus()
            }
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(1, 0.82, 0.25, 0.14)
            border.width: 3
            border.color: Theme.signal
            opacity: fileDrop.containsDrag ? 1 : 0
            visible: opacity > 0
            z: 1500

            Behavior on opacity {
                NumberAnimation { duration: Theme.ms(120); easing.type: Easing.OutCubic }
            }

            Text {
                anchors.centerIn: parent
                text: "Drop to add this image"
                color: Theme.paper
                font.family: Theme.body
                font.weight: Font.DemiBold
                font.pixelSize: 18
            }
        }
    }

    Rectangle {
        id: banner

        // A notice about the last action, and any images the scene cannot show right now.
        readonly property string message: [area_.scene.notice, area_.scene.imageWarning].filter((text) => text.length > 0).join("\n")
        readonly property bool isProblem: area_.scene.noticeIsProblem || area_.scene.imageWarning.length > 0

        onMessageChanged: if (area_.scene.notice.length > 0 && !area_.scene.noticeIsProblem) noticeTimer.restart()

        Timer {
            id: noticeTimer

            interval: 7000
            onTriggered: area_.scene.clearNotice()
        }

        anchors.horizontalCenter: stage.horizontalCenter
        // Below the stage when there is room, so it does not cover the scene.
        y: Math.min(stage.y + stage.height + 12, area_.height - height - 4)
        width: bannerText.width + 36
        height: bannerText.height + 22
        radius: 20
        color: Theme.panel
        border.width: 1
        border.color: isProblem ? Theme.bad : Theme.line
        opacity: message.length > 0 ? 1 : 0
        scale: message.length > 0 ? 1 : 0.96
        visible: opacity > 0
        z: 2100

        Behavior on opacity {
            NumberAnimation { duration: Theme.ms(150); easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            SpringAnimation { spring: 6; damping: 0.5; duration: Theme.ms(220) }
        }

        Text {
            id: bannerText

            anchors.centerIn: parent
            width: Math.min(implicitWidth, stage.width - 84)
            text: banner.message
            color: banner.isProblem ? Theme.bad : Theme.paper
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.body
            font.pixelSize: 14
            lineHeight: 1.2
        }
    }
}
