import QtQuick
import QtQuick.Window

Window {
    id: root

    required property StudioController studio
    required property bool reducedMotion
    property bool startOnNewGame: false

    width: 1280
    height: 800
    minimumWidth: 980
    minimumHeight: 620
    visible: true
    color: Theme.ink
    title: studio.hasProject ? studio.projectName + " - AnyDevKit Studio" : "AnyDevKit Studio"

    Component.onCompleted: Theme.reducedMotion = reducedMotion

    Loader {
        id: content

        anchors.fill: parent
        sourceComponent: root.studio.hasProject ? projectView : welcomeView
        opacity: 0
        scale: 0.992
        onLoaded: entrance.restart()

        ParallelAnimation {
            id: entrance

            NumberAnimation { target: content; property: "opacity"; to: 1; duration: Theme.ms(240); easing.type: Easing.OutCubic }
            SpringAnimation { target: content; property: "scale"; to: 1; spring: 5; damping: 0.5; duration: Theme.ms(280) }
        }
    }

    Component {
        id: welcomeView
        WelcomeView { studio: root.studio; newGameOpen: root.startOnNewGame }
    }

    Component {
        id: projectView
        ProjectView { studio: root.studio }
    }

    DropArea {
        anchors.fill: parent
        enabled: !root.studio.building && !root.studio.creatingGame
        onDropped: (drop) => {
            if (drop.hasUrls) root.studio.openFolder(drop.urls[0])
        }
    }
}
