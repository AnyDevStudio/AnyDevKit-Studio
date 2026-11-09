import QtQuick
import QtQuick.Shapes

// One jigsaw piece. The tab is the arm of a D-pad: a straight sided key ending in a point, aimed at
// the block that runs next. The socket on the top edge takes the tab of the block above it.
Shape {
    id: outline

    property real blockWidth: 440
    property real headerHeight: 64
    property real cavityHeight: 0
    property real footerHeight: 0
    property real indent: 38
    property bool socket: true
    property bool tab: true
    property color fill: Theme.raised
    property color stroke: Theme.line
    property real strokeWidth: 1.5
    property int strokeStyle: ShapePath.SolidLine

    readonly property bool wraps: cavityHeight > 0
    readonly property real corner: 12
    readonly property real notchX: 26
    readonly property real notchWidth: 34
    readonly property real notchEdge: 5
    readonly property real notchPoint: 10
    readonly property real tabInset: 2
    readonly property real totalHeight: headerHeight + cavityHeight + footerHeight

    width: blockWidth
    height: totalHeight + (tab ? notchEdge + notchPoint : 0)
    preferredRendererType: Shape.CurveRenderer

    function pathData() {
        const w = blockWidth
        const h = totalHeight
        const r = corner
        const nx = notchX
        const nw = notchWidth
        const edge = notchEdge
        const point = notchPoint
        const inset = tabInset
        let d = ""
        const add = (s) => { d += s + " " }

        // Top edge, left to right, with the socket cut into it.
        add("M " + r + " 0")
        if (socket) {
            add("L " + nx + " 0")
            add("L " + nx + " " + edge)
            add("L " + (nx + nw / 2) + " " + (edge + point))
            add("L " + (nx + nw) + " " + edge)
            add("L " + (nx + nw) + " 0")
        }
        add("L " + (w - r) + " 0")
        add("A " + r + " " + r + " 0 0 1 " + w + " " + r)

        if (wraps) {
            const inner = 8
            const hdr = headerHeight
            const cav = hdr + cavityHeight
            const ix = indent + nx
            // Right end of the header, then the underside of the header back to the spine.
            add("L " + w + " " + (hdr - r))
            add("A " + r + " " + r + " 0 0 1 " + (w - r) + " " + hdr)
            add("L " + (ix + nw - inset) + " " + hdr)
            // The header's own tab, into the first nested block.
            add("L " + (ix + nw - inset) + " " + (hdr + edge))
            add("L " + (ix + nw / 2) + " " + (hdr + edge + point))
            add("L " + (ix + inset) + " " + (hdr + edge))
            add("L " + (ix + inset) + " " + hdr)
            add("L " + (indent + inner) + " " + hdr)
            add("A " + inner + " " + inner + " 0 0 0 " + indent + " " + (hdr + inner))
            add("L " + indent + " " + (cav - inner))
            add("A " + inner + " " + inner + " 0 0 0 " + (indent + inner) + " " + cav)
            add("L " + (w - r) + " " + cav)
            add("A " + r + " " + r + " 0 0 1 " + w + " " + (cav + r))
            add("L " + w + " " + (h - r))
            add("A " + r + " " + r + " 0 0 1 " + (w - r) + " " + h)
        } else {
            add("L " + w + " " + (h - r))
            add("A " + r + " " + r + " 0 0 1 " + (w - r) + " " + h)
        }

        // Bottom edge, right to left, with the tab pointing at the next block.
        if (tab) {
            add("L " + (nx + nw - inset) + " " + h)
            add("L " + (nx + nw - inset) + " " + (h + edge))
            add("L " + (nx + nw / 2) + " " + (h + edge + point))
            add("L " + (nx + inset) + " " + (h + edge))
            add("L " + (nx + inset) + " " + h)
        }
        add("L " + r + " " + h)
        add("A " + r + " " + r + " 0 0 1 0 " + (h - r))
        add("L 0 " + r)
        add("A " + r + " " + r + " 0 0 1 " + r + " 0")
        add("Z")
        return d
    }

    ShapePath {
        fillColor: outline.fill
        strokeColor: outline.stroke
        strokeWidth: outline.strokeWidth
        strokeStyle: outline.strokeStyle
        dashPattern: [4, 3]
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin

        PathSvg { path: outline.pathData() }
    }
}
