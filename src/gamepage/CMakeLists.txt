# The Game page's logic: param.json edits and image processing. It needs Qt Gui for images, so it
# stays out of studio_core.
add_library(studio_gamepage STATIC
    GamePage.cpp
    GamePage.h
    GamePageDocument.cpp
    GamePageDocument.h
)
target_include_directories(studio_gamepage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_gamepage PUBLIC studio_core Qt6::Core Qt6::Gui)
qt_extract_metatypes(studio_gamepage)
