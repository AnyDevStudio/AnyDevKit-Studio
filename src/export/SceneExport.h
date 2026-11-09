#pragma once

#include "Scene.h"

namespace Studio {

// Decodes every image the scene uses. This is the only part of the export that needs Qt Gui, which
// is why it lives outside studio_core. Images come back in sceneImageSources order.
bool decodeSceneImages(const QString& projectRoot, const Scene& scene, QList<SceneImageData>& images, QString& error);

// Decodes the scene's images and writes the generated files into the project.
bool exportScene(const QString& projectRoot, const Scene& scene, QString& error);

}
