import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AnyDevKitStudio

// The project's scenes as a strip of level tabs above the stage. One tab is the scene being edited; a
// small flag marks the one the game starts in. Double click a tab, or press Rename, to change its name.
Rectangle {
    id: strip

    required property SceneDocument scene

    property int renaming: -1

    function beginRename(index) {
        renaming = index
        nameInput.text = scene.sceneList[index].name
        nameInput.forceActiveFocus()
        nameInput.selectAll()
    }

    function commitRename() {
        if (renaming >= 0) scene.renameScene(renaming, nameInput.text)
        renaming = -1
    }

    implicitHeight: 52
    color: "transparent"

    component StripButton: T.Button {
        id: control

        implicitHeight: 32
        implicitWidth: implicitContentWidth + 26
        leftPadding: 13
        rightPadding: 13
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.Button
        Accessible.name: text

        contentItem: Text {
            text: control.text
            color: control.enabled ? Theme.paper : Theme.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.body
            font.pixelSize: 14
        }

        background: Rectangle {
            radius: height / 2
            color: control.hovered && control.enabled ? Theme.line : Theme.raised
            border.width: control.visualFocus ? 2 : 0
            border.color: Theme.paper
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        spacing: 10

        Text {
            text: "Scenes"
            color: Theme.muted
            font.family: Theme.body
            font.pixelSize: 14
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            visible: strip.renaming >= 0
            radius: 18
            color: Theme.ink
            border.width: 2
            border.color: Theme.signal

            TextInput {
                id: nameInput

                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.paper
                selectionColor: Theme.signal
                selectedTextColor: Theme.ink
                font.family: Theme.body
                font.pixelSize: 15
                maximumLength: 40
                onAccepted: strip.commitRename()
                onActiveFocusChanged: if (!activeFocus && strip.renaming >= 0) strip.commitRename()
                Keys.onEscapePressed: strip.renaming = -1
            }
        }

        ListView {
            id: tabs

            Layout.fillWidth: true
            Layout.preferredHeight: 36
            visible: strip.renaming < 0
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: strip.scene.sceneList
            currentIndex: strip.scene.currentSceneIndex

            delegate: Rectangle {
                id: tab

                required property var modelData
                required property int index
                readonly property bool isCurrent: index === strip.scene.currentSceneIndex

                implicitHeight: 36
                implicitWidth: Math.min(220, label.implicitWidth + (startMark.visible ? 34 : 20) + 16)
                radius: 18
                color: isCurrent ? Theme.signal : tabMouse.containsMouse ? Theme.line : Theme.raised
                border.width: tabFocus.activeFocus ? 2 : 0
                border.color: Theme.paper

                Behavior on color {
                    ColorAnimation { duration: Theme.ms(120) }
                }

                // The scene the game starts in carries a small pennant, in the colors of the tab it sits on.
                Canvas {
                    id: startMark

                    x: 13
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    visible: tab.modelData.isStart
                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        context.fillStyle = tab.isCurrent ? Theme.ink : Theme.signal
                        context.fillRect(2, 1, 2, 12)
                        context.beginPath()
                        context.moveTo(4, 1)
                        context.lineTo(13, 4)
                        context.lineTo(4, 8)
                        context.closePath()
                        context.fill()
                    }
                    Connections {
                        target: tab
                        function onIsCurrentChanged() { startMark.requestPaint() }
                    }
                }

                Text {
                    id: label

                    anchors.verticalCenter: parent.verticalCenter
                    x: startMark.visible ? 33 : 16
                    width: parent.width - x - 14
                    text: tab.modelData.name
                    color: tab.isCurrent ? Theme.ink : Theme.paper
                    elide: Text.ElideRight
                    font.family: Theme.body
                    font.weight: Font.Medium
                    font.pixelSize: 14
                }

                Item {
                    id: tabFocus

                    anchors.fill: parent
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Scene " + tab.modelData.name
                    Keys.onReturnPressed: strip.scene.selectScene(tab.index)
                    Keys.onSpacePressed: strip.scene.selectScene(tab.index)
                }

                MouseArea {
                    id: tabMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: strip.scene.selectScene(tab.index)
                    onDoubleClicked: {
                        strip.scene.selectScene(tab.index)
                        strip.beginRename(tab.index)
                    }
                }

            }
        }

        StripButton {
            text: "+ Add"
            enabled: strip.scene.sceneList.length < 32
            onClicked: {
                strip.renaming = -1
                strip.scene.addScene()
            }
        }

        StripButton {
            text: "Rename"
            onClicked: strip.beginRename(strip.scene.currentSceneIndex)
        }

        StripButton {
            text: "Duplicate"
            enabled: strip.scene.sceneList.length < 32
            onClicked: strip.scene.duplicateScene(strip.scene.currentSceneIndex)
        }

        StripButton {
            text: "Delete"
            enabled: strip.scene.sceneList.length > 1
            onClicked: {
                strip.renaming = -1
                strip.scene.deleteScene(strip.scene.currentSceneIndex)
            }
        }

        StripButton {
            text: strip.scene.startSceneIndex === strip.scene.currentSceneIndex ? "Start scene" : "Make start"
            enabled: strip.scene.startSceneIndex !== strip.scene.currentSceneIndex
            onClicked: strip.scene.setStartScene(strip.scene.currentSceneIndex)
        }
    }
}
