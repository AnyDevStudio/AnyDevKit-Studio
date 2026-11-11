import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// Every item on the stage, front-most first. Items are drawn in scene order with later ones on
// top, so the rows are the scene's items reversed. They are placed by hand for that reversal.
Rectangle {
    id: panel

    required property SceneDocument scene

    readonly property int rowHeight: 32
    readonly property int count: scene.count
    readonly property int selected: scene.selectedIndex
    readonly property bool hasSelection: selected >= 0
    readonly property string defaultHint: count > 0 ? "Top row is in front" : ""
    readonly property string keyboardHint: (Qt.platform.os === "osx" ? "Option" : "Alt") + "+arrows move, Space hides, Return renames"

    // The hint under the pointer, or the keyboard help once the keyboard is in use here.
    property string hoverHint: ""

    // While a row is dragged: which item, its name for the ghost, the pointer inside the list,
    // and the gap between rows it would land in.
    property int dragIndex: -1
    property string dragName: ""
    property real dragX: 0
    property real dragY: 0
    property int dropGap: -1
    readonly property bool dragging: dragIndex >= 0

    color: Theme.panel

    function rowTop(index) { return (count - 1 - index) * rowHeight }

    function reveal(index) {
        if (index < 0 || flick.height <= 0) return
        const top = rowTop(index)
        if (top < flick.contentY) flick.contentY = top
        else if (top + rowHeight > flick.contentY + flick.height) flick.contentY = Math.max(0, top + rowHeight - flick.height)
    }

    function moveDrag(viewX, viewY) {
        dragX = viewX
        dragY = viewY
        dropGap = Math.max(0, Math.min(count, Math.round((viewY + flick.contentY) / rowHeight)))
    }

    function endDrag(drop) {
        const item = dragIndex
        const gap = dropGap
        dragIndex = -1
        dropGap = -1
        if (drop && item >= 0 && gap >= 0) scene.reorderItemToGap(item, gap)
    }

    function handleKey(event) {
        const index = selected
        const alt = event.modifiers & Qt.AltModifier
        const shift = event.modifiers & Qt.ShiftModifier
        const up = event.key === Qt.Key_Up
        const down = event.key === Qt.Key_Down
        if (up || down) {
            if (alt && index >= 0) {
                if (shift) up ? scene.bringItemToFront(index) : scene.sendItemToBack(index)
                else up ? scene.bringItemForward(index) : scene.sendItemBackward(index)
            } else if (!alt && count > 0) {
                scene.selectedIndex = index < 0 ? count - 1 : Math.max(0, Math.min(count - 1, up ? index + 1 : index - 1))
            }
        } else if (index < 0) {
            return
        } else if (event.key === Qt.Key_Space) {
            scene.setItemHidden(index, !scene.isItemHidden(index))
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_F2) {
            rows.itemAt(index).startRename()
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            scene.removeItem(index)
        } else {
            return
        }
        list.keyboardInUse = true
        event.accepted = true
    }

    Connections {
        target: panel.scene
        function onSelectionChanged() { panel.reveal(panel.selected) }
    }

    component IconButton: T.Button {
        id: button

        required property string iconKind
        property string hint
        property color tint: hovered && enabled ? Theme.paper : Theme.muted

        implicitWidth: 28
        implicitHeight: 28
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.name: hint
        onHoveredChanged: panel.hoverHint = hovered && enabled ? hint : ""
        scale: down ? 0.9 : 1

        Behavior on scale {
            SpringAnimation { spring: 6; damping: 0.42; duration: Theme.ms(180) }
        }

        contentItem: Item {
            LayerIcon {
                anchors.centerIn: parent
                kind: button.iconKind
                tint: button.tint
                opacity: button.enabled ? 1 : 0.35
            }
        }

        background: Rectangle {
            radius: height / 2
            color: button.hovered && button.enabled ? Theme.raised : "transparent"

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: height / 2
                color: "transparent"
                border.width: 2
                border.color: Theme.paper
                visible: button.visualFocus
            }
        }
    }

    // A miniature of the item on the stage's own background color, in the item's own color.
    component LayerChip: Rectangle {
        id: chip

        required property string kind
        required property color tint
        property bool hiddenItem: false

        implicitWidth: 32
        implicitHeight: 22
        radius: 6
        color: panel.scene.background
        border.width: 1
        border.color: Theme.line
        clip: true

        Item {
            anchors.fill: parent
            opacity: chip.hiddenItem ? 0.35 : 1

            Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 12
                color: chip.tint
                visible: chip.kind === "rectangle"
            }

            PixelText {
                anchors.centerIn: parent
                width: 20
                height: 10
                visible: chip.kind === "text"
                text: "Ab"
                color: chip.tint
            }

            PixelBurst {
                anchors.centerIn: parent
                cell: 3
                visible: chip.kind === "particles"
                color: chip.tint
            }

            Loader {
                anchors.centerIn: parent
                width: 20
                height: 13
                active: chip.kind === "image"
                sourceComponent: ImageGlyph { cutout: chip.color }
            }

            // A tilemap's chip: a small grid of cells, standing in for the map.
            Grid {
                anchors.centerIn: parent
                visible: chip.kind === "tilemap"
                columns: 3
                rows: 2
                spacing: 1

                Repeater {
                    model: 6
                    Rectangle { width: 5; height: 5; color: chip.tint; opacity: index % 3 === 1 ? 0.4 : 1 }
                }
            }
        }

        // Crossed through when hidden, so the state reads even at a glance down the list.
        Loader {
            anchors.fill: parent
            active: chip.hiddenItem
            sourceComponent: Canvas {
                id: hatch
                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    context.strokeStyle = Theme.muted
                    context.lineWidth = 1
                    for (let x = -height; x < width; x += 6) {
                        context.beginPath()
                        context.moveTo(x, height)
                        context.lineTo(x + height, 0)
                        context.stroke()
                    }
                }
                Component.onCompleted: requestPaint()
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.line
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 30
            Layout.rightMargin: 10
            spacing: 8

            Text {
                text: "Layers"
                color: Theme.paper
                font.family: Theme.body
                font.weight: Font.DemiBold
                font.pixelSize: 15
            }

            Text {
                text: panel.count + "/1024"
                color: panel.count >= 1024 ? Theme.bad : Theme.muted
                font.family: Theme.mono
                font.pixelSize: 13
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                text: panel.hoverHint.length > 0 ? panel.hoverHint : list.showKeyboardHelp ? panel.keyboardHint : panel.defaultHint
                color: Theme.muted
                elide: Text.ElideRight
                font.family: Theme.body
                font.pixelSize: 13
            }

            IconButton {
                iconKind: "toFront"
                hint: "To front"
                focusPolicy: Qt.TabFocus
                enabled: panel.hasSelection && panel.selected < panel.count - 1
                onClicked: panel.scene.bringItemToFront(panel.selected)
            }

            IconButton {
                iconKind: "forward"
                hint: "Bring forward"
                focusPolicy: Qt.TabFocus
                enabled: panel.hasSelection && panel.selected < panel.count - 1
                onClicked: panel.scene.bringItemForward(panel.selected)
            }

            IconButton {
                iconKind: "backward"
                hint: "Send backward"
                focusPolicy: Qt.TabFocus
                enabled: panel.selected > 0
                onClicked: panel.scene.sendItemBackward(panel.selected)
            }

            IconButton {
                iconKind: "toBack"
                hint: "To back"
                focusPolicy: Qt.TabFocus
                enabled: panel.selected > 0
                onClicked: panel.scene.sendItemToBack(panel.selected)
            }
        }

        T.Control {
            id: list

            // Clicking a row focuses the list too, but the ring is only for keyboard use.
            property bool keyboardInUse: false
            readonly property bool showKeyboardHelp: activeFocus && (visualFocus || keyboardInUse)

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            focusPolicy: Qt.TabFocus
            Keys.onPressed: (event) => panel.handleKey(event)
            onActiveFocusChanged: if (!activeFocus) keyboardInUse = false

            contentItem: Item {
                Flickable {
                    id: flick

                    anchors.fill: parent
                    contentWidth: width
                    contentHeight: panel.count * panel.rowHeight
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    Repeater {
                        id: rows

                        model: panel.scene

                        Item {
                            id: row

                            required property int index
                            required property string itemId
                            required property string itemType
                            required property string itemName
                            required property color itemColor
                            required property bool itemHidden

                            readonly property bool selected: panel.selected === index
                            readonly property bool grabbed: panel.dragIndex === index
                            readonly property string label: itemName.length > 0 ? itemName : itemId
                            property bool renaming: false

                            function startRename() { renaming = true }

                            function finishRename(text, commit, refocus) {
                                if (!renaming) return
                                renaming = false
                                if (commit && text !== itemName) panel.scene.setItemValue(index, "name", text)
                                if (refocus) list.forceActiveFocus(Qt.OtherFocusReason)
                            }

                            objectName: "layerRow-" + itemId
                            width: flick.width
                            height: panel.rowHeight
                            y: panel.rowTop(index)
                            // Only rows in view are drawn, which keeps a long list cheap.
                            visible: y + height > flick.contentY && y < flick.contentY + flick.height
                            z: grabbed ? 1 : 0
                            opacity: grabbed ? 0.4 : 1
                            Accessible.role: Accessible.ListItem
                            Accessible.name: label + (itemHidden ? ", hidden while editing" : "")
                            Accessible.selected: selected

                            Behavior on y {
                                enabled: !panel.dragging
                                NumberAnimation { duration: Theme.ms(150); easing.type: Easing.OutCubic }
                            }
                            Behavior on opacity {
                                NumberAnimation { duration: Theme.ms(90) }
                            }

                            HoverHandler { id: rowHover }

                            Rectangle {
                                anchors.fill: parent
                                anchors.topMargin: 1
                                anchors.bottomMargin: 1
                                radius: 8
                                color: row.selected ? Theme.raised : rowHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"

                                Behavior on color {
                                    ColorAnimation { duration: Theme.ms(90) }
                                }

                                // The stage's signal yellow marks the row the stage has selected.
                                Rectangle {
                                    x: 3
                                    y: 7
                                    width: 3
                                    height: parent.height - 14
                                    radius: 1.5
                                    color: Theme.signal
                                    visible: row.selected
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -1
                                    radius: 9
                                    color: "transparent"
                                    border.width: 2
                                    border.color: Theme.paper
                                    visible: row.selected && list.showKeyboardHelp
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                cursorShape: panel.dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor

                                property real pressY: 0

                                onPressed: (mouse) => {
                                    panel.scene.selectedIndex = row.index
                                    list.forceActiveFocus(Qt.MouseFocusReason)
                                    list.keyboardInUse = false
                                    pressY = mouse.y
                                }
                                onPositionChanged: (mouse) => {
                                    if (!pressed) return
                                    if (panel.dragIndex < 0) {
                                        if (Math.abs(mouse.y - pressY) < 6) return
                                        panel.dragName = row.label
                                        panel.dragIndex = row.index
                                    }
                                    const point = mapToItem(flick, mouse.x, mouse.y)
                                    panel.moveDrag(point.x, point.y)
                                }
                                onReleased: if (panel.dragging) panel.endDrag(true)
                                onCanceled: panel.endDrag(false)
                                onDoubleClicked: row.startRename()
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 6
                                spacing: 6

                                LayerIcon {
                                    kind: "grip"
                                    tint: Theme.line
                                    opacity: rowHover.hovered || row.selected ? 1 : 0
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 20

                                    Behavior on opacity {
                                        NumberAnimation { duration: Theme.ms(90) }
                                    }
                                }

                                LayerChip {
                                    kind: row.itemType
                                    tint: row.itemColor
                                    hiddenItem: row.itemHidden
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: !row.renaming
                                    text: row.label
                                    color: row.itemHidden ? Theme.muted : Theme.paper
                                    elide: Text.ElideRight
                                    font.family: Theme.body
                                    font.pixelSize: 14
                                    font.strikeout: row.itemHidden
                                    Accessible.ignored: true
                                }

                                Loader {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 26
                                    active: row.renaming
                                    visible: active
                                    // Only now is the field in the window, so it can really take focus.
                                    onLoaded: {
                                        item.forceActiveFocus()
                                        item.selectAll()
                                    }

                                    sourceComponent: T.TextField {
                                        id: renameField

                                        verticalAlignment: TextInput.AlignVCenter
                                        leftPadding: 8
                                        rightPadding: 8
                                        color: Theme.paper
                                        selectionColor: Theme.signal
                                        selectedTextColor: Theme.ink
                                        maximumLength: 64
                                        font.family: Theme.body
                                        font.pixelSize: 14
                                        text: row.itemName
                                        Keys.onEscapePressed: row.finishRename(text, false, true)
                                        onAccepted: row.finishRename(text, true, true)
                                        onEditingFinished: row.finishRename(text, true, false)

                                        background: Rectangle {
                                            radius: 6
                                            color: Theme.ink
                                            border.width: 2
                                            border.color: Theme.signal
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: row.itemHidden && !row.renaming
                                    implicitWidth: hiddenLabel.implicitWidth + 16
                                    implicitHeight: 20
                                    radius: height / 2
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Theme.line

                                    Text {
                                        id: hiddenLabel
                                        anchors.centerIn: parent
                                        text: "Hidden"
                                        color: Theme.signal
                                        font.family: Theme.body
                                        font.weight: Font.DemiBold
                                        font.pixelSize: 11
                                    }
                                }

                                Loader {
                                    active: (rowHover.hovered || row.selected) && !row.renaming
                                    visible: active

                                    sourceComponent: RowLayout {
                                        spacing: 2

                                        IconButton {
                                            objectName: "layerDuplicate-" + row.itemId
                                            iconKind: "duplicate"
                                            hint: "Duplicate (" + (Qt.platform.os === "osx" ? "Cmd" : "Ctrl") + "+D)"
                                            onClicked: panel.scene.duplicateItem(row.index)
                                        }

                                        IconButton {
                                            objectName: "layerDelete-" + row.itemId
                                            iconKind: "delete"
                                            hint: "Delete"
                                            onClicked: panel.scene.removeItem(row.index)
                                        }
                                    }
                                }

                                IconButton {
                                    objectName: "layerEye-" + row.itemId
                                    iconKind: row.itemHidden ? "eyeOff" : "eye"
                                    hint: row.itemHidden ? "Show on the stage" : "Hide while editing. The game still shows it"
                                    tint: row.itemHidden ? Theme.signal : hovered ? Theme.paper : Theme.muted
                                    onClicked: panel.scene.setItemHidden(row.index, !row.itemHidden)
                                }
                            }
                        }
                    }

                    // Where the dragged row would land.
                    Item {
                        x: 2
                        y: panel.dropGap * panel.rowHeight - 1
                        width: flick.width - 4
                        height: 2
                        z: 10
                        visible: panel.dragging && panel.dropGap >= 0 && panel.dropGap !== panel.count - 1 - panel.dragIndex && panel.dropGap !== panel.count - panel.dragIndex

                        Behavior on y {
                            NumberAnimation { duration: Theme.ms(80); easing.type: Easing.OutCubic }
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.signal
                        }

                        Rectangle {
                            x: -2
                            y: -3
                            width: 8
                            height: 8
                            color: Theme.signal
                        }
                    }
                }

                // Only there when the list is taller than the panel.
                Rectangle {
                    anchors.right: parent.right
                    width: 3
                    radius: 1.5
                    color: Theme.line
                    visible: flick.contentHeight > flick.height
                    y: flick.visibleArea.yPosition * flick.height
                    height: flick.visibleArea.heightRatio * flick.height
                }

                Text {
                    anchors.fill: parent
                    visible: panel.count === 0
                    text: "Nothing on the stage yet. Drag a shape, text or image onto it."
                    color: Theme.muted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                    font.family: Theme.body
                    font.pixelSize: 14
                }
            }

            background: Item {
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    radius: 10
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.paper
                    visible: list.showKeyboardHelp && !panel.hasSelection
                }
            }

            // Scrolls while a dragged row sits at the top or bottom edge of a long list.
            Timer {
                interval: 30
                repeat: true
                running: panel.dragging
                onTriggered: {
                    const edge = 26
                    let step = 0
                    if (panel.dragY < edge) step = -8
                    else if (panel.dragY > flick.height - edge) step = 8
                    if (step === 0) return
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY + step))
                    panel.moveDrag(panel.dragX, panel.dragY)
                }
            }
        }
    }

    // Follows the pointer, to the right of it so the names underneath stay readable.
    Rectangle {
        id: ghost

        x: Math.max(8, Math.min(panel.width - width - 8, list.x + panel.dragX + 16))
        y: list.y + Math.max(0, Math.min(list.height - height, panel.dragY - height / 2))
        width: Math.min(panel.width * 0.6, ghostLabel.implicitWidth + 44)
        height: 28
        radius: height / 2
        visible: opacity > 0
        opacity: panel.dragging ? 1 : 0
        color: Theme.raised
        border.width: 1
        border.color: Theme.signal
        z: 20

        Behavior on opacity {
            NumberAnimation { duration: Theme.ms(90) }
        }

        LayerIcon {
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            kind: "grip"
            tint: Theme.signal
        }

        Text {
            id: ghostLabel
            anchors.left: parent.left
            anchors.leftMargin: 32
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: panel.dragName
            color: Theme.paper
            elide: Text.ElideRight
            font.family: Theme.body
            font.pixelSize: 13
        }
    }
}
