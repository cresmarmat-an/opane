# Changelog

All notable changes to opane. The version is set in `CMakeLists.txt`, and
`opane/version.h` makes it available to a program at compile time.

## 0.0.1

The first release.

### Application

- `StartApp` and `App`: the window, the GPU device and swapchain, the frame
  loop, and an orderly shutdown.
- Direct3D 12, Vulkan, and Metal through SDL3's GPU API, chosen automatically
  or by name with `AppConfig::Backend`. `IsGraphicsBackendAvailable`,
  `GetGraphicsBackendName`, and `App::GetGraphicsBackend` help build a settings
  screen.
- Input: keys, mouse buttons, the wheel, typed text, and modifier keys, with
  the pointer in interface units on any display.
- An interface scale that follows the display or is set by hand
  (`AppConfig::UiScale`, `App::SetUiScale`). Text is rasterized at the real
  size, so it stays sharp at every scale.
- Hidden windows (`AppConfig::Hidden`) for automated tests.
- Files found through ordered asset roots.
- The program's icon: `opane_set_app_icon` embeds it in the Windows executable
  and the macOS app bundle, and `AppConfig::Icon` and `App::SetIcon` set the
  window's icon at runtime.

### Drawing

- Rectangles, rounded rectangles, circles, lines, and borders, anti-aliased in
  the shader, batched, and clipped.
- Text: UTF-8, TrueType fonts, a glyph atlas that grows as needed, kerning,
  mixed fonts and colours in one line, and word wrapping.
- Images with mip maps and anisotropic filtering.
- Fills of a colour, linear and radial gradients, or an image fitted six ways
  (stretch, contain, cover, center, tile, nine-slice); soft shadows, inset
  shadows, and glows; frosted glass; per-corner radii; and transforms and
  opacity (`DrawBox`, `DrawFill`, `DrawShadow`, `DrawImage`, `BackdropBlur`,
  `PushTransform`, `PushOpacity`).
- Custom HLSL materials with hot reload and animated uniforms, and
  post-process chains over any element and its children.

### The element tree

- Layout with scale-plus-offset sizes, stacks, flexible sizes (`Flex`,
  `MinSize`), and custom layouts (`LayoutMode::Custom`).
- Events that bubble up the tree, pointer capture, keyboard focus with Tab and
  Shift+Tab, focus rings shown only for keyboard use, and shortcuts.
- Moving elements (`MoveTo`, `BringToFront`), and removal queued to the end of
  the frame so an element can remove itself from its own handler.
- An overlay layer for popups and tooltips.
- Pointer shapes and tooltips.
- `Element::Appearance`: a look for each state (normal, hovered, pressed,
  focused, selected, disabled) with smooth transitions; `Transform` and
  `Opacity` on any element, with hit testing that follows the transform;
  `Enabled`; `SetSelected`; and `Painter` to draw an element yourself.
- Light and dark themes, skins for every built-in widget in `Theme::Styles`,
  named looks for your own elements (`StyleName`), and JSON theme files that
  reload while the program runs (`App::LoadTheme`).
- Windows without a system frame (`AppConfig::Borderless`), with your own
  title bar: `WindowRegion` drag and resize regions, and `MinimizeWindow`,
  `MaximizeWindow`, and `RestoreWindow`.
- `ElementRef` refers to an element without owning it and returns null once
  the element is removed.

### Widgets

- Panel, Label, Button, Checkbox, Slider, TextInput (with selection, word
  movement, and undo and redo), ScrollView, Image, and Viewport.
- ProgressBar, RadioButton, Toggle, and NumberField.
- Popup, Menu with submenus and checked items, MenuBar with shortcuts,
  Dropdown, Dialog, context menus (`App::ShowMenu`), and message dialogs
  (`App::ShowDialog`).
- Splitter, TabView, and Window.
- DockSpace: panels in tabs and splits, dragged between groups, floated and
  docked again, closed and reopened, with layouts saved to text.
- ListView and TreeView, which only create rows that are visible.
- TextArea for multi-line editing with wrapping and undo.

### Audio

- Sounds and music with volume groups, positional sound with a listener, and
  one shared mix with a hosted world, so the process opens one audio device.

### Hosting a world

- A world from ludifex or another library can find opane's device, window, and
  frame loop through SDL's shared properties, and draw full-window
  (`SetMainWorld`), inside a layout (`Viewport::SetWorld`), or from your own
  loop (`RenderWorld`).

### Building and installing

- Installs itself, SDL3, and a CMake package for `find_package(opane)`, with
  Debug and Release libraries side by side.
- Shaders are written in HLSL and compiled at build time to DXIL, SPIR-V, and
  MSL as the platform needs.
- `opane_add_material` compiles a material into a program at build time, so a
  shipped program needs no shader compiler and no `.hlsl` files.
- `opane/version.h` reports the version at compile time.
- MIT licensed. The dependencies' licenses are in `THIRD_PARTY_NOTICES.md`.

### Known limitations

- Scripts that need text shaping (Arabic, Devanagari, and similar) are drawn
  character by character, and right-to-left text is not reordered.
- Tested on Windows only. Linux and macOS builds are supported but have not
  been run on real hardware.
