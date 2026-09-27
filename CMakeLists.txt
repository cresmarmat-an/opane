cmake_minimum_required(VERSION 3.22)

project(opane
    VERSION 0.0.1
    DESCRIPTION "Application, window, input, and UI library"
    HOMEPAGE_URL "https://cresmarmat-an.github.io/opane/"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Installs opane, SDL3, and a package find_package(opane) reads. On by default
# only when opane is the project being built, not when it is a subdirectory of
# yours.
option(OPANE_INSTALL "Generate opane's install rules and package" ${PROJECT_IS_TOP_LEVEL})

# ---------------------------------------------------------------------------
# Dependencies
#
# SDL3 is fetched rather than vendored so the pinned tag is visible in one
# place. It is built as a shared library because that is SDL's best-supported
# configuration; consumers copy the runtime DLL with $<TARGET_RUNTIME_DLLS:...>.
# ---------------------------------------------------------------------------

include(FetchContent)

# Every clone is made with long paths allowed. miniaudio's repository holds
# test files nested deep enough that, under a build directory of ordinary
# depth, Git for Windows would refuse to check them out without it.

set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL ${OPANE_INSTALL} CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    GIT_CONFIG core.longpaths=true)

FetchContent_MakeAvailable(SDL3)

# stb_truetype rasterizes glyphs and stb_image decodes images. The repository
# has no CMake project, so FetchContent only downloads it and the headers are
# added to the include path.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
    GIT_CONFIG core.longpaths=true)

FetchContent_MakeAvailable(stb)

# miniaudio is also a single header with no CMake project of its own.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    GIT_CONFIG core.longpaths=true
    # Only the header is used. miniaudio's repository has a CMake project of
    # its own, which would build and install libraries nothing links; naming a
    # folder with no CMakeLists.txt here downloads it without adding that.
    SOURCE_SUBDIR header-only)

FetchContent_MakeAvailable(miniaudio)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

add_library(opane STATIC
    src/App.cpp
    src/Assets.cpp
    src/Audio.cpp
    src/Backend.cpp
    src/Containers.cpp
    src/Controls.cpp
    src/Dock.cpp
    src/DrawList.cpp
    src/Images.cpp
    src/Input.cpp
    src/Lists.cpp
    src/Log.cpp
    src/Materials.cpp
    src/Popups.cpp
    src/RenderTargets.cpp
    src/Renderer.cpp
    src/Styling.cpp
    src/ThemeFile.cpp
    src/Json.cpp
    src/Text.cpp
    src/TextArea.cpp
    src/Ui.cpp
    src/Widgets.cpp)

add_library(opane::opane ALIAS opane)

# Shaders are compiled to bytecode at build time and embedded, so a program
# built against opane has no runtime file dependency and cannot drift out of
# sync with the shaders it was compiled against.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake)

opane_add_shader(opane ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.hlsl
    VertexMain vs_6_0 OpaneUiVertex ${CMAKE_CURRENT_SOURCE_DIR}/shaders/material.hlsli)
opane_add_shader(opane ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.hlsl
    FragmentMain ps_6_0 OpaneUiFragment ${CMAKE_CURRENT_SOURCE_DIR}/shaders/material.hlsli)

# The blur behind frosted glass.
opane_add_shader(opane ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_blur.hlsl BlurVertexMain vs_6_0 OpaneUiBlurVertex)
opane_add_shader(opane ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui_blur.hlsl BlurFragmentMain ps_6_0 OpaneUiBlurFragment)

# A material created from an .hlsl file is compiled at runtime, to whichever
# format the device runs. The include it reads travels inside the library, so
# it always matches the build that reads its output; the compilers are found
# when first needed, starting with the ones this build used. Materials built
# from bytecode need neither, which is the path a shipped program takes.
opane_embed_file(opane ${CMAKE_CURRENT_SOURCE_DIR}/shaders/material.hlsli OpaneMaterialInclude)
target_compile_definitions(opane PRIVATE
    OPANE_DXC_PATH="${OPANE_DXC}"
    OPANE_DXC_SPIRV_PATH="${OPANE_DXC_SPIRV}"
    OPANE_SPIRV_CROSS_PATH="${OPANE_SPIRV_CROSS}")

# Debug and release builds install side by side.
set_target_properties(opane PROPERTIES DEBUG_POSTFIX d)

# <opane/version.h>, from the version above, so the two cannot disagree.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/include/opane/version.h @ONLY)

target_include_directories(opane
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
        ${miniaudio_SOURCE_DIR})

# SDL is PUBLIC because the escape hatches on App hand back SDL_Window* and
# SDL_GPUDevice*, which a consumer needs SDL's headers to use.
target_link_libraries(opane PUBLIC SDL3::SDL3)

target_compile_features(opane PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(opane PRIVATE /W4 /permissive-)
else()
    target_compile_options(opane PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# App icons
#
# opane_set_app_icon gives a program's .exe, app bundle, and window an icon of
# its own. opane_icon is the build-time tool that turns a picture into the
# icon files; nothing links it. It is built only when something needs it,
# unless it is installed.
# ---------------------------------------------------------------------------

if(OPANE_INSTALL)
    add_executable(opane_icon tools/opane_icon.cpp)
else()
    add_executable(opane_icon EXCLUDE_FROM_ALL tools/opane_icon.cpp)
endif()
target_include_directories(opane_icon PRIVATE ${stb_SOURCE_DIR})
target_compile_features(opane_icon PRIVATE cxx_std_20)
if(MSVC)
    target_compile_definitions(opane_icon PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppIcon.cmake)

# ---------------------------------------------------------------------------
# Installation
#
# cmake --install puts opane, SDL3, and a package in the prefix, so a project
# elsewhere can use find_package(opane). Install the Debug and the Release
# configurations into the same prefix: the libraries carry a d suffix in Debug.
# ---------------------------------------------------------------------------

if(OPANE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(OpanePackageDir "${CMAKE_INSTALL_LIBDIR}/cmake/opane")

    install(TARGETS opane opane_icon
        EXPORT opaneTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/opane
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/include/opane/version.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/opane)

    install(EXPORT opaneTargets
        NAMESPACE opane::
        DESTINATION ${OpanePackageDir})

    configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/opaneConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/opaneConfig.cmake
        INSTALL_DESTINATION ${OpanePackageDir})

    # Before 1.0, a minor version may change the interface, so only the same
    # major and minor version is taken as compatible.
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/opaneConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)

    install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/opaneConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/opaneConfigVersion.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppIcon.cmake
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinary.cmake
        DESTINATION ${OpanePackageDir})

    # What opane_add_material compiles against.
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/material.hlsli
        DESTINATION ${CMAKE_INSTALL_DATADIR}/opane/shaders)

    install(FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/README.md
            ${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md
            ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
            ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

    # cpack -G ZIP -C "Debug;Release" makes the release archive.
    if(PROJECT_IS_TOP_LEVEL)
        set(CPACK_PACKAGE_NAME opane)
        set(CPACK_PACKAGE_VENDOR "Cresmar Mat-an")
        set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
        set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
        set(CPACK_PACKAGE_FILE_NAME "opane-${PROJECT_VERSION}-windows-x64")
        set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
        set(CPACK_GENERATOR ZIP)
        include(CPack)
    endif()
endif()
