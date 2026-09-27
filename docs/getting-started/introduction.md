# Introduction

opane is an application, window, input, and interface library for C++20. It
opens a window, runs the frame loop, reads the keyboard and mouse, and gives you
an element tree with layout, events, theming, and a set of ready-made widgets.
Everything is drawn on the GPU through SDL3, on Direct3D 12, Vulkan, or Metal.

opane does not depend on ludifex. Use it on its own for tools, editors, and
other interface programs, or together with ludifex when you also want a 2D or
3D world on screen.

## A first program

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

Step by step:

1. `StartApp` creates the window and the GPU device. If it fails, `IsValid()`
   is false and the reason is in the log.
2. `GetRoot()` is the element that fills the window. `Add<T>()` creates a child
   of any element type, including your own, and the tree owns it.
3. `PlaceCentered()` sets the button's position and anchor so it stays in the
   middle of its parent when the window is resized.
4. `Run` calls your function once per frame, then lays out and draws the
   element tree on top of anything you painted. It returns when the window
   closes.
5. `Shutdown` releases the window, the device, and the audio engine.

## What opane includes

- **The application:** one window, the frame loop, keyboard and mouse input,
  the clipboard, textures, audio with volume groups and positional sound, and
  your program's own icon.
- **Drawing:** a draw list of rectangles, rounded rectangles, circles, lines,
  and borders drawn from signed distance fields, with gradients, images, soft
  shadows, glows, frosted glass, clipping, transforms, and opacity.
- **Text:** TrueType fonts, UTF-8, kerning, mixed fonts and colours on one line,
  word wrapping, and text that stays sharp at any size and interface scale.
- **The element tree:** layout with stacks, flexible sizes, and custom layouts;
  events with pointer capture, keyboard focus, Tab navigation, and shortcuts.
- **Widgets:** labels, buttons, checkboxes, radio buttons, toggles, sliders,
  progress bars, number fields, text fields, multi-line text areas, images,
  scroll views, lists, trees, drop-downs, menus, menu bars, popups, dialogs,
  tooltips, splitters, tab views, windows, and a dock space.
- **Styling:** light and dark themes, a look per state (hovered, pressed,
  focused, selected, disabled) that eases between states, skins for every
  built-in widget, JSON theme files that reload while the program runs, and
  frameless windows with a title bar you draw yourself.
- **Extending:** your own elements with the same abilities as the built-in
  ones, custom HLSL shaders on any element, and post-process chains over any
  part of the interface.
- **Hosting a world:** a ludifex world, or a renderer of your own, drawn under
  the interface or inside a viewport in your layout.

## Platforms

The shaders are written once in HLSL and compiled for each graphics API when
opane builds:

| Platform | Backends |
|---|---|
| Windows | Direct3D 12, Vulkan |
| Linux | Vulkan |
| macOS | Metal |

This release has been tested on Windows with Direct3D 12 and Vulkan. The build
supports Linux and macOS, but they have not been tested on real hardware yet.

## Where to go next

- [Installation](installation.md): add opane to a CMake project.
- [The application](../application/the-application.md): the window and the
  frame loop.
- [The element tree](../interface/element-tree.md) and
  [Layout](../interface/layout.md): building an interface.
- [Capabilities and limitations](../reference/status.md): what opane does and
  does not do.
