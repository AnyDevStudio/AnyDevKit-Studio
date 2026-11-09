#include "ParticlePreview.h"

#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>

namespace Studio {

namespace {

// How long a one-shot effect's preview waits before it plays again, so a finished effect can be seen.
constexpr double OneShotReplaySeconds = 3.6;
// The moment shown as a still frame: late enough for every preset to be under way.
constexpr double StillSeconds = 2.6;

Particles::Color toParticleColor(const QColor& color) {
    return {color.redF(), color.greenF(), color.blueF()};
}

std::uint32_t fnv1a(const QString& text) {
    std::uint32_t hash = 2166136261u;
    for (const char byte : text.toUtf8()) hash = (hash ^ static_cast<unsigned char>(byte)) * 16777619u;
    return hash;
}

std::uint8_t indexOf(const auto& names, const QString& name) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (QString::fromLatin1(names[index]) == name) return static_cast<std::uint8_t>(index);
    }
    return 0;
}

}

ParticlePreview::ParticlePreview(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}

QVariantMap ParticlePreview::settings() const { return values; }
QRectF ParticlePreview::area() const { return region; }
QColor ParticlePreview::background() const { return sky; }
bool ParticlePreview::still() const { return keepStill; }

void ParticlePreview::setSettings(const QVariantMap& settings) {
    if (settings == values) return;
    values = settings;
    emit settingsChanged();
    rebuild();
}

void ParticlePreview::setArea(const QRectF& area) {
    if (area == region) return;
    // Moving keeps a running effect going; resizing changes where particles live, so it restarts,
    // and a still frame is drawn again for its new place.
    const auto sameSize = area.size() == region.size();
    region = area;
    emit areaChanged();
    if (sameSize && ready && !keepStill) emitter.SetPosition(static_cast<float>(region.x()), static_cast<float>(region.y()));
    else rebuild();
}

void ParticlePreview::setBackground(const QColor& color) {
    if (color == sky) return;
    sky = color;
    emit backgroundChanged();
    rebuild();
}

void ParticlePreview::setStill(bool still) {
    if (still == keepStill) return;
    keepStill = still;
    emit stillChanged();
    rebuild();
}

void ParticlePreview::rebuild() {
    const auto presetName = values.value("preset").toString();
    if (presetName.isEmpty() || region.width() <= 0 || region.height() <= 0) {
        ready = false;
        update();
        return;
    }

    const auto color = QColor::fromString(values.value("color").toString());
    effect.preset = static_cast<Particles::Preset>(indexOf(Particles::PresetNames, presetName));
    effect.palette = static_cast<Particles::Palette>(indexOf(Particles::PaletteNames, values.value("palette").toString()));
    effect.amount = static_cast<std::uint32_t>(std::max(values.value("amount").toInt(), 1));
    effect.seed = fnv1a(values.value("id").toString());
    effect.spread = static_cast<float>(values.value("spread").toInt()) / 100.0f;
    effect.speed = static_cast<float>(values.value("speed").toInt()) / 100.0f;
    effect.gravity = static_cast<float>(values.value("gravity").toInt()) / 100.0f;
    effect.pixelSize = static_cast<float>(std::max(values.value("pixelSize").toInt(), 1));
    effect.looping = values.value("looping").toBool();

    pool.assign(effect.amount, Particles::Particle{});
    quads.assign(effect.amount, Particles::Quad{});
    const Particles::Area area{static_cast<float>(region.x()), static_cast<float>(region.y()), static_cast<float>(region.width()), static_cast<float>(region.height())};
    emitter.Reset(effect, area, toParticleColor(color), toParticleColor(sky), pool, quads);
    ready = true;
    replayClock = 0.0;

    if (keepStill) {
        for (double elapsed = 0.0; elapsed < StillSeconds; elapsed += 1.0 / 60.0) emitter.Update(1.0 / 60.0);
    }
    update();
}

void ParticlePreview::advance(double seconds) {
    if (!ready || keepStill) return;
    if (!effect.looping) {
        replayClock += seconds;
        if (replayClock >= OneShotReplaySeconds) {
            rebuild();
            return;
        }
    }
    emitter.Update(seconds);
    update();
}

void ParticlePreview::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode* ParticlePreview::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGGeometryNode*>(old);
    const auto shown = ready ? emitter.Quads() : std::span<const Particles::Quad>{};
    if (shown.empty()) {
        delete node;
        return nullptr;
    }

    if (!node) {
        node = new QSGGeometryNode;
        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    auto* geometry = node->geometry();
    geometry->allocate(static_cast<int>(shown.size() * 6));
    auto* vertex = geometry->vertexDataAsColoredPoint2D();
    const auto unit = static_cast<float>(width() / Particles::StageWidth);
    for (const auto& quad : shown) {
        const auto left = quad.x * unit;
        const auto top = quad.y * unit;
        const auto right = (quad.x + quad.width) * unit;
        const auto bottom = (quad.y + quad.height) * unit;
        const auto red = static_cast<uchar>(quad.color.red * 255.0f);
        const auto green = static_cast<uchar>(quad.color.green * 255.0f);
        const auto blue = static_cast<uchar>(quad.color.blue * 255.0f);
        for (const auto [x, y] : {std::pair{left, top}, {right, top}, {left, bottom}, {left, bottom}, {right, top}, {right, bottom}}) {
            vertex++->set(x, y, red, green, blue, 255);
        }
    }
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

}
