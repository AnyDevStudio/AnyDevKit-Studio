import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// One block on the canvas. Its fields come from the catalog's flags, so a new block type draws here
// without any change.
T.Control {
    id: block

    required property string blockId
    required property string blockType
    required property string title
    required property string category
    required property bool isEvent
    required property bool holdsActions
    required property var fields
    required property int runOrder
    required property int depth
    required property real blockX
    required property real blockY
    required property real blockWidth
    required property real blockHeight
    required property real headerHeight
    required property real cavityHeight
    required property real footerHeight
    required property real problemHeight
    required property bool twoRows
    required property string item
    required property string otherItem
    required property string itemName
    required property string otherItemName
    required property string button
    required property real pointX
    required property real pointY
    required property string blockColor
    required property int value
    required property string comparison
    required property string variable
    required property string sound
    required property string animation
    required property bool flipped
    required property string problem
    required property bool dragged
    required property bool emptyCavity

    property LogicDocument doc
    property Item logicView

    // Where this block was when it was picked up, so a carried stack keeps its shape.
    property real originX: 0
    property real originY: 0

    readonly property bool selected: doc.selectedId === blockId
    readonly property bool hasProblem: problem.length > 0
    readonly property color titleColor: isEvent ? Theme.ink : Theme.paper
    readonly property color softColor: isEvent ? Theme.raised : Theme.muted

    function fieldValue(key) {
        switch (key) {
        case "item": return item
        case "otherItem": return otherItem
        case "variable": return variable
        case "sound": return sound
        case "button": return button
        case "comparison": return comparison
        case "x": return String(pointX)
        case "y": return String(pointY)
        case "value": return String(value)
        case "color": return blockColor
        case "animation": return animation
        case "flipped": return flipped ? "true" : "false"
        default: return ""
        }
    }

    function fieldName(key) {
        if (key === "item") return itemName
        if (key === "otherItem") return otherItemName
        if (key === "variable") return variable.length === 0 ? "" : doc.variables.some((choice) => choice.name === variable) ? variable : "Missing variable"
        if (key === "sound") return sound.length === 0 ? "" : doc.sounds.some((choice) => choice.name === sound) ? sound : "Missing sound"
        if (key === "animation") return animation.length === 0 ? "" : choices("animation").some((choice) => choice.id === animation) ? animation : "Missing animation"
        if (key === "flipped") return flipped ? "Face left" : "Face right"
        const list = key === "button" ? doc.buttons : doc.comparisons
        const found = list.find((choice) => choice.id === fieldValue(key))
        return found ? found.name : ""
    }

    function choices(key) {
        if (key === "item" || key === "otherItem") return doc.sceneItems
        if (key === "variable") return doc.variables
        if (key === "sound") return doc.sounds
        if (key === "animation") {
            const chosen = doc.sceneItems.find((candidate) => candidate.id === item)
            return chosen ? chosen.animations : []
        }
        if (key === "flipped") return [{ id: "false", name: "Face right" }, { id: "true", name: "Face left" }]
        return key === "button" ? doc.buttons : doc.comparisons
    }

    function placeholder(key) {
        if (key === "button") return "Choose a button"
        if (key === "comparison") return "Choose a comparison"
        if (key === "variable") return "Choose a variable"
        if (key === "sound") return "Choose a sound"
        if (key === "animation") return "Choose animation"
        if (key === "flipped") return "Choose direction"
        return "Choose item"
    }

    function focusFirstField() {
        const first = fieldRow.children.length > 0 ? fieldRow.children[0] : null
        if (first) first.takeFocus()
    }

    x: dragged ? logicView.carryX + originX - logicView.carryOriginX : blockX
    y: dragged ? logicView.carryY + originY - logicView.carryOriginY : blockY
    width: blockWidth
    height: blockHeight
    z: dragged ? 1000 + depth : depth

    Behavior on x {
        enabled: !Theme.reducedMotion && !block.dragged
        SpringAnimation { spring: 4.6; damping: 0.34; epsilon: 0.3; duration: Theme.ms(280) }
    }
    Behavior on y {
        enabled: !Theme.reducedMotion && !block.dragged
        SpringAnimation { spring: 4.6; damping: 0.34; epsilon: 0.3; duration: Theme.ms(280) }
    }

    objectName: "block_" + blockId
    // A block that just arrived settles into place with the same spring as a move.
    SpringAnimation {
        id: settle
        target: block
        property: "scale"
        to: 1
        spring: 5
        damping: 0.34
        duration: Theme.ms(260)
    }

    Component.onCompleted: {
        if (!logicView.ready || Theme.reducedMotion) return
        scale = 0.9
        settle.start()
    }

    focusPolicy: Qt.TabFocus
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: title + (hasProblem ? ". " + problem : "")
    onActiveFocusChanged: if (activeFocus) doc.selectedId = blockId

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            const next = doc.nextBlockId(blockId) || doc.previousBlockId(blockId)
            doc.removeBlock(blockId)
            logicView.focusBlock(next)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            focusFirstField()
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            logicView.focusBlock(doc.nextBlockId(blockId))
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            logicView.focusBlock(doc.previousBlockId(blockId))
            event.accepted = true
        } else if (event.key === Qt.Key_Tab) {
            const next = doc.nextBlockId(blockId)
            if (next) {
                logicView.focusBlock(next)
                event.accepted = true
            }
        } else if (event.key === Qt.Key_Backtab) {
            const previous = doc.previousBlockId(blockId)
            if (previous) {
                logicView.focusBlock(previous)
                event.accepted = true
            }
        }
    }

    component FieldEditor: Item {
        id: editor

        required property var modelData
        readonly property string key: modelData.key
        readonly property string kind: modelData.kind
        readonly property string label: modelData.label

        function takeFocus() { loader.item.forceFocus() }

        implicitWidth: loader.implicitWidth + (labelText.visible ? labelText.implicitWidth + 8 : 0)
        implicitHeight: 40

        Text {
            id: labelText
            visible: editor.label.length > 0 && editor.kind === "item"
            anchors.verticalCenter: parent.verticalCenter
            text: editor.label
            color: block.softColor
            font.family: Theme.body
            font.pixelSize: 15
        }

        Loader {
            id: loader
            x: labelText.visible ? labelText.implicitWidth + 8 : 0
            sourceComponent: editor.kind === "number" || editor.kind === "whole" ? numberField : editor.kind === "color" ? colorField : pickerField
        }

        Component {
            id: pickerField
            BlockPicker {
                function forceFocus() { forceActiveFocus(Qt.TabFocusReason) }
                objectName: "field_" + block.blockId + "_" + editor.key
                preferredWidth: editor.kind === "comparison" ? 172 : editor.kind === "button" ? 148 : editor.kind === "animation" ? 140 : editor.kind === "flip" ? 118 : 168
                choices: block.choices(editor.key)
                current: block.fieldValue(editor.key)
                shown: block.fieldName(editor.key)
                placeholder: block.placeholder(editor.key)
                invalid: (editor.kind === "item" || editor.kind === "variable" || editor.kind === "sound" || editor.kind === "animation") && (shown === "Missing item" || shown === "Missing variable" || shown === "Missing sound" || shown === "" || shown === "Missing animation")
                emptyText: editor.kind === "variable" ? "Add a variable in the Variables area first." : editor.kind === "animation" ? "Give the item a sprite sheet and an animation first." : editor.kind === "sound" ? "Import a sound in the Sounds area first." : "Add an item in the Scene tab first."
                onChosen: (id) => block.doc.setField(block.blockId, editor.key, id)
                Accessible.description: block.title
            }
        }

        Component {
            id: numberField
            BlockNumber {
                function forceFocus() { takeFocus() }
                inputName: "field_" + block.blockId + "_" + editor.key
                label: editor.label
                labelColor: block.softColor
                whole: editor.kind === "whole"
                value: block.fieldValue(editor.key)
                onCommitted: (text) => block.doc.setField(block.blockId, editor.key, Number(text))
            }
        }

        Component {
            id: colorField
            BlockColorPicker {
                function forceFocus() { forceActiveFocus(Qt.TabFocusReason) }
                objectName: "field_" + block.blockId + "_" + editor.key
                value: block.fieldValue(editor.key)
                onChosen: (color) => block.doc.setField(block.blockId, editor.key, color)
            }
        }
    }

    // A hard offset shape rather than a blur, so a lifted stack still looks printed, not floating.
    BlockOutline {
        x: 8
        y: 10
        visible: block.dragged
        blockWidth: block.blockWidth
        headerHeight: block.headerHeight
        cavityHeight: block.cavityHeight
        footerHeight: block.footerHeight
        socket: !block.isEvent
        fill: Theme.ink
        stroke: Theme.ink
        opacity: 0.55
    }

    BlockOutline {
        id: shape

        blockWidth: block.blockWidth
        headerHeight: block.headerHeight
        cavityHeight: block.cavityHeight
        footerHeight: block.footerHeight
        socket: !block.isEvent
        fill: block.isEvent ? Theme.signal : Theme.raised
        stroke: block.hasProblem ? Theme.bad
              : block.selected || block.dragged ? Theme.signal
              : mouse.containsMouse ? Theme.muted
              : block.isEvent ? Theme.signal : Theme.line
        strokeWidth: block.hasProblem || block.selected ? 2.5 : 1.5
    }

    // The keyboard ring: shown only when focus arrived by Tab or the arrow keys.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -5
        radius: 14
        color: "transparent"
        border.width: 2
        border.color: Theme.paper
        visible: block.visualFocus
    }

    MouseArea {
        id: mouse

        property point pressAt
        property bool lifting: false

        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

        onPressed: (event) => {
            block.forceActiveFocus(Qt.MouseFocusReason)
            block.doc.selectedId = block.blockId
            pressAt = Qt.point(event.x, event.y)
            lifting = false
        }

        onPositionChanged: (event) => {
            if (!pressed) return
            if (!lifting && Math.abs(event.x - pressAt.x) + Math.abs(event.y - pressAt.y) > 6) {
                lifting = true
                block.logicView.beginCarry(block.blockId, pressAt.x, pressAt.y)
            }
            if (lifting) block.logicView.updateCarry(mouse, event.x, event.y)
        }

        onReleased: (event) => {
            if (!lifting) return
            block.logicView.updateCarry(mouse, event.x, event.y)
            block.logicView.finishCarry()
            lifting = false
        }

        onCanceled: {
            if (!lifting) return
            block.logicView.cancelCarry()
            lifting = false
        }
    }

    // A wrapping block's empty middle says what belongs in it.
    Text {
        visible: block.emptyCavity
        x: block.footerHeight > 0 ? 38 + 18 : 18
        y: block.headerHeight + (block.cavityHeight - height) / 2
        text: "Snap blocks in here"
        color: Theme.muted
        font.family: Theme.body
        font.pixelSize: 15
    }

    RowLayout {
        id: titleRow

        x: 18
        y: 0
        width: block.blockWidth - 36 - (chip.visible ? chip.width + 6 : 0)
        height: 64
        spacing: 12

        BlockGlyph {
            kind: block.category
            tint: block.isEvent ? Theme.ink : Theme.signal
        }

        Text {
            Layout.fillWidth: true
            text: block.title
            color: block.titleColor
            elide: Text.ElideRight
            font.family: Theme.body
            font.weight: Font.DemiBold
            font.pixelSize: 18
        }
    }

    // A run order the person can read. Events run from first to last.
    Rectangle {
        id: chip

        visible: block.isEvent
        x: block.blockWidth - 18 - width
        y: 18
        width: 28
        height: 28
        radius: 14
        color: Theme.ink

        Text {
            anchors.centerIn: parent
            text: block.runOrder
            color: Theme.signal
            font.family: Theme.mono
            font.pixelSize: 14
        }
    }

    Row {
        id: fieldRow

        x: block.twoRows ? 18 : block.blockWidth - 18 - implicitWidth - (chip.visible ? chip.width + 8 : 0)
        y: block.twoRows ? 62 : 12
        spacing: 10

        Repeater {
            model: block.fields
            delegate: FieldEditor {}
        }
    }

    Rectangle {
        visible: block.hasProblem
        x: 12
        y: block.headerHeight - block.problemHeight + 2
        width: block.blockWidth - 24
        height: block.problemHeight - 8
        radius: 8
        color: Theme.ink

        Text {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            verticalAlignment: Text.AlignVCenter
            text: block.problem
            color: Theme.bad
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 2
            font.family: Theme.body
            font.pixelSize: 14
        }
    }
}
