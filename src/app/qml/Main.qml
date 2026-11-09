import QtQuick
import QtQuick.Window

Window {
    id: root

    required property StudioController studio
    required property bool reducedMotion
    property bool startOnNewGame: false
    property bool startOnTools: false
    // A binding until the person leaves the setup screen, which then ends it for good.
    property bool setupActive: !studio.hasProject && studio.needsToolSetup && !startOnNewGame

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
        sourceComponent: root.studio.hasProject ? projectView : root.setupActive ? setupView : welcomeView
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
        id: setupView

        SetupView {
            studio: root.studio
            onFinished: {
                root.startOnNewGame = true
                root.setupActive = false
            }
            onUseExistingFolders: {
                // First, because the next line ends setupActive's binding and swaps the view at once.
                root.startOnTools = true
                root.startOnNewGame = true
                root.setupActive = false
            }
        }
    }

    Component {
        id: welcomeView

        WelcomeView {
            studio: root.studio
            newGameOpen: root.startOnNewGame
            startOnTools: root.startOnTools
            onGuidedSetupRequested: {
                root.startOnNewGame = false
                root.startOnTools = false
                root.setupActive = true
            }
        }
    }

    Component {
        id: projectView
        ProjectView { studio: root.studio }
    }

    // Behind the content, so a drop area inside it (the scene stage) gets the first chance at a file.
    DropArea {
        anchors.fill: parent
        z: -1
        enabled: !root.studio.building && !root.studio.creatingGame && !root.studio.setup.running
        onDropped: (drop) => {
            if (drop.hasUrls) root.studio.openFolder(drop.urls[0])
        }
    }
}
