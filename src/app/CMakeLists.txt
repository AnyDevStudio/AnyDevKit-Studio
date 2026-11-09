qt_add_executable(AnyDevKitStudio WIN32 MACOSX_BUNDLE
    main.cpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(AnyDevKitStudio
    URI AnyDevKitStudio
    VERSION 1.0
    # Keeps the module folder from taking the executable's name on Linux.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/AnyDevKitStudio
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/PillButton.qml
        qml/SafeAreaFrame.qml
        qml/WelcomeView.qml
        qml/ProjectView.qml
        qml/StageLane.qml
        qml/FileList.qml
        qml/LogPanel.qml
        qml/EditorPicker.qml
        qml/SceneStage.qml
        qml/SceneView.qml
        qml/SceneInspector.qml
        qml/Field.qml
        qml/ColorRow.qml
    SOURCES
        StudioController.cpp
        StudioController.h
        ReducedMotion.cpp
        ReducedMotion.h
        PixelText.cpp
        PixelText.h
    RESOURCES
        assets/fonts/ArchivoBlack-Regular.ttf
        assets/fonts/IBMPlexSans-Regular.ttf
        assets/fonts/IBMPlexSans-Medium.ttf
        assets/fonts/IBMPlexSans-SemiBold.ttf
        assets/fonts/IBMPlexMono-Regular.ttf
        assets/icon/app-icon.png
)

# The game runtime that "Add the scene editor" installs into a project.
file(GLOB_RECURSE scene_runtime_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/runtime/SceneGame/*")
qt_add_resources(AnyDevKitStudio "scene_runtime"
    PREFIX "/runtime"
    BASE "${PROJECT_SOURCE_DIR}/runtime"
    FILES ${scene_runtime_files}
)

target_link_libraries(AnyDevKitStudio PRIVATE studio_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

target_compile_definitions(AnyDevKitStudio PRIVATE PROJECT_VERSION_TEXT="${PROJECT_VERSION}")

if(APPLE)
    target_link_libraries(AnyDevKitStudio PRIVATE "-framework CoreFoundation")
endif()

set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/AppIcon.icns")
set_source_files_properties("${app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(AnyDevKitStudio PRIVATE "${app_icon}")
if(WIN32)
    target_sources(AnyDevKitStudio PRIVATE assets/icon/AppIcon.rc)
endif()

set_target_properties(AnyDevKitStudio PROPERTIES
    MACOSX_BUNDLE_ICON_FILE AppIcon.icns
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.utkuhalis.anydevkitstudio
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
