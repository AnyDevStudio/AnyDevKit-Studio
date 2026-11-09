add_library(studio_core STATIC
    Project.cpp
    Project.h
    BuildRunner.cpp
    BuildRunner.h
    CodeEditors.cpp
    CodeEditors.h
)
target_include_directories(studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_core PUBLIC Qt6::Core)
