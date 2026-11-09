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

# The sound mixer and the save format from the game runtime, always under sanitizers (not on MSVC,
# which has none of these). ThreadSanitizer builds the mixer test a second time, for its two-thread test.
function(studio_add_sanitized_test name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/runtime/SceneGame/include")
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra)
        target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
endfunction()
studio_add_sanitized_test(tst_audio_mixer tst_audio_mixer.cpp)
studio_add_sanitized_test(tst_save_format tst_save_format.cpp)
# The 2D platformer physics from the game runtime, always under sanitizers, with a long soak of random worlds.
studio_add_sanitized_test(tst_physics2d tst_physics2d.cpp)
if(NOT MSVC)
    add_executable(tst_audio_mixer_tsan tst_audio_mixer.cpp)
    target_compile_features(tst_audio_mixer_tsan PRIVATE cxx_std_20)
    target_include_directories(tst_audio_mixer_tsan PRIVATE "${PROJECT_SOURCE_DIR}/runtime/SceneGame/include")
    target_compile_options(tst_audio_mixer_tsan PRIVATE -O1 -g -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(tst_audio_mixer_tsan PRIVATE -fsanitize=thread)
    add_test(NAME tst_audio_mixer_tsan COMMAND tst_audio_mixer_tsan)
    set_target_properties(tst_audio_mixer_tsan PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

# WAV parsing, conversion and code generation. Sounds.cpp is compiled in here as well as in
# studio_core so that this copy of it, which is the code under test, runs under sanitizers.
qt_add_executable(tst_sounds tst_sounds.cpp "${PROJECT_SOURCE_DIR}/src/core/Sounds.cpp")
target_link_libraries(tst_sounds PRIVATE studio_core Qt6::Test)
if(NOT MSVC)
    target_compile_options(tst_sounds PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(tst_sounds PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME tst_sounds COMMAND tst_sounds)

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
set_target_properties(tst_particles tst_core tst_scene tst_scene_document tst_gamepage tst_blocks tst_logic_document tst_scene_export tst_font_tables tst_sounds tst_world2d tst_sprites_tilemap PROPERTIES WIN32_EXECUTABLE FALSE)
