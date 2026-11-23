# Pikmin Recomp launcher: the only published artifact. Standalone project (no game dependencies).
cmake_minimum_required(VERSION 3.25)
set(PIKMIN_LAUNCHER_VERSION "0.1.0" CACHE STRING "Launcher version (from the release tag)")
project(PikminRecompLauncher VERSION ${PIKMIN_LAUNCHER_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Gui Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

add_library(launcher_core STATIC
    src/core/rom.cpp src/core/version.cpp src/core/release.cpp src/core/progress.cpp
    src/core/toolchain.cpp src/core/paths.cpp)
target_include_directories(launcher_core PUBLIC src)
target_link_libraries(launcher_core PUBLIC Qt6::Core)
target_compile_definitions(launcher_core PUBLIC PIKMIN_LAUNCHER_VERSION="${PIKMIN_LAUNCHER_VERSION}")

enable_testing()
add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE launcher_core)
add_test(NAME core_test COMMAND core_test)
set_tests_properties(core_test PROPERTIES SKIP_RETURN_CODE 77)
