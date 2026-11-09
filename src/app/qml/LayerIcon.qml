import QtQuick
import QtQuick.Shapes

// The small line icons of the layers panel, drawn the same way as the block group marks.
Shape {
    id: icon

    property string kind: "eye"
    property color tint: Theme.muted

    width: 20
    height: 20
    preferredRendererType: Shape.CurveRenderer

    function pathData() {
        switch (kind) {
        case "eyeOff":
            return "M 2 10 C 5 4.5 15 4.5 18 10 C 15 15.5 5 15.5 2 10 Z M 3.5 3.5 L 16.5 16.5"
        case "duplicate":
            return "M 7.5 7.5 L 16.5 7.5 L 16.5 16.5 L 7.5 16.5 Z M 4.5 12.5 L 3.5 12.5 L 3.5 3.5 L 12.5 3.5 L 12.5 4.5"
        case "delete":
            return "M 4 6 L 16 6 M 8 6 L 8 3.5 L 12 3.5 L 12 6 M 6 6 L 6.8 16.5 L 13.2 16.5 L 14 6"
        case "forward":
            return "M 5 12.5 L 10 7.5 L 15 12.5"
        case "backward":
            return "M 5 7.5 L 10 12.5 L 15 7.5"
        case "toFront":
            return "M 4 4 L 16 4 M 5 15 L 10 10 L 15 15"
        case "toBack":
            return "M 4 16 L 16 16 M 5 5 L 10 10 L 15 5"
        case "grip":
            return "M 7.5 5 L 7.5 5.01 M 12.5 5 L 12.5 5.01 M 7.5 10 L 7.5 10.01 M 12.5 10 L 12.5 10.01 M 7.5 15 L 7.5 15.01 M 12.5 15 L 12.5 15.01"
        default:
            return "M 2 10 C 5 4.5 15 4.5 18 10 C 15 15.5 5 15.5 2 10 Z M 10 12.2 A 2.2 2.2 0 1 0 10 7.8 A 2.2 2.2 0 1 0 10 12.2 Z"
        }
    }

    ShapePath {
        fillColor: "transparent"
        strokeColor: icon.tint
        strokeWidth: icon.kind === "grip" ? 2.4 : 1.8
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin

        PathSvg { path: icon.pathData() }
    }
}
