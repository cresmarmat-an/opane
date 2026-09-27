# Capabilities and limitations

This page lists what opane 0.0.1 can do and what it cannot, in one place. Each
topic page has a Limitations section with more detail.

## What opane can do

| Area | Capabilities |
|---|---|
| Application | One window with a frame loop, VSync, high-DPI support, hidden windows for tests, borderless windows with your own title bar, the program's icon, clipboard text. See [The application](../application/the-application.md). |
| Graphics | Direct3D 12, Vulkan, and Metal through SDL3's GPU API, chosen automatically or by name. See [Graphics backends](../application/graphics-backends.md). |
| Input | Keyboard (by key position), three mouse buttons, the wheel, typed text through the platform's text input, and events routed through the element tree. See [Input](../application/input.md). |
| Interface scale | Everything in interface units, following the display's scale or one you choose, with text rasterized at the real size. See [Interface scale](../application/interface-scale.md). |
| Drawing | Rectangles, rounded rectangles (a radius per corner), circles, lines, outlines, gradients, images fitted six ways including nine-slice and tiling, soft shadows and glows, frosted glass, clipping, 2D transforms, and opacity, all anti-aliased in the shader and batched. See [Drawing directly](../extending/drawing-directly.md). |
| Textures | PNG, JPEG, BMP, TGA, GIF, PSD, and HDR files with mip maps and anisotropic filtering; textures from pixels; textures from other libraries. See [Textures](../application/textures.md). |
| Text | TrueType fonts, UTF-8, kerning, mixed fonts and colours in one line, word wrapping, sharp text at every size, and letters that custom shaders can outline or colour. See [Text and fonts](../interface/text-and-fonts.md). |
| Layout | Scale-plus-offset sizes and positions, anchors, vertical and horizontal stacks, flexible sizes, minimum sizes, and layouts of your own. See [Layout](../interface/layout.md). |
| Events and focus | Pointer capture, bubbling, keyboard focus with Tab navigation and focus rings, and shortcuts that work anywhere. See [Keyboard focus](../interface/keyboard-focus.md). |
| Widgets | Panel, Label, Button, Checkbox, Toggle, RadioButton, Slider, ProgressBar, NumberField, TextInput, TextArea, ScrollView, Image, Viewport, Popup, Menu, MenuBar, Dropdown, Dialog, Splitter, TabView, Window, DockSpace, ListView, and TreeView, plus tooltips and pointer shapes. See [Built-in widgets](../interface/widgets.md). |
| Docking | Panels in tabs and splits, dragged between groups with guides, torn off into floating windows, closed and reopened, and layouts saved to text. See [Docking](../interface/docking.md). |
| Styling | Light and dark themes, a look per state that blends between states, skins for every widget, named looks, and JSON theme files that reload while the program runs. See [Designing your own look](../styling/your-own-look.md). |
| Custom elements | Your own elements with the same abilities as the built-in widgets, including custom hit testing, layout, and focus. See [Custom elements](../extending/custom-elements.md). |
| Custom shaders | HLSL fragment shaders on any element, with hot reload during development and embedded bytecode for shipping; post-process chains over any subtree; animated uniforms. See [Custom shaders](../extending/custom-shaders.md). |
| Audio | WAV, FLAC, MP3, and Ogg Vorbis; sounds and streamed music; volume groups; positional sound with a listener; one shared mix with a hosted world. See [Audio](../application/audio.md). |
| Hosting a world | A ludifex world or your own renderer, under the interface or inside a viewport, sharing the GPU device and the audio device. See [Hosting a world](../application/hosting-a-world.md). |
| Diagnostics | A log with levels and categories that you can redirect, and access to the SDL window and GPU device. See [Diagnostics](../application/diagnostics.md). |

## What opane does not do

**Platforms**

- This release has been tested on Windows only. Linux (Vulkan) and macOS
  (Metal) builds are supported by the build scripts but have not been run on
  real hardware.
- There is no OpenGL, WebGPU, mobile, or web support.

**Application**

- One window per application, and one application per process.
- No gamepad or joystick input. Touch and pen input arrive only as emulated
  mouse input. Only three mouse buttons and a vertical wheel.
- No dragging files onto the window, and the clipboard holds text only.
- No native file, folder, colour, or font dialogs, and no native menus.

**Text**

- No complex text shaping (Arabic, Devanagari, Thai, and similar scripts are
  drawn incorrectly), no right-to-left text, and no ligatures.
- No font fallback and no colour emoji.
- Line breaking at spaces only.

**Interface**

- No accessibility support such as screen readers.
- Interfaces are built in C++; there is no markup language or visual designer,
  and no data binding.
- `Label` shows one line; lists and trees show one line of text per row.
- No colour picker, date picker, table view, or rich text editor.
- Horizontal sliders and vertical scroll views only.
- Floating windows and docked panels stay inside the main window.

**Drawing and shaders**

- No arbitrary paths, polygons, or curves; no dashed lines.
- Custom shaders are fragment shaders only, with up to eight uniforms and one
  texture.

**Audio**

- One voice per sound, no audio effects, no recording, and no choice of output
  device.

## Design notes that affect how you use it

Elements are the one place opane hands out raw pointers instead of handles.
`Add<T>()` returns a `T*` owned by the tree. Removal is queued to the end of
the frame, so an element can safely be removed from inside its own event
handler, but do not keep an `Element*` after removing it. To keep a reference
to an element that might be removed by something else, use an
[`ElementRef`](../extending/custom-elements.md#holding-on-to-another-element),
which returns null once the element is gone.
