import QtQuick
import QtQuick.Shapes

// A small mark for each project type, drawn on a 28 unit grid like the block group marks.
Shape {
    id: glyph

    property string kind: "2d"
    property color tint: Theme.paper

    width: 28
    height: 28
    preferredRendererType: Shape.CurveRenderer

    function strokePath() {
        switch (kind) {
        case "app": // a window with a header, the shape of the starter screen
            return "M 5 4 L 23 4 A 2 2 0 0 1 25 6 L 25 22 A 2 2 0 0 1 23 24 L 5 24 A 2 2 0 0 1 3 22 L 3 6 A 2 2 0 0 1 5 4 Z M 3 10 L 25 10 M 8 20 L 20 20"
        case "code": // a shell prompt with its cursor
            return "M 5 8 L 12 14 L 5 20 M 15 21 L 24 21"
        case "3d": // a wireframe cube
            return "M 14 3 L 24 9 L 24 19 L 14 25 L 4 19 L 4 9 Z M 4 9 L 14 14 L 24 9 M 14 14 L 14 25"
        default: // the starter scene's square, jumping along the ground
            return "M 3 24 L 25 24 M 3 18 C 4 11 8 8 13 8"
        }
    }

    function fillPath() {
        switch (kind) {
        case "app":
            return "M 8 14 L 20 14 L 20 17 L 8 17 Z"
        case "2d":
            return "M 15 7 L 23 7 L 23 15 L 15 15 Z"
        default:
            return ""
        }
    }

    ShapePath {
        fillColor: "transparent"
        strokeColor: glyph.tint
        strokeWidth: 2
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin

        PathSvg { path: glyph.strokePath() }
    }

    ShapePath {
        fillColor: glyph.tint
        strokeColor: "transparent"
        strokeWidth: 0

        PathSvg { path: glyph.fillPath() }
    }
}
