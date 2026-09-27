# Installation

opane is a CMake project. You can fetch it from GitHub while your project
configures, keep a copy inside your project, or install it once and find it
with `find_package`.

## Requirements

- CMake 3.22 or later.
- A C++20 compiler. This release was tested with MSVC from the Visual Studio
  2026 Build Tools.
- A shader compiler for each graphics API you build for:

| Backend | Shader format | Compiler | Built on |
|---|---|---|---|
| Direct3D 12 | DXIL | `dxc` from the Windows SDK | Windows |
| Vulkan | SPIR-V | `dxc` from the [Vulkan SDK](https://vulkan.lunarg.com) | Windows, Linux |
| Metal | MSL | the Vulkan SDK's `dxc` and `spirv-cross` | macOS |

CMake finds the compilers by itself and prints the formats it will build, for
example `opane shader formats: DXIL;SPIRV`. On Windows without the Vulkan SDK,
it says so and builds for Direct3D 12 only.

## Adding it to your project

Fetch it from GitHub and link it:

```cmake
include(FetchContent)
FetchContent_Declare(opane
    GIT_REPOSITORY https://github.com/cresmarmat-an/opane.git
    GIT_TAG v0.0.1)
FetchContent_MakeAvailable(opane)

target_link_libraries(my_app PRIVATE opane::opane)
```

To use a copy of your own instead, replace the three `FetchContent` lines with
`add_subdirectory(path/to/opane)`. You can also keep the `FetchContent` lines
and point them at a local copy while you work:

```bash
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_OPANE=path/to/opane
```

SDL3 is fetched along with opane and built as a shared library, so copy its DLL
next to your executable after each build:

```cmake
add_custom_command(TARGET my_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:my_app>" "$<TARGET_FILE_DIR:my_app>"
    COMMAND_EXPAND_LISTS)
```

## Installing it once

When opane is the top-level project, it installs itself, SDL3, and a CMake
package. Build both configurations and install them into the same prefix;
debug libraries have a `d` suffix, so the two sit side by side:

```bash
cmake -S path/to/opane -B build-opane
```

```bash
cmake --build build-opane --config Release
```

```bash
cmake --build build-opane --config Debug
```

```bash
cmake --install build-opane --config Debug --prefix C:/sdk
```

```bash
cmake --install build-opane --config Release --prefix C:/sdk
```

Then, in your project, with `C:/sdk` on `CMAKE_PREFIX_PATH`:

```cmake
find_package(opane 0.0.1 REQUIRED)
target_link_libraries(my_app PRIVATE opane::opane)
```

The same `$<TARGET_RUNTIME_DLLS:my_app>` step copies `SDL3.dll` beside your
program. Running `cpack -G ZIP -C "Debug;Release"` in the build directory
makes a zip of the same install. The zips attached to each
[GitHub release](https://github.com/cresmarmat-an/opane/releases) are made
this way, for Windows x64.

When opane is a subdirectory of your project, it installs nothing unless you
set `OPANE_INSTALL=ON`.

## Build options

| Option | Default | Meaning |
|---|---|---|
| `OPANE_INSTALL` | on when top-level | Generate install rules and the CMake package. |
| `OPANE_SHADER_FORMATS` | empty | Build exactly these formats, from `DXIL`, `SPIRV`, and `MSL`. Every format named must build. |
| `OPANE_DXC` | found | The `dxc` that makes DXIL. It must have `dxil.dll` beside it, as the Windows SDK's does. |
| `OPANE_DXC_SPIRV` | found | The `dxc` that makes SPIR-V, usually the Vulkan SDK's. |
| `OPANE_SPIRV_CROSS` | found | `spirv-cross`, which turns SPIR-V into MSL for Metal. |

## What gets fetched

opane fetches SDL3 `release-3.4.16`, stb (`stb_truetype` and `stb_image`, at a
pinned commit), and miniaudio `0.11.25`. Their licenses are reproduced in
[`THIRD_PARTY_NOTICES.md`](https://github.com/cresmarmat-an/opane/blob/main/THIRD_PARTY_NOTICES.md);
ship that file with your program.

Shaders are compiled and embedded into the library when it builds, so a
program built with opane has no shader files to ship and no shader compiler
at runtime. The one exception is a material you load from an `.hlsl` file while
developing; see [Custom shaders](../extending/custom-shaders.md).

## Checking the version

`<opane/version.h>` is generated from the version in `CMakeLists.txt` and
included by `<opane/opane.h>`:

```cpp
std::printf("opane %s\n", opane::VersionString);   // also VersionMajor, VersionMinor, VersionPatch

#if OPANE_VERSION_MAJOR == 0
// ...
#endif
```

What changed between versions is in
[the changelog](https://github.com/cresmarmat-an/opane/blob/main/CHANGELOG.md).

## Limitations

- Only CMake is supported. There is no package for vcpkg, Conan, or other
  package managers yet.
- SDL3 is always built as a shared library, so `SDL3.dll` (or `libSDL3.so`,
  `libSDL3.dylib`) has to ship beside your program.
- Prebuilt release zips are for Windows x64 only.
