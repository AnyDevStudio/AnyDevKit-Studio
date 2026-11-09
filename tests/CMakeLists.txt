qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE studio_core Qt6::Test)
target_compile_definitions(tst_core PRIVATE STUDIO_TEST_CMAKE="${CMAKE_COMMAND}")
add_test(NAME tst_core COMMAND tst_core)

qt_add_executable(tst_scene tst_scene.cpp)
target_link_libraries(tst_scene PRIVATE studio_core Qt6::Test)
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

qt_add_executable(tst_gamepage tst_gamepage.cpp)
target_link_libraries(tst_gamepage PRIVATE studio_gamepage Qt6::Test)
add_test(NAME tst_gamepage COMMAND tst_gamepage)

# qt_standard_project_setup makes Windows executables GUI apps, which hides test output.
set_target_properties(tst_core tst_scene tst_scene_document tst_gamepage PROPERTIES WIN32_EXECUTABLE FALSE)
