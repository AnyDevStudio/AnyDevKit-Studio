import QtQuick
import QtQuick.Dialogs
import AnyDevKitStudio

// Chooses a picture for the scene, either as a new item or to replace the selected one.
FileDialog {
    id: picker

    required property StudioController studio

    property real centerX: 960
    property real centerY: 540
    property int replacing: -1

    function chooseNew(x, y) {
        replacing = -1
        centerX = x
        centerY = y
        open()
    }

    function chooseReplacement(index) {
        replacing = index
        open()
    }

    title: replacing >= 0 ? "Replace image" : "Add image"
    nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)", "All files (*)"]
    onAccepted: {
        if (replacing >= 0) studio.replaceSceneImage(replacing, selectedFile)
        else studio.addSceneImage(selectedFile, centerX, centerY)
    }
}
