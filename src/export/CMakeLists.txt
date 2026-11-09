add_library(studio_export STATIC
    SceneExport.cpp
    SceneExport.h
    SceneImageImport.cpp
    SceneImageImport.h
)
target_include_directories(studio_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_export PUBLIC studio_core Qt6::Gui)
