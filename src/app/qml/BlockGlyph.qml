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
