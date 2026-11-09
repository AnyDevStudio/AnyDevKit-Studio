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
        qml/NewGameForm.qml
        qml/ProjectTypeCard.qml
        qml/ProjectTypeGlyph.qml
        qml/ProjectView.qml
        qml/StageLane.qml
        qml/FileList.qml
        qml/LogPanel.qml
        qml/EditorPicker.qml
        qml/ExportFormatPicker.qml
        qml/PkgKeysPanel.qml
        qml/SceneStage.qml
        qml/SceneView.qml
        qml/SceneInspector.qml
        qml/LayerPanel.qml
        qml/LayerIcon.qml
        qml/ImagePicker.qml
        qml/ImageGlyph.qml
        qml/ImagePlaceholder.qml
        qml/LogicView.qml
        qml/VariablesPanel.qml
        qml/SoundsPanel.qml
        qml/LogicBlock.qml
        qml/BlockOutline.qml
        qml/BlockGlyph.qml
        qml/BlockPicker.qml
        qml/BlockNumber.qml
        qml/BlockColorPicker.qml
        qml/GamePageView.qml
        qml/ConsolePreview.qml
        qml/Field.qml
        qml/ColorRow.qml
        qml/ChoiceRow.qml
        qml/SliderRow.qml
        qml/PixelBurst.qml
    SOURCES
        StudioController.cpp
        StudioController.h
        SoundPreviewPlayer.cpp
        SoundPreviewPlayer.h
        ReducedMotion.cpp
        ReducedMotion.h
        PixelText.cpp
        PixelText.h
        PixelFont.h
        ParticlePreview.cpp
        ParticlePreview.h
    RESOURCES
        assets/fonts/ArchivoBlack-Regular.ttf
        assets/fonts/IBMPlexSans-Regular.ttf
        assets/fonts/IBMPlexSans-Medium.ttf
        assets/fonts/IBMPlexSans-SemiBold.ttf
        assets/fonts/IBMPlexMono-Regular.ttf
        assets/icon/app-icon.png
        assets/defaults/icon0.png
        assets/defaults/pic0.png
        assets/defaults/pic1.png
)

# The game runtime that "Add the scene editor" installs into a project.
file(GLOB_RECURSE scene_runtime_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/runtime/SceneGame/*")
qt_add_resources(AnyDevKitStudio "scene_runtime"
    PREFIX "/runtime"
    BASE "${PROJECT_SOURCE_DIR}/runtime"
    FILES ${scene_runtime_files}
)

target_link_libraries(AnyDevKitStudio PRIVATE studio_core studio_gamepage studio_export Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
# The stage preview compiles the game's particle simulation, so the two always match.
target_include_directories(AnyDevKitStudio PRIVATE "${PROJECT_SOURCE_DIR}/runtime/SceneGame/include")

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

# `cmake --install` gives a self-contained app on macOS and Windows: Qt, its plugins and the QML
# modules are copied next to the program. On Linux it installs only the program, its menu entry
# and its icon, which is what a distribution package wants. The AppImage job bundles Qt on top.
include(GNUInstallDirs)
install(TARGETS AnyDevKitStudio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(APPLE OR WIN32)
    # The deploy tools skip the offscreen platform, which the release workflow starts the packaged
    # app on. Placing it first lets macdeployqt repoint its Qt libraries into the bundle.
    if(APPLE)
        set(offscreen_plugin_dir "AnyDevKitStudio.app/Contents/PlugIns/platforms")
    else()
        set(offscreen_plugin_dir "plugins/platforms")
    endif()
    # REAL_PATH because Homebrew's plugin folder holds symlinks, which would dangle inside the app.
    install(CODE "
        file(REAL_PATH \"$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>\" offscreen_plugin)
        file(INSTALL \"\${offscreen_plugin}\"
            DESTINATION \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${offscreen_plugin_dir}\"
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        )
    ")

    if(WIN32)
        # A zip has no installer to add the Visual C++ runtime, so its DLLs travel with the program.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
        include(InstallRequiredSystemLibraries)
    endif()

    qt_generate_deploy_qml_app_script(
        TARGET AnyDevKitStudio
        OUTPUT_SCRIPT deploy_script
        NO_COMPILER_RUNTIME
        NO_TRANSLATIONS
    )
    install(SCRIPT "${deploy_script}")

    if(APPLE)
        install(CODE "
            execute_process(
                COMMAND \"${PROJECT_SOURCE_DIR}/packaging/macos/fix-install-names.sh\"
                    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/AnyDevKitStudio.app\"
                COMMAND_ERROR_IS_FATAL ANY
            )
        ")
    endif()
else()
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/anydevkit-studio.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
    install(FILES assets/icon/app-icon.png
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
        RENAME anydevkit-studio.png
    )
endif()
