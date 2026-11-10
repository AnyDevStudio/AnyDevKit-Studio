import QtQuick
import QtQuick.Templates as T
import AnyDevKitStudio

// The 16:9 stage: what is drawn here is what the PS5 runtime draws.
Item {
    id: area_

    required property SceneDocument scene
    // Painting a tilemap: "paint", "erase", "fill", or empty for ordinary selecting.
    property string tilemapTool: ""
    property int paintTile: 0
    // The animation of the selected sprite to play on the stage; empty plays its first one.
    property string previewAnimation: ""

    readonly property alias stage: stage
    readonly property bool tilemapPainting: tilemapTool.length > 0 && scene.selected.type === "tilemap"

    // The view over the world: which world point is at the stage's top left, and how large it shows.
    function worldX(stageX) { return stage.viewX + stageX / stage.unit }
    function worldY(stageY) { return stage.viewY + stageY / stage.unit }
    function stageX(worldValue) { return (worldValue - stage.viewX) * stage.unit }
    function stageY(worldValue) { return (worldValue - stage.viewY) * stage.unit }
    function zoomBy(factor, anchorX, anchorY) {
        const next = Math.min(stage.maximumZoom, Math.max(stage.minimumZoom, stage.zoom * factor))
        if (next === stage.zoom) return
        const fixedX = worldX(anchorX)
        const fixedY = worldY(anchorY)
        stage.zoom = next
        stage.viewX = fixedX - anchorX / stage.unit
        stage.viewY = fixedY - anchorY / stage.unit
        stage.keepViewOnWorld()
    }
    function fitWorld() {
        stage.zoom = Math.min(1920 / scene.worldWidth, 1080 / scene.worldHeight)
        stage.viewX = 0
        stage.viewY = 0
    }
    function resetView() {
        stage.zoom = 1
        stage.viewX = scene.cameraX
        stage.viewY = scene.cameraY
    }

    signal imageFileDropped(url file, real centerX, real centerY)

    function takeFocus() { stage.forceActiveFocus() }

    function imageFiles(urls) {
        return Array.from(urls).filter((file) => /^file:.*\.(png|jpe?g|webp)$/i.test(String(file)))
    }

    // The view starts where the game starts: on the camera.
    Connections {
        target: area_.scene
        function onLoadedChanged() { area_.resetView() }
    }
    Component.onCompleted: resetView()

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
        objectName: "sceneStage"

        property real zoom: 1
        property real viewX: 0
        property real viewY: 0
        readonly property real minimumZoom: 0.1
        readonly property real maximumZoom: 6
        // Pixels on the screen per world unit.
        readonly property real unit: width / 1920 * zoom
        readonly property real cameraStageX: area_.stageX(area_.scene.cameraX)
        readonly property real cameraStageY: area_.stageY(area_.scene.cameraY)
        readonly property bool worldLargerThanScreen: area_.scene.worldWidth > 1920 || area_.scene.worldHeight > 1080

        // Never pans out of sight of the world, however the view got there.
        function keepViewOnWorld() {
            const spanX = width / unit
            const spanY = height / unit
            viewX = Math.max(-spanX * 0.75, Math.min(viewX, area_.scene.worldWidth - spanX * 0.25))
            viewY = Math.max(-spanY * 0.75, Math.min(viewY, area_.scene.worldHeight - spanY * 0.25))
        }

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: Math.min(parent.width, parent.height * 16 / 9)
        height: width * 9 / 16
        color: Theme.ink
        clip: true
        activeFocusOnTab: true

        // Middle button drag pans; the wheel pans too, and with Ctrl or Cmd it zooms around the pointer.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.MiddleButton
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
            property real startX
            property real startY
            property real viewStartX
            property real viewStartY
            onPressed: (mouse) => { startX = mouse.x; startY = mouse.y; viewStartX = stage.viewX; viewStartY = stage.viewY }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                stage.viewX = viewStartX - (mouse.x - startX) / stage.unit
                stage.viewY = viewStartY - (mouse.y - startY) / stage.unit
                stage.keepViewOnWorld()
            }
        }

        WheelHandler {
            acceptedModifiers: Qt.NoModifier
            onWheel: (event) => {
                stage.viewX -= event.angleDelta.x / stage.unit * 0.5
                stage.viewY -= event.angleDelta.y / stage.unit * 0.5
                stage.keepViewOnWorld()
            }
        }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier | Qt.MetaModifier
            onWheel: (event) => area_.zoomBy(Math.pow(1.0015, event.angleDelta.y), point.position.x, point.position.y)
        }

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

        // The world itself. Where the view goes beyond it the stage shows through in ink.
        Rectangle {
            x: area_.stageX(0)
            y: area_.stageY(0)
            width: area_.scene.worldWidth * stage.unit
            height: area_.scene.worldHeight * stage.unit
            color: area_.scene.background

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: stage.worldLargerThanScreen ? 1 : 0
                border.color: Qt.rgba(1, 1, 1, 0.45)
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
                required property bool itemHidden
                required property bool itemScreenFixed
                required property var itemSprite
                required property var itemTilemap

                readonly property bool selected: area_.scene.selectedIndex === index
                readonly property bool isParticles: itemType === "particles"
                readonly property bool isImage: itemType === "image"
                readonly property bool isTilemap: itemType === "tilemap"
                readonly property bool isPixelImage: isImage && itemSprite.pixelated === true
                // A screen-fixed item is positioned on the camera's frame, not in the world.
                readonly property real worldLeft: itemX + (itemScreenFixed ? area_.scene.cameraX : 0)
                readonly property real worldTop: itemY + (itemScreenFixed ? area_.scene.cameraY : 0)

                x: area_.stageX(worldLeft)
                y: area_.stageY(worldTop)
                z: index // a reorder swaps delegates in place, so paint order comes from the row
                width: itemWidth * stage.unit
                height: itemHeight * stage.unit
                // Hidden while editing only. Being invisible also stops it being clicked.
                visible: !itemHidden

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
                    x: -stage.viewX * stage.unit - stageItem.x
                    y: -stage.viewY * stage.unit - stageItem.y
                    width: 1920 * stage.unit
                    height: 1080 * stage.unit

                    sourceComponent: ParticlePreview {
                        id: preview

                        settings: stageItem.itemParticles
                        area: Qt.rect(stageItem.worldLeft, stageItem.worldTop, stageItem.itemWidth, stageItem.itemHeight)
                        background: area_.scene.background
                        still: Theme.reducedMotion

                        // Particles only run while the Scene tab is showing and motion is allowed.
                        FrameAnimation {
                            running: !Theme.reducedMotion && area_.visible && !stageItem.itemHidden
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

                // Sharp pixels and sprite sheets, drawn the way the game draws them.
                PixelImage {
                    id: pixelPicture

                    property real animationClock: 0

                    readonly property var animationFrames: {
                        const name = area_.previewAnimation
                        if (stageItem.selected && name.length > 0) {
                            const match = (area_.scene.selected.animations || []).find((animation) => animation.name === name)
                            if (match) return match.frames.split(",").map((number) => Number(number.trim()))
                        }
                        return stageItem.itemSprite.frames || []
                    }
                    readonly property real animationFps: {
                        const name = area_.previewAnimation
                        if (stageItem.selected && name.length > 0) {
                            const match = (area_.scene.selected.animations || []).find((animation) => animation.name === name)
                            if (match) return match.fps
                        }
                        return stageItem.itemSprite.fps || 8
                    }
                    readonly property int animationStep: animationFrames.length > 0 ? Math.floor(animationClock * animationFps) % animationFrames.length : 0

                    anchors.fill: parent
                    visible: stageItem.isPixelImage
                    source: stageItem.itemImageUrl
                    frameWidth: stageItem.itemSprite.frameWidth || 0
                    frameHeight: stageItem.itemSprite.frameHeight || 0
                    flipped: stageItem.itemSprite.flipX === true
                    frame: animationFrames.length > 0 ? animationFrames[animationStep] : 0

                    FrameAnimation {
                        running: stageItem.isPixelImage && !Theme.reducedMotion && area_.visible && !stageItem.itemHidden && pixelPicture.animationFrames.length > 1
                        onTriggered: pixelPicture.animationClock += frameTime
                    }
                }

                Image {
                    id: picture

                    anchors.fill: parent
                    visible: stageItem.isImage && !stageItem.isPixelImage && status !== Image.Error
                    source: stageItem.itemImageUrl
                    fillMode: Image.Stretch
                    asynchronous: true
                    mipmap: true
                }

                Loader {
                    x: -stageItem.x
                    y: -stageItem.y
                    width: stage.width
                    height: stage.height
                    active: stageItem.isTilemap

                    sourceComponent: TilemapView {
                        document: area_.scene
                        itemIndex: stageItem.index
                        source: stageItem.itemImageUrl
                        mapX: stageItem.x
                        mapY: stageItem.y
                        unit: stage.unit
                        grid: area_.tilemapPainting && stageItem.selected
                        showSolid: stageItem.selected && area_.tilemapTool.length > 0
                    }
                }

                ImagePlaceholder {
                    anchors.fill: parent
                    visible: (stageItem.isImage || stageItem.isTilemap) && (stageItem.itemImageProblem.length > 0 || picture.status === Image.Error)
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
                    // A tilemap fills the world, so clicks go through it; pick it in the Layers list.
                    enabled: !stageItem.isTilemap

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
                        const offsetX = stageItem.itemScreenFixed ? area_.scene.cameraX : 0
                        const offsetY = stageItem.itemScreenFixed ? area_.scene.cameraY : 0
                        area_.scene.moveItem(stageItem.index, area_.worldX(point.x - grabX) - offsetX, area_.worldY(point.y - grabY) - offsetY)
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
                            area_.scene.resizeItem(stageItem.index, area_.worldX(point.x) - stageItem.worldLeft, area_.worldY(point.y) - stageItem.worldTop, keepAspect)
                        }
                        onReleased: area_.scene.endChange()
                    }
                }
            }
        }

        SafeAreaFrame {
            x: stage.cameraStageX
            y: stage.cameraStageY
            width: 1920 * stage.unit
            height: 1080 * stage.unit
            tickColor: Qt.rgba(1, 1, 1, 0.28)
            z: 1000
        }

        // Where the camera starts: drag its tag to move it, and it shows what the game opens on.
        Rectangle {
            id: cameraFrame

            x: stage.cameraStageX
            y: stage.cameraStageY
            width: 1920 * stage.unit
            height: 1080 * stage.unit
            color: "transparent"
            border.width: 2
            border.color: Theme.signal
            visible: stage.worldLargerThanScreen || stage.zoom !== 1
            z: 1100

            Rectangle {
                id: cameraTag

                width: cameraLabel.implicitWidth + 16
                height: 22
                radius: 4
                x: 6
                y: parent.height - height - 6
                color: Theme.signal

                Text {
                    id: cameraLabel

                    anchors.centerIn: parent
                    text: "Camera"
                    color: Theme.ink
                    font.family: Theme.body
                    font.weight: Font.DemiBold
                    font.pixelSize: 12
                }

                MouseArea {
                    property real grabX
                    property real grabY

                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    preventStealing: true
                    onPressed: (mouse) => {
                        const point = mapToItem(stage, mouse.x, mouse.y)
                        grabX = point.x - stage.cameraStageX
                        grabY = point.y - stage.cameraStageY
                        area_.scene.beginChange()
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        const point = mapToItem(stage, mouse.x, mouse.y)
                        area_.scene.setCameraStart(area_.worldX(point.x - grabX), area_.worldY(point.y - grabY))
                    }
                    onReleased: area_.scene.endChange()
                }
            }
        }

        // Paints tiles with the chosen tool. It sits over every item while a tool is on.
        MouseArea {
            id: tilePainter

            property int lastColumn: -1
            property int lastRow: -1

            function mapItem() { return area_.scene.selected }
            function cellAt(mouseX, mouseY) {
                const map = mapItem()
                const originX = (map.x || 0) + (map.screenFixed ? area_.scene.cameraX : 0)
                const originY = (map.y || 0) + (map.screenFixed ? area_.scene.cameraY : 0)
                const cellWidth = map.tileWidth * map.tileScale
                const cellHeight = map.tileHeight * map.tileScale
                return Qt.point(Math.floor((area_.worldX(mouseX) - originX) / cellWidth), Math.floor((area_.worldY(mouseY) - originY) / cellHeight))
            }
            function tileToPaint() { return area_.tilemapTool === "erase" ? -1 : area_.paintTile }
            // A fast drag moves many cells between events; step through them so the stroke has no holes.
            function paintLineTo(column, row) {
                const stepsX = column - lastColumn
                const stepsY = row - lastRow
                const steps = Math.max(Math.abs(stepsX), Math.abs(stepsY))
                for (let step = 1; step <= steps; ++step) {
                    area_.scene.paintTile(area_.scene.selectedIndex, lastColumn + Math.round(stepsX * step / steps), lastRow + Math.round(stepsY * step / steps), tileToPaint())
                }
                if (steps === 0) area_.scene.paintTile(area_.scene.selectedIndex, column, row, tileToPaint())
                lastColumn = column
                lastRow = row
            }

            anchors.fill: parent
            visible: area_.tilemapPainting
            hoverEnabled: true
            preventStealing: true
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.CrossCursor
            z: 1400

            onPressed: (mouse) => {
                const cell = cellAt(mouse.x, mouse.y)
                if (area_.tilemapTool === "fill") {
                    area_.scene.fillTiles(area_.scene.selectedIndex, cell.x, cell.y, area_.paintTile)
                    return
                }
                area_.scene.beginChange()
                lastColumn = cell.x
                lastRow = cell.y
                area_.scene.paintTile(area_.scene.selectedIndex, cell.x, cell.y, tileToPaint())
            }
            onPositionChanged: (mouse) => {
                const cell = cellAt(mouse.x, mouse.y)
                hoverCell.x = area_.stageX((cell.x * mapItem().tileWidth * mapItem().tileScale) + (mapItem().x || 0) + (mapItem().screenFixed ? area_.scene.cameraX : 0))
                hoverCell.y = area_.stageY((cell.y * mapItem().tileHeight * mapItem().tileScale) + (mapItem().y || 0) + (mapItem().screenFixed ? area_.scene.cameraY : 0))
                hoverCell.width = mapItem().tileWidth * mapItem().tileScale * stage.unit
                hoverCell.height = mapItem().tileHeight * mapItem().tileScale * stage.unit
                if (pressed && area_.tilemapTool !== "fill") paintLineTo(cell.x, cell.y)
            }
            onReleased: if (area_.tilemapTool !== "fill") area_.scene.endChange()

            Rectangle {
                id: hoverCell

                color: Qt.rgba(1, 0.82, 0.25, 0.28)
                border.width: 2
                border.color: Theme.signal
                visible: tilePainter.containsMouse
            }
        }

        // Pan and zoom, on the stage's own corner so it never covers the work.
        ZoomButtons {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            z: 1700
            buttons: [
                { label: "\u2212", hint: "Zoom out", act: () => area_.zoomBy(1 / 1.25, stage.width / 2, stage.height / 2) },
                { label: Math.round(stage.zoom * 100) + "%", hint: "Back to the camera at 100%", act: () => area_.resetView() },
                { label: "+", hint: "Zoom in", act: () => area_.zoomBy(1.25, stage.width / 2, stage.height / 2) },
                { label: "Fit", hint: "Show the whole world", act: () => area_.fitWorld() }
            ]
        }

        Text {
            anchors.centerIn: parent
            visible: area_.scene.count === 0
            text: "Drag a shape, text, image or tilemap here"
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
                files.forEach((file, order) => area_.imageFileDropped(file, area_.worldX(drop.x + order * 28), area_.worldY(drop.y + order * 28)))
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
