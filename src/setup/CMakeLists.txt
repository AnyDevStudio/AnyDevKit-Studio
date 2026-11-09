# The guided first-run setup: downloading AnyDevKit, unpacking it safely and running adk setup.
# It needs Qt Network, so it stays out of studio_core.
add_library(studio_setup STATIC
    HostTools.cpp
    HostTools.h
    HttpClient.cpp
    HttpClient.h
    ReleaseAssets.cpp
    ReleaseAssets.h
    SafeArchive.cpp
    SafeArchive.h
    SetupFlow.cpp
    SetupFlow.h
    SetupProgress.cpp
    SetupProgress.h
)
target_include_directories(studio_setup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_setup PUBLIC studio_core Qt6::Core Qt6::Network)
qt_extract_metatypes(studio_setup)
