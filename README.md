<p align="center">
  <img src="Icon.png" alt="" width="128" />
</p>

<h1 align="center">opane</h1>

<p align="center">
  Application, window, input, and interface library for C++20, built on SDL3.
</p>

<p align="center">
  <a href="https://cresmarmat-an.github.io/opane/"><strong>Documentation</strong></a> ·
  <a href="https://cresmarmat-an.github.io/opane/getting-started/installation/">Installation</a> ·
  <a href="https://github.com/cresmarmat-an/opane-examples">Examples</a> ·
  <a href="CHANGELOG.md">Changelog</a>
</p>

<p align="center">
  <img alt="Version 0.0.1" src="https://img.shields.io/badge/version-0.0.1-0466EF" />
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-132A4C" />
  <img alt="Direct3D 12, Vulkan, and Metal" src="https://img.shields.io/badge/backends-Direct3D%2012%20%7C%20Vulkan%20%7C%20Metal-0466EF" />
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-132A4C" /></a>
</p>

opane opens a window on Direct3D 12, Vulkan, or Metal, runs the frame loop, and
gives you an element tree with layout, events, theming, and a set of standard
widgets. You can restyle every widget or draw your own. It does not depend on
[ludifex](https://github.com/cresmarmat-an/ludifex): use it alone for an
interface application, or with ludifex when you also need a 2D or 3D world.

```cpp
#include <opane/opane.h>

int main()
{
    opane::App app = opane::StartApp({ .Title = "Hello", .Width = 1280, .Height = 720 });
    if (!app.IsValid())
    {
        return 1;
    }

    opane::Button* button = app.GetRoot()->Add<opane::Button>();
    button->Text = "Click me";
    button->PlaceCentered();
    button->OnClick = [] { /* ... */ };

    app.Run([&](float deltaSeconds) {
        if (app.GetInput().WasKeyPressed(opane::Key::Escape))
        {
            app.Close();
        }
    });

    app.Shutdown();
}
```

## Getting it

Fetch it from GitHub and link it:

```cmake
include(FetchContent)
FetchContent_Declare(opane
    GIT_REPOSITORY https://github.com/cresmarmat-an/opane.git
    GIT_TAG v0.0.1)
FetchContent_MakeAvailable(opane)

target_link_libraries(my_app PRIVATE opane::opane)
```

SDL3 is fetched with it and built as a shared library; copy its DLL beside your
program:

```cmake
add_custom_command(TARGET my_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:my_app>" "$<TARGET_FILE_DIR:my_app>"
    COMMAND_EXPAND_LISTS)
```

It needs CMake 3.22 or later, a C++20 compiler, and a shader compiler for each
backend you build: `dxc` from the Windows SDK for Direct3D 12, and the
[Vulkan SDK](https://vulkan.lunarg.com) for Vulkan. [Installation](https://cresmarmat-an.github.io/opane/getting-started/installation/)
covers installing it once and using `find_package`, and every build option.

## Documentation

The documentation lives at
**[cresmarmat-an.github.io/opane](https://cresmarmat-an.github.io/opane/)**.
Its pages are the Markdown files in [`docs/`](docs), organized by what you
want to do:

| | |
|---|---|
| [Getting started](https://cresmarmat-an.github.io/opane/getting-started/introduction/) | Installation, and how opane finds your images, fonts, and sounds |
| [The application](https://cresmarmat-an.github.io/opane/application/the-application/) | The window and frame loop, graphics backends, input, textures, audio, your program's icon, and hosting a world |
| [Building an interface](https://cresmarmat-an.github.io/opane/interface/element-tree/) | The element tree, layout, widgets, focus, menus and dialogs, docking, lists and trees, text, and animation |
| [Styling](https://cresmarmat-an.github.io/opane/styling/theming/) | Themes, a look of your own, skins and theme files, and windows with your own title bar |
| [Extending opane](https://cresmarmat-an.github.io/opane/extending/drawing-directly/) | Drawing directly, writing custom elements, and custom shaders |
| [Reference](https://cresmarmat-an.github.io/opane/reference/status/) | Capabilities and limitations, the license, the changelog, and third-party notices |

## Examples

- [opane-examples](https://github.com/cresmarmat-an/opane-examples): interfaces, text, a custom look, and the interface checks
- [opane-ludifex-examples](https://github.com/cresmarmat-an/opane-ludifex-examples): opane hosting ludifex worlds, from a falling box to the graphics showcase

## Status

0.0.1 is the first release. It has been tested on Windows with Direct3D 12 and
Vulkan. Linux and macOS builds are supported but have not been tested on real
hardware yet.
[Capabilities and limitations](https://cresmarmat-an.github.io/opane/reference/status/)
lists what opane can and cannot do.

## License

opane is released under the [MIT License](LICENSE). Copyright (c) 2026 Cresmar
Mat-an.

A program built with opane also contains SDL3, stb, and miniaudio, each under
its own permissive license; [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
reproduces them all.
