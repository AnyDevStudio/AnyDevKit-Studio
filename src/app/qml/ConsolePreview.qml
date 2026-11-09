import QtQuick
import AnyDevKitStudio

// Studio's own impression of a console home screen: a backdrop, a game shelf and the game's name.
Rectangle {
    id: preview

    required property string name
    required property url icon
    required property url background

    readonly property real unit: width / 100

    radius: 10
    color: Theme.ink
    clip: true

    Rectangle {
        anchors.fill: parent
        visible: preview.background.toString().length === 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.ink }
            GradientStop { position: 1; color: Theme.raised }
        }

        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -preview.height * 0.16
            text: "Your background goes here"
            color: Theme.line
            font.family: Theme.body
            font.pixelSize: Math.max(13, preview.unit * 1.8)
        }
    }

    Image {
        anchors.fill: parent
        source: preview.background
        cache: false
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: 1920
        opacity: status === Image.Ready ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: Theme.ms(260); easing.type: Easing.OutCubic } }
    }

    SafeAreaFrame {
        anchors.fill: parent
        tickColor: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.35)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.62
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0) }
            GradientStop { position: 1; color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.92) }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: shelf.top
        anchors.leftMargin: preview.unit * 6
        anchors.rightMargin: preview.unit * 6
        anchors.bottomMargin: preview.unit * 2.4
        text: preview.name.length > 0 ? preview.name : "Name your game"
        color: Theme.paper
        opacity: preview.name.length > 0 ? 1 : 0.5
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        font.family: Theme.display
        font.pixelSize: preview.unit * 6.6
        lineHeight: 0.94
    }

    Row {
        id: shelf

        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: preview.unit * 6
        anchors.bottomMargin: preview.unit * 4.8
        spacing: preview.unit * 1.6

        Item {
            id: gameTile

            width: preview.unit * 13.5
            height: width

            property real lift: 0
            Behavior on lift { SpringAnimation { spring: 4; damping: 0.45; duration: Theme.ms(320) } }

            Component.onCompleted: lift = -preview.unit * 0.9

            Item {
                anchors.fill: parent
                anchors.topMargin: gameTile.lift
                anchors.bottomMargin: -gameTile.lift

                Rectangle {
                    anchors.fill: parent
                    radius: ring.radius
                    color: Theme.raised
                }

                Image {
                    anchors.fill: parent
                    anchors.margins: ring.border.width - 0.5
                    source: preview.icon
                    cache: false
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 256
                    opacity: status === Image.Ready ? 1 : 0

                    Behavior on opacity { NumberAnimation { duration: Theme.ms(180) } }
                }

                Text {
                    anchors.centerIn: parent
                    visible: preview.icon.toString().length === 0
                    text: "Icon"
                    color: Theme.muted
                    font.family: Theme.body
                    font.pixelSize: Math.max(11, preview.unit * 1.6)
                }

                // Sits over the picture, and its thickness hides the picture's square corners.
                Rectangle {
                    id: ring

                    anchors.fill: parent
                    radius: width * 0.13
                    color: "transparent"
                    border.width: Math.max(3, width * 0.042)
                    border.color: Theme.signal
                }
            }
        }

        Repeater {
            model: 4

            Rectangle {
                required property int index

                anchors.bottom: parent.bottom
                width: preview.unit * 9.4
                height: width
                radius: width * 0.16
                color: "transparent"
                border.width: 1
                border.color: Theme.paper
                opacity: 0.32 - index * 0.06
            }
        }
    }
}
