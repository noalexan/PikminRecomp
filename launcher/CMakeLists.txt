# Pikmin Recomp launcher: the only published artifact. Standalone project (no game dependencies).
cmake_minimum_required(VERSION 3.25)
set(PIKMIN_LAUNCHER_VERSION "0.2.0" CACHE STRING "Launcher version (from the release tag)")
project(PikminRecompLauncher VERSION ${PIKMIN_LAUNCHER_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Gui Quick QuickControls2 QuickEffects)
qt_standard_project_setup(REQUIRES 6.5)

add_library(launcher_core STATIC
    src/core/rom.cpp src/core/version.cpp src/core/release.cpp src/core/progress.cpp
    src/core/toolchain.cpp src/core/paths.cpp src/core/update.cpp)
target_include_directories(launcher_core PUBLIC src)
target_link_libraries(launcher_core PUBLIC Qt6::Core)
target_compile_definitions(launcher_core PUBLIC PIKMIN_LAUNCHER_VERSION="${PIKMIN_LAUNCHER_VERSION}")

enable_testing()
add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE launcher_core)
add_test(NAME core_test COMMAND core_test)
set_tests_properties(core_test PROPERTIES SKIP_RETURN_CODE 77)

qt_add_executable(pikmin-launcher src/main.cpp src/app/launcher.cpp)
target_link_libraries(pikmin-launcher PRIVATE launcher_core Qt6::Network Qt6::Gui Qt6::Quick Qt6::QuickControls2)
qt_add_resources(pikmin-launcher icon PREFIX / FILES assets/pikminrecomp-launcher-256.png)  # window icon
qt_add_qml_module(pikmin-launcher URI PikminLauncher VERSION 1.0
    QML_FILES qml/Main.qml qml/GlassPanel.qml qml/StatusRow.qml qml/PillButton.qml qml/ForestBackground.qml)
set_target_properties(pikmin-launcher PROPERTIES WIN32_EXECUTABLE ON)
install(TARGETS pikmin-launcher RUNTIME DESTINATION bin)
