#pragma once

#include "Scene.h"

namespace Studio {

struct ImportedSceneImage {
    QString source; // project-relative, for example assets/logo.png
    QString name;   // the picked file's name, for the editor
    QString notice; // set when the picture had to be changed to fit, to tell the user
    int width = 0;
    int height = 0;
};

// Copies a PNG, JPEG or WebP file into the project's assets folder as a PNG with a safe unique name.
// `scene` is what the new picture has to fit next to in the scene's image memory. Pictures past the
// format's size limit are scaled down, which `imported.notice` reports. The source file is never changed.
bool importSceneImage(const QString& projectRoot, const Scene& scene, const QString& filePath, ImportedSceneImage& imported, QString& error);

}
