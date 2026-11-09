#pragma once

#include "Sounds.h"

namespace Studio {

// Imports a sound into the project. A WAV is copied as it is; another format is decoded here and stored
// as a WAV, which is why this is outside studio_core: decoding uses Qt Multimedia when this copy of
// Studio was built with it. Without Qt Multimedia only WAV can be imported, and the message says so.
bool importSoundFile(const QString& projectRoot, const QList<ProjectSound>& existing, const QString& filePath, ImportedSound& imported, QString& error);

// True when this copy of Studio can decode MP3 and OGG.
bool canDecodeCompressedSound();

}
