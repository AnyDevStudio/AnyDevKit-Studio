qt_add_executable(AnyDevKitStudio WIN32 MACOSX_BUNDLE
    main.cpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(AnyDevKitStudio
    URI AnyDevKitStudio
    VERSION 1.0
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
    SOURCES
        StudioController.cpp
        StudioController.h
        ReducedMotion.cpp
        ReducedMotion.h
    RESOURCES
        assets/fonts/ArchivoBlack-Regular.ttf
        assets/fonts/IBMPlexSans-Regular.ttf
        assets/fonts/IBMPlexSans-Medium.ttf
        assets/fonts/IBMPlexSans-SemiBold.ttf
        assets/fonts/IBMPlexMono-Regular.ttf
)

target_link_libraries(AnyDevKitStudio PRIVATE studio_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

target_compile_definitions(AnyDevKitStudio PRIVATE PROJECT_VERSION_TEXT="${PROJECT_VERSION}")

if(APPLE)
    target_link_libraries(AnyDevKitStudio PRIVATE "-framework CoreFoundation")
endif()

set_target_properties(AnyDevKitStudio PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.utkuhalis.anydevkitstudio
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
