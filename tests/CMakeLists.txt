qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE studio_core Qt6::Test)
target_compile_definitions(tst_core PRIVATE STUDIO_TEST_CMAKE="${CMAKE_COMMAND}")
add_test(NAME tst_core COMMAND tst_core)

qt_add_executable(tst_scene tst_scene.cpp)
target_link_libraries(tst_scene PRIVATE studio_core Qt6::Test)
target_include_directories(tst_scene PRIVATE "${PROJECT_SOURCE_DIR}/runtime/SceneGame/include")
add_test(NAME tst_scene COMMAND tst_scene)

# Lets tst_scene install a runtime from a real Qt resource path.
set(scene_runtime_fixture "${CMAKE_CURRENT_BINARY_DIR}/scene_runtime_fixture")
file(WRITE "${scene_runtime_fixture}/CMakeLists.txt.in" "project(@NAME@)\n")
file(WRITE "${scene_runtime_fixture}/src/Main.cpp" "// @NAME@ main\n")
qt_add_resources(tst_scene "scene_runtime_fixture"
    PREFIX "/runtime/SceneGame"
    BASE "${scene_runtime_fixture}"
    FILES
        "${scene_runtime_fixture}/CMakeLists.txt.in"
        "${scene_runtime_fixture}/src/Main.cpp"
)

qt_add_executable(tst_scene_document tst_scene_document.cpp)
target_link_libraries(tst_scene_document PRIVATE studio_core Qt6::Test)
add_test(NAME tst_scene_document COMMAND tst_scene_document)

# The particle simulation from the game runtime, with no Qt so it can also run under sanitizers.
add_executable(tst_particles tst_particles.cpp)
target_include_directories(tst_particles PRIVATE "${PROJECT_SOURCE_DIR}/runtime/SceneGame/include")
add_test(NAME tst_particles COMMAND tst_particles)

qt_add_executable(tst_gamepage tst_gamepage.cpp)
target_link_libraries(tst_gamepage PRIVATE studio_gamepage Qt6::Test)
add_test(NAME tst_gamepage COMMAND tst_gamepage)

qt_add_executable(tst_blocks tst_blocks.cpp)
target_link_libraries(tst_blocks PRIVATE studio_core Qt6::Test)
target_compile_definitions(tst_blocks PRIVATE
    STUDIO_TEST_CXX="${CMAKE_CXX_COMPILER}"
    STUDIO_TEST_SYSROOT="${CMAKE_OSX_SYSROOT}"
    STUDIO_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(NAME tst_blocks COMMAND tst_blocks)

qt_add_executable(tst_logic_document tst_logic_document.cpp)
target_link_libraries(tst_logic_document PRIVATE studio_core Qt6::Test)
add_test(NAME tst_logic_document COMMAND tst_logic_document)

qt_add_executable(tst_scene_export tst_scene_export.cpp)
target_link_libraries(tst_scene_export PRIVATE studio_export Qt6::Test)
add_test(NAME tst_scene_export COMMAND tst_scene_export)

qt_add_executable(tst_font_tables tst_font_tables.cpp)
target_link_libraries(tst_font_tables PRIVATE Qt6::Test)
target_compile_definitions(tst_font_tables PRIVATE STUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME tst_font_tables COMMAND tst_font_tables)

qt_add_executable(tst_sprites_tilemap tst_sprites_tilemap.cpp)
target_link_libraries(tst_sprites_tilemap PRIVATE studio_core Qt6::Test)
add_test(NAME tst_sprites_tilemap COMMAND tst_sprites_tilemap)

# The camera, animation and tile culling from the game runtime, with no Qt so it can run under sanitizers.
add_executable(tst_world2d tst_world2d.cpp)
target_include_directories(tst_world2d PRIVATE "${PROJECT_SOURCE_DIR}/runtime/SceneGame/include")
add_test(NAME tst_world2d COMMAND tst_world2d)

# qt_standard_project_setup makes Windows executables GUI apps, which hides test output.
set_target_properties(tst_particles tst_world2d tst_sprites_tilemap tst_core tst_scene tst_scene_document tst_gamepage tst_blocks tst_logic_document tst_scene_export tst_font_tables PROPERTIES WIN32_EXECUTABLE FALSE)
