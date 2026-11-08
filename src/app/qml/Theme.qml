pragma Singleton

import QtQuick

QtObject {
    readonly property color ink: "#151C3A"
    readonly property color panel: "#1C2549"
    readonly property color raised: "#2A3463"
    readonly property color line: "#3D4983"
    readonly property color paper: "#F3F1E2"
    readonly property color muted: "#AAB4D6"
    readonly property color signal: "#FFD23F"
    readonly property color good: "#67E3C8"
    readonly property color bad: "#FF8373"

    property bool reducedMotion: false
    function ms(duration) { return reducedMotion ? 0 : duration }

    readonly property FontLoader archivo: FontLoader { source: "qrc:/qt/qml/AnyDevKitStudio/assets/fonts/ArchivoBlack-Regular.ttf" }
    readonly property FontLoader plexRegular: FontLoader { source: "qrc:/qt/qml/AnyDevKitStudio/assets/fonts/IBMPlexSans-Regular.ttf" }
    readonly property FontLoader plexMedium: FontLoader { source: "qrc:/qt/qml/AnyDevKitStudio/assets/fonts/IBMPlexSans-Medium.ttf" }
    readonly property FontLoader plexSemiBold: FontLoader { source: "qrc:/qt/qml/AnyDevKitStudio/assets/fonts/IBMPlexSans-SemiBold.ttf" }
    readonly property FontLoader plexMono: FontLoader { source: "qrc:/qt/qml/AnyDevKitStudio/assets/fonts/IBMPlexMono-Regular.ttf" }

    readonly property string display: archivo.font.family
    readonly property string body: plexRegular.font.family
    readonly property string mono: plexMono.font.family
}
