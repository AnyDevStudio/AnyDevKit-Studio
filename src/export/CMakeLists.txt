add_library(studio_export STATIC
    SceneExport.cpp
    SceneExport.h
    SceneImageImport.cpp
    SceneImageImport.h
    SoundImport.cpp
    SoundImport.h
)
target_include_directories(studio_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_export PUBLIC studio_core Qt6::Gui)

# Optional: decodes MP3 and OGG on import and plays previews. Without it, only WAV can be imported.
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(TARGET Qt6::Multimedia)
    target_link_libraries(studio_export PUBLIC Qt6::Multimedia)
    target_compile_definitions(studio_export PUBLIC STUDIO_HAS_MULTIMEDIA)
endif()
