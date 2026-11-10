import QtQuick
import QtQuick.Shapes

// A small mark for each block group, drawn so the groups read apart without needing more colors.
Shape {
    id: glyph

    property string kind: "events"
    property color tint: Theme.signal

    width: 20
    height: 20
    preferredRendererType: Shape.CurveRenderer

    function pathData() {
        switch (kind) {
        case "movement":
            return "M 3 17 L 15 5 M 7 4.5 L 16 4 L 15.5 13"
        case "looks":
            return "M 2 10 C 5 4.5 15 4.5 18 10 C 15 15.5 5 15.5 2 10 Z M 10 12.2 A 2.2 2.2 0 1 0 10 7.8 A 2.2 2.2 0 1 0 10 12.2 Z"
        case "variables":
            return "M 4 3.5 L 4 16.5 M 16 3.5 L 16 16.5 M 4 10 L 16 10 M 8.5 3 L 8.5 17"
        case "sound":
            return "M 3 8 L 3 12 L 6.5 12 L 11 16 L 11 4 L 6.5 8 Z M 14 7 C 15.5 8.5 15.5 11.5 14 13"
        case "physics":
            return "M 3 16 L 3 13 L 7 13 L 7 16 M 13 4 L 13 7 L 17 7 L 17 4 M 7 13 C 7 9 13 11 13 7"
        case "scenes":
            return "M 3 3 L 17 3 L 17 17 L 3 17 Z M 3 7 L 17 7 M 8 3 L 8 7"
        case "players": // two heads side by side
            return "M 7 8.5 A 2.8 2.8 0 1 0 7 2.9 A 2.8 2.8 0 1 0 7 8.5 Z M 2 17 C 2 12 12 12 12 17 M 14 9 A 2.4 2.4 0 1 0 14 4.2 A 2.4 2.4 0 1 0 14 9 Z M 13.5 11.6 C 16 11.6 18 13.5 18 16"
        case "copies": // a stack of cards, one stamped out of the next
            return "M 3 6 L 12 6 L 12 17 L 3 17 Z M 6 3 L 17 3 L 17 14 L 12 14"
        case "behavior": // a wandering path ending in a step
            return "M 3 16 C 6 10 9 17 11 11 C 12.5 6.5 15 8 17 4 M 14.5 3.5 L 17 4 L 16.8 6.6"
        case "saving":
            return "M 4 3 L 13.5 3 L 16.5 6 L 16.5 17 L 4 17 Z M 7 3 L 7 7.5 L 12.5 7.5 L 12.5 3 M 7 17 L 7 11.5 L 13.5 11.5 L 13.5 17"
        default:
            return "M 6 3 L 15.5 10 L 6 17 Z"
        }
    }

    ShapePath {
        fillColor: glyph.kind === "events" ? glyph.tint : "transparent"
        strokeColor: glyph.tint
        strokeWidth: 2
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin

        PathSvg { path: glyph.pathData() }
    }
}
