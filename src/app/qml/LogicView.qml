import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Shapes
import AnyDevKitStudio

// The Logic tab: a palette of blocks on the left and a tidy, ordered canvas on the right. Where the
// blocks go is worked out by LogicDocument, so this file only draws and carries them.
Item {
    id: view

    required property StudioController studio

    readonly property LogicDocument doc: studio.logic

    property real pointerX: 0
    property real pointerY: 0
    property real gripX: 0
    property real gripY: 0
    property bool carrying: false
    property bool carryingNew: false
    property bool overPalette: false
    property var carriedPalette: ({})
    property real carryX: 0
    property real carryY: 0
    property real carryOriginX: 0
    property real carryOriginY: 0
    property bool ready: false
    property real zoom: 1
    readonly property real minimumZoom: 0.3
    readonly property real maximumZoom: 1.5

    Timer {
        // Blocks loaded with the project appear at once; only ones added later animate in.
        interval: 300
        running: true
        onTriggered: view.ready = true
    }

    function commitPendingEdits() {
        const focused = Window.activeFocusItem
        if (focused && focused.commitEdit) focused.commitEdit()
    }

    function blockItem(id) {
        for (let index = 0; index < blocks.count; ++index) {
            const candidate = blocks.itemAt(index)
            if (candidate && candidate.blockId === id) return candidate
        }
        return null
    }

    function focusBlock(id, reason) {
        const target = blockItem(id)
        if (!target) return
        target.forceActiveFocus(reason === undefined ? Qt.TabFocusReason : reason)
        ensureVisible(target)
    }

    function scrollCanvasTo(contentX, contentY) {
        canvas.contentX = Math.max(0, Math.min(canvas.contentWidth - canvas.width, contentX))
        canvas.contentY = Math.max(0, Math.min(canvas.contentHeight - canvas.height, contentY))
    }

    // Zooms around a point on the canvas. A new zoom can change how many columns fit and move every block,
    // so what stays under the point is the block that was there, not the empty spot it left.
    function zoomAt(factor, anchorX, anchorY) {
        let next = Math.min(maximumZoom, Math.max(minimumZoom, zoom * factor))
        if (Math.abs(next - 1) < 0.02) next = 1
        if (next === zoom) return
        const boardX = (anchorX + canvas.contentX) / zoom
        const boardY = (anchorY + canvas.contentY) / zoom
        const under = board.childAt(boardX, boardY)
        const held = under && under.blockId !== undefined ? under : null
        const offsetX = held ? boardX - held.blockX : 0
        const offsetY = held ? boardY - held.blockY : 0
        zoom = next
        scrollCanvasTo((held ? held.blockX + offsetX : boardX) * zoom - anchorX, (held ? held.blockY + offsetY : boardY) * zoom - anchorY)
    }

    function ensureVisible(target) {
        const top = (target.blockY - 24) * zoom
        const bottom = (target.blockY + target.blockHeight + 40) * zoom
        if (top < canvas.contentY) canvas.contentY = Math.max(0, top)
        else if (bottom > canvas.contentY + canvas.height) canvas.contentY = Math.max(0, Math.min(bottom - canvas.height, canvas.contentHeight - canvas.height))
    }

    function showFirstProblem() {
        if (doc.firstProblemId) focusBlock(doc.firstProblemId)
    }

    function focusCanvas() { canvasFocus.forceActiveFocus(Qt.OtherFocusReason) }

    function beginCarry(id, gripAtX, gripAtY) {
        for (let index = 0; index < blocks.count; ++index) {
            const candidate = blocks.itemAt(index)
            if (!candidate) continue
            candidate.originX = candidate.x
            candidate.originY = candidate.y
            if (candidate.blockId === id) {
                carryOriginX = candidate.x
                carryOriginY = candidate.y
            }
        }
        gripX = gripAtX
        gripY = gripAtY
        carryingNew = false
        carrying = true
        doc.startMoving(id)
    }

    function beginPaletteCarry(type, title, category, isEvent, holdsActions) {
        carriedPalette = ({ title: title, category: category, isEvent: isEvent, holdsActions: holdsActions })
        gripX = 44
        gripY = 28
        carryingNew = true
        carrying = true
        doc.startAdding(type)
    }

    function updateCarry(item, x, y) {
        const point = item.mapToItem(view, x, y)
        pointerX = point.x
        pointerY = point.y
        pushCarry()
    }

    function pushCarry() {
        if (!carrying) return
        const boardPoint = view.mapToItem(board, pointerX, pointerY)
        carryX = boardPoint.x - gripX
        carryY = boardPoint.y - gripY
        overPalette = pointerX < paletteFrame.width
        doc.dragTo(carryX, carryY)
    }

    function finishCarry() {
        if (!carrying) return
        carrying = false
        if (overPalette) doc.dropOnPalette()
        else {
            const placed = doc.drop()
            if (placed) focusBlock(placed, Qt.MouseFocusReason)
        }
        overPalette = false
    }

    function cancelCarry() {
        if (!carrying) return
        carrying = false
        overPalette = false
        doc.cancelDrag()
    }

    Item {
        id: canvasFocus
        focus: true
    }

    Timer {
        // While carrying a block near the top or bottom edge, scroll the canvas towards it.
        interval: 16
        repeat: true
        running: view.carrying
        onTriggered: {
            const point = view.mapToItem(canvas, view.pointerX, view.pointerY)
            const max = Math.max(0, canvas.contentHeight - canvas.height)
            let step = 0
            if (point.y < 56) step = -12
            else if (point.y > canvas.height - 56) step = 12
            if (step === 0 || point.x < 0 || point.x > canvas.width) return
            canvas.contentY = Math.max(0, Math.min(max, canvas.contentY + step))
            view.pushCarry()
        }
    }

    component PaletteTile: T.Control {
        id: tile

        required property var modelData
        objectName: "paletteTile_" + modelData.type
        readonly property bool wrapper: modelData.holdsActions && !modelData.isEvent

        implicitWidth: 216
        implicitHeight: wrapper ? 78 : 56
        focusPolicy: Qt.TabFocus
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: "Add " + modelData.title

        function add() {
            const added = view.doc.addFromPalette(modelData.type)
            if (added) view.focusBlock(added, Qt.TabFocusReason)
        }

        Keys.onReturnPressed: add()
        Keys.onEnterPressed: add()
        Keys.onSpacePressed: add()

        BlockOutline {
            blockWidth: tile.width
            headerHeight: tile.wrapper ? 42 : 42
            cavityHeight: tile.wrapper ? 14 : 0
            footerHeight: tile.wrapper ? 12 : 0
            indent: 18
            socket: !tile.modelData.isEvent
            fill: tile.modelData.isEvent ? Theme.signal : Theme.raised
            stroke: tile.visualFocus ? Theme.paper : hover.containsMouse ? Theme.muted : tile.modelData.isEvent ? Theme.signal : Theme.line
            strokeWidth: tile.visualFocus ? 2.5 : 1.5
        }

        RowLayout {
            x: 14
            y: 0
            width: tile.width - 28
            height: 42
            spacing: 10

            BlockGlyph {
                kind: tile.modelData.category
                tint: tile.modelData.isEvent ? Theme.ink : Theme.signal
            }

            Text {
                Layout.fillWidth: true
                text: tile.modelData.title
                color: tile.modelData.isEvent ? Theme.ink : Theme.paper
                elide: Text.ElideRight
                font.family: Theme.body
                font.weight: Font.DemiBold
                font.pixelSize: 15
            }
        }

        MouseArea {
            id: hover

            property point pressAt
            property bool lifting: false

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            onPressed: (event) => {
                pressAt = Qt.point(event.x, event.y)
                lifting = false
            }
            onPositionChanged: (event) => {
                if (!pressed) return
                if (!lifting && Math.abs(event.x - pressAt.x) + Math.abs(event.y - pressAt.y) > 6) {
                    lifting = true
                    view.beginPaletteCarry(tile.modelData.type, tile.modelData.title, tile.modelData.category, tile.modelData.isEvent, tile.modelData.holdsActions)
                }
                if (lifting) view.updateCarry(hover, event.x, event.y)
            }
            onReleased: (event) => {
                if (!lifting) {
                    tile.add()
                    return
                }
                view.updateCarry(hover, event.x, event.y)
                view.finishCarry()
                lifting = false
            }
            onCanceled: {
                if (lifting) view.cancelCarry()
                lifting = false
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: view.doc.loaded

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: paletteFrame

                Layout.fillHeight: true
                Layout.preferredWidth: 248
                color: Theme.panel
                z: 10

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    anchors.bottomMargin: 70
                    spacing: 10

                    Text {
                        text: "Snap blocks to give your game rules"
                        color: Theme.paper
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        font.family: Theme.body
                        font.weight: Font.DemiBold
                        font.pixelSize: 16
                    }

                    VariablesPanel {
                        Layout.fillWidth: true
                        doc: view.doc
                    }

                    SoundsPanel {
                        Layout.fillWidth: true
                        doc: view.doc
                        studio: view.studio
                    }

                    Item {
                        id: paletteArea
                        objectName: "paletteArea"
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Flickable {
                            id: paletteScroll

                            anchors.fill: parent
                            contentHeight: paletteColumn.implicitHeight + 8
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            ColumnLayout {
                                id: paletteColumn

                                width: paletteScroll.width
                                spacing: 14

                                Repeater {
                                    model: view.doc.palette

                                    ColumnLayout {
                                        id: group

                                        required property var modelData

                                        Layout.fillWidth: true
                                        spacing: 6

                                        Text {
                                            text: group.modelData.title
                                            color: Theme.muted
                                            font.family: Theme.body
                                            font.pixelSize: 14
                                            Layout.leftMargin: 2
                                        }

                                        Repeater {
                                            model: group.modelData.blocks
                                            delegate: PaletteTile {}
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            // Shows more blocks wait below when the list is taller than the panel.
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 24
                            visible: paletteScroll.contentY + paletteScroll.height < paletteScroll.contentHeight - 1
                            gradient: Gradient {
                                GradientStop { position: 0; color: Qt.rgba(0.11, 0.145, 0.286, 0) }
                                GradientStop { position: 1; color: Theme.panel }
                            }
                        }
                    }

                }

                Rectangle {
                    // Solid, so a long list always ends cleanly above the button.
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 70
                    color: Theme.panel

                    PillButton {
                        anchors.fill: parent
                        anchors.margins: 16
                        anchors.bottomMargin: 16
                        anchors.topMargin: 16
                        text: "Undo"
                        enabled: view.doc.canUndo
                        onClicked: view.doc.undo()
                    }
                }

                // Blocks carried off the canvas go in this bin.
                Rectangle {
                    anchors.fill: parent
                    visible: view.carrying && !view.carryingNew
                    color: Qt.rgba(0.1, 0.13, 0.27, 0.92)

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        text: "Drop here to remove these blocks"
                        color: view.overPalette ? Theme.signal : Theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.family: Theme.body
                        font.weight: Font.DemiBold
                        font.pixelSize: 18
                    }
                }
            }

            Item {
                id: canvasFrame

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                // Faint D-pad marks on the board; they stay put while the blocks scroll over them.
                Canvas {
                    anchors.fill: parent
                    opacity: 0.5
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        context.fillStyle = Theme.line
                        const spacing = 72
                        for (let x = spacing / 2; x < width; x += spacing) {
                            for (let y = spacing / 2; y < height; y += spacing) {
                                context.fillRect(x - 4, y - 0.75, 8, 1.5)
                                context.fillRect(x - 0.75, y - 4, 1.5, 8)
                            }
                        }
                    }
                }

                Flickable {
                    id: canvas

                    objectName: "logicCanvas"
                    anchors.fill: parent
                    anchors.topMargin: banner.visible ? banner.height + 18 : 0
                    contentWidth: Math.max(view.doc.contentWidth * view.zoom, width)
                    contentHeight: Math.max(view.doc.contentHeight * view.zoom, height)
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: !view.carrying

                    Binding {
                        target: view.doc
                        property: "canvasWidth"
                        value: canvas.width / view.zoom
                    }

                    Item {
                        id: board

                        objectName: "logicBoard"
                        width: canvas.contentWidth / view.zoom
                        height: canvas.contentHeight / view.zoom
                        scale: view.zoom
                        transformOrigin: Item.TopLeft

                        WheelHandler {
                            acceptedModifiers: Qt.ControlModifier | Qt.MetaModifier
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            enabled: !view.carrying
                            onWheel: (event) => {
                                const point_ = board.mapToItem(canvas, point.position.x, point.position.y)
                                view.zoomAt(Math.pow(1.0015, event.angleDelta.y), point_.x, point_.y)
                            }
                        }

                        MouseArea {
                            // A click on the board clears the selection and takes the keyboard away from a field.
                            anchors.fill: parent
                            onClicked: {
                                view.doc.selectedId = ""
                                view.focusCanvas()
                            }
                        }

                        BlockOutline {
                            id: landing

                            objectName: "landingSpot"
                            readonly property var spot: view.doc.landing

                            z: 900
                            x: spot.valid ? spot.x : x
                            y: spot.valid ? spot.y : y
                            visible: view.carrying && spot.valid === true
                            blockWidth: spot.valid ? spot.width : 440
                            headerHeight: spot.valid ? spot.height : 64
                            socket: !(spot.valid && spot.isEvent)
                            fill: Qt.rgba(1, 0.82, 0.25, 0.22)
                            stroke: Theme.signal
                            strokeWidth: 2.5
                            strokeStyle: ShapePath.DashLine

                            Behavior on x {
                                enabled: !Theme.reducedMotion
                                SpringAnimation { spring: 5; damping: 0.4; epsilon: 0.3; duration: Theme.ms(180) }
                            }
                            Behavior on y {
                                enabled: !Theme.reducedMotion
                                SpringAnimation { spring: 5; damping: 0.4; epsilon: 0.3; duration: Theme.ms(180) }
                            }
                        }

                        Repeater {
                            id: blocks
                            model: view.doc

                            delegate: LogicBlock {
                                doc: view.doc
                                logicView: view
                            }
                        }
                    }
                }

                ZoomButtons {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 14
                    z: 1400
                    visible: !view.doc.empty
                    buttons: [
                        { label: "\u2212", hint: "Zoom out", act: () => view.zoomAt(1 / 1.25, canvas.width / 2, canvas.height / 2) },
                        { label: Math.round(view.zoom * 100) + "%", hint: "Back to 100%", act: () => view.zoomAt(1 / view.zoom, canvas.width / 2, canvas.height / 2) },
                        { label: "+", hint: "Zoom in", act: () => view.zoomAt(1.25, canvas.width / 2, canvas.height / 2) }
                    ]
                }

                // The first step on an empty canvas.
                ColumnLayout {
                    id: emptyHint

                    x: Math.max(40, (canvasFrame.width - width) / 2)
                    y: Math.max(20, (canvasFrame.height - implicitHeight) / 2 - 20)
                    width: Math.min(canvasFrame.width - 80, 560)
                    visible: view.doc.empty
                    opacity: view.carrying ? 0.25 : 1
                    spacing: 18

                    Behavior on opacity { NumberAnimation { duration: Theme.ms(160) } }

                    readonly property string starterTitle: {
                        const events = view.doc.palette.find((group) => group.key === "events")
                        return events ? events.blocks[0].title : ""
                    }
                    readonly property string starterType: {
                        const events = view.doc.palette.find((group) => group.key === "events")
                        return events ? events.blocks[0].type : ""
                    }

                    BlockOutline {
                        Layout.preferredHeight: height
                        blockWidth: Math.min(380, emptyHint.width)
                        headerHeight: 68
                        socket: false
                        fill: Qt.rgba(1, 0.82, 0.25, 0.12)
                        stroke: Theme.signal
                        strokeWidth: 2
                        strokeStyle: ShapePath.DashLine

                        Text {
                            x: 22
                            height: 68
                            verticalAlignment: Text.AlignVCenter
                            text: emptyHint.starterTitle
                            color: Theme.signal
                            font.family: Theme.body
                            font.weight: Font.DemiBold
                            font.pixelSize: 19
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Give your game its first rule."
                        color: Theme.paper
                        wrapMode: Text.WordWrap
                        font.family: Theme.display
                        font.pixelSize: 46
                        lineHeight: 0.98
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Drag \"" + emptyHint.starterTitle + "\" from the left onto this board, then snap what should happen underneath it."
                        color: Theme.muted
                        wrapMode: Text.WordWrap
                        font.family: Theme.body
                        font.pixelSize: 18
                        lineHeight: 1.3
                    }

                    PillButton {
                        primary: true
                        text: "Add \"" + emptyHint.starterTitle + "\""
                        onClicked: {
                            const added = view.doc.addFromPalette(emptyHint.starterType)
                            if (added) view.focusBlock(added, Qt.TabFocusReason)
                        }
                    }
                }

                // Build blockers, in one place, with a way to jump to the first one.
                Rectangle {
                    id: banner

                    anchors.top: parent.top
                    anchors.topMargin: 14
                    x: Math.max(20, (parent.width - width) / 2)
                    width: Math.min(parent.width - 40, bannerRow.implicitWidth + 28)
                    height: 52
                    radius: 26
                    color: Theme.ink
                    border.width: 2
                    border.color: Theme.bad
                    visible: view.doc.problemCount > 0
                    z: 1500

                    RowLayout {
                        id: bannerRow

                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 8
                        spacing: 14

                        Text {
                            Layout.fillWidth: true
                            text: view.doc.programProblem.length > 0
                                  ? view.doc.programProblem
                                  : (view.doc.problemCount === 1 ? "1 block needs fixing before this can build." : view.doc.problemCount + " blocks need fixing before this can build.")
                            color: Theme.paper
                            elide: Text.ElideRight
                            font.family: Theme.body
                            font.weight: Font.Medium
                            font.pixelSize: 15
                        }

                        PillButton {
                            visible: view.doc.firstProblemId.length > 0
                            text: "Show me"
                            onClicked: view.showFirstProblem()
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 18
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width - 40, toastText.implicitWidth + 36)
                    height: toastText.implicitHeight + 18
                    radius: 18
                    color: Theme.panel
                    border.width: 1
                    border.color: Theme.line
                    visible: view.doc.error.length > 0
                    z: 1500

                    Text {
                        id: toastText
                        anchors.centerIn: parent
                        width: parent.width - 36
                        text: view.doc.error
                        color: Theme.bad
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        font.family: Theme.body
                        font.pixelSize: 15
                    }
                }
            }
        }

        // A palette block on its way to the canvas. It floats above both panels.
        BlockOutline {
            visible: view.carrying && view.carryingNew
            x: view.pointerX - view.gripX * view.zoom
            y: view.pointerY - view.gripY * view.zoom
            z: 3000
            transformOrigin: Item.TopLeft
            blockWidth: 440
            headerHeight: 64
            cavityHeight: view.carriedPalette.holdsActions && !view.carriedPalette.isEvent ? 60 : 0
            footerHeight: view.carriedPalette.holdsActions && !view.carriedPalette.isEvent ? 32 : 0
            socket: !view.carriedPalette.isEvent
            fill: view.carriedPalette.isEvent ? Theme.signal : Theme.raised
            stroke: Theme.signal
            strokeWidth: 2
            scale: 1.02 * view.zoom

            Text {
                x: 22
                height: 64
                verticalAlignment: Text.AlignVCenter
                text: view.carriedPalette.title || ""
                color: view.carriedPalette.isEvent ? Theme.ink : Theme.paper
                font.family: Theme.body
                font.weight: Font.DemiBold
                font.pixelSize: 18
            }
        }
    }

    // No scene yet, or logic.json is damaged.
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 96, 620)
        visible: !view.doc.loaded
        spacing: 22

        Text {
            Layout.fillWidth: true
            text: view.doc.loadError.length > 0 ? "The game logic file needs fixing." : "Add a scene to give your blocks something to move."
            color: Theme.paper
            wrapMode: Text.WordWrap
            font.family: Theme.display
            font.pixelSize: 48
            lineHeight: 0.96
        }

        Text {
            Layout.fillWidth: true
            text: view.doc.loadError.length > 0 ? view.doc.loadError : "Blocks act on the items in your scene. Add the scene editor, and the blocks you snap here build with the same button."
            color: Theme.muted
            wrapMode: Text.WordWrap
            font.family: Theme.body
            font.pixelSize: 18
            lineHeight: 1.3
        }

        PillButton {
            primary: true
            text: view.doc.loadError.length > 0 ? "Try again" : "Add the scene editor"
            onClicked: view.doc.loadError.length > 0 ? view.doc.reopen() : view.studio.addSceneEditor()
        }
    }
}
