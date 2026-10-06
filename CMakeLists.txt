cmake_minimum_required(VERSION 3.21)

project(CyberHandStudio VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Quick3D SerialPort Network Svg Test)
qt_standard_project_setup()

# Gamepad support (Controller mode) through SDL2, optional
option(CH_WITH_SDL2 "Enable gamepad input through SDL2" ON)
if (CH_WITH_SDL2)
    find_package(SDL2 CONFIG QUIET)
endif()

# ---- core library: everything that is not QML, shared by app and tests ----
file(GLOB CH_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h)

add_library(chcore STATIC ${CH_CORE_SOURCES})
target_include_directories(chcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chcore PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D Qt6::SerialPort Qt6::Network)
# source tree, for running from the build folder without deploying models/
target_compile_definitions(chcore PUBLIC CH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(chcore PUBLIC
    CH_VERSION="${PROJECT_VERSION}")

if (SDL2_FOUND)
    message(STATUS "SDL2 found: gamepad support enabled")
    target_compile_definitions(chcore PUBLIC CH_HAVE_SDL2=1)
    if (TARGET SDL2::SDL2)
        target_link_libraries(chcore PUBLIC SDL2::SDL2)
    else()
        target_include_directories(chcore PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(chcore PUBLIC ${SDL2_LIBRARIES})
    endif()
else()
    message(STATUS "SDL2 not found: gamepad support disabled")
endif()

# ---- application ----
if (WIN32)
    set(CH_WIN_RC ${CMAKE_CURRENT_SOURCE_DIR}/assets/app.rc)
endif()

qt_add_executable(CyberHandStudio src/main.cpp ${CH_WIN_RC})

file(GLOB_RECURSE CH_QML_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml)

set_source_files_properties(qml/Theme.qml qml/Icons.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(CyberHandStudio
    URI CyberHand
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES ${CH_QML_FILES})

file(GLOB CH_ASSET_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/examples/*)

qt_add_resources(CyberHandStudio "assets"
    PREFIX "/"
    FILES ${CH_ASSET_FILES})

target_link_libraries(CyberHandStudio PRIVATE
    chcore Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D Qt6::Svg)

set_target_properties(CyberHandStudio PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE)

install(TARGETS CyberHandStudio RUNTIME DESTINATION .)

# ---- unit tests ----
option(CH_BUILD_TESTS "Build unit tests" ON)
if (CH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
