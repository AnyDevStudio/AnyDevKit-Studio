import QtQuick

// The palette's picture symbol: a frame with a sun and two hills cut out of it.
Canvas {
    id: glyph

    property color fill: Theme.signal
    property color cutout: Theme.ink

    onFillChanged: requestPaint()
    onCutoutChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const context = getContext("2d")
        const w = width
        const h = height
        context.reset()
        context.fillStyle = glyph.fill
        context.fillRect(0, 0, w, h)
        context.fillStyle = glyph.cutout
        context.beginPath()
        context.arc(w * 0.74, h * 0.28, h * 0.13, 0, Math.PI * 2)
        context.fill()
        context.beginPath()
        context.moveTo(w * 0.08, h * 0.9)
        context.lineTo(w * 0.34, h * 0.36)
        context.lineTo(w * 0.52, h * 0.68)
        context.lineTo(w * 0.66, h * 0.46)
        context.lineTo(w * 0.92, h * 0.9)
        context.closePath()
        context.fill()
    }
}
