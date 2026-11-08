qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE studio_core Qt6::Test)
target_compile_definitions(tst_core PRIVATE STUDIO_TEST_CMAKE="${CMAKE_COMMAND}")
add_test(NAME tst_core COMMAND tst_core)
