add_library(studio_core STATIC
    Blocks.cpp
    Blocks.h
    LogicDocument.cpp
    LogicDocument.h
    LogicLayout.cpp
    LogicLayout.h
    Project.cpp
    Project.h
    BuildRunner.cpp
    BuildRunner.h
    CodeEditors.cpp
    CodeEditors.h
    NewGame.cpp
    NewGame.h
    Scene.cpp
    Scene.h
    SceneDocument.cpp
    SceneDocument.h
)
target_include_directories(studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_core PUBLIC Qt6::Core)
qt_extract_metatypes(studio_core)
