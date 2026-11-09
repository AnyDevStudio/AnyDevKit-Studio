#pragma once

#include "Scene/Particles.hpp.in"

#include <QColor>
#include <QQuickItem>
#include <QRectF>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace Studio {

// Runs a particles item on the stage with the same simulation the game uses. Its size is the whole
// stage, because particles leave the item's area.
class ParticlePreview : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap settings READ settings WRITE setSettings NOTIFY settingsChanged)
    Q_PROPERTY(QRectF area READ area WRITE setArea NOTIFY areaChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged)
    // True shows one fixed moment of the effect instead of running it (Theme.reducedMotion).
    Q_PROPERTY(bool still READ still WRITE setStill NOTIFY stillChanged)

public:
    explicit ParticlePreview(QQuickItem* parent = nullptr);

    QVariantMap settings() const;
    void setSettings(const QVariantMap& settings);
    QRectF area() const;
    void setArea(const QRectF& area);
    QColor background() const;
    void setBackground(const QColor& color);
    bool still() const;
    void setStill(bool still);

    Q_INVOKABLE void advance(double seconds);

signals:
    void settingsChanged();
    void areaChanged();
    void backgroundChanged();
    void stillChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void rebuild();

    QVariantMap values;
    QRectF region;
    QColor sky = Qt::black;
    bool keepStill = false;

    Particles::Emitter emitter;
    std::vector<Particles::Particle> pool;
    std::vector<Particles::Quad> quads;
    Particles::Effect effect{};
    double replayClock = 0.0;
    bool ready = false;
};

}
