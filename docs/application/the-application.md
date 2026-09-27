# The application

`StartApp` initializes SDL, creates the window and the GPU device, starts the
audio engine, finds a system font, and returns the application. Always check
`IsValid()`: when something fails, the returned `App` is invalid and the reason
has already been written to the log.

```cpp
opane::App app = opane::StartApp({
    .Title = "My Application",
    .Icon = "",           // the window's icon; see Your program's icon
    .Width = 1280,        // in interface units
    .Height = 720,
    .Resizable = true,
    .VSync = true,        // wait for the display's refresh
    .HighDpi = true,      // use the display's full resolution
    .DebugGpu = false,    // turn on the graphics API's validation layer
    .Backend = opane::GraphicsBackend::Automatic,   // see Graphics backends
    .UiScale = 0.0f,      // 0 follows the display; see Interface scale
    .Hidden = false,      // true for automated tests and offscreen tools
    .Borderless = false,  // true for a window that draws its own title bar
    .ResizeBorder = 6.0f, // how close to a borderless window's edge resizes it
    .ClearColor = opane::Color::FromBytes(18, 20, 26),
});
if (!app.IsValid())
{
    return 1;
}
```

Every field has a sensible default, so `opane::StartApp()` with no arguments
also works. `Borderless` and `ResizeBorder` are covered in
[A window of your own](../styling/custom-windows.md).

`App` is a lightweight handle. Copying it copies the handle, not the
application, so you can pass it around freely. There can only be one
application per process: a second `StartApp` logs an error and returns an
invalid `App`.

## The frame loop

`Run` is the simplest way to drive the program:

```cpp
app.Run([&](float deltaSeconds) {
    // Per-frame logic and painting go here.
});

app.Shutdown();
```

`deltaSeconds` is the time since the previous frame. Your function runs before
the frame is drawn, so anything it changes shows up in the same frame. After
it, the element tree is laid out, updated, and painted, which is why widgets
always appear on top of whatever your function painted. `Run` returns when the
window is closed or `Close()` is called.

When the interface and a hosted world are the whole program, you can call
`Run()` with no function at all.

## Writing the loop yourself

`Run` makes these calls, in this order:

```cpp
while (app.IsOpen())
{
    const float deltaSeconds = app.PollEvents();

    // Your logic and painting.

    app.UpdateInterface(deltaSeconds);

    app.BeginFrame();
    app.EndFrame();
}
```

- `PollEvents` handles window and input events, passes them to the element
  tree, clears the frame's draw list, and returns the time since the last call.
  The time is clamped to a quarter of a second, so pausing in a debugger does
  not produce one huge step.
- `UpdateInterface` lays out, updates, and paints the element tree.
- `BeginFrame` acquires the swapchain image and records everything painted
  since `PollEvents`; `EndFrame` submits the frame to the GPU.

Paint between `PollEvents` and `BeginFrame`, using `app.GetDrawList()`. See
[Drawing directly](../extending/drawing-directly.md).

## Closing and shutting down

`Close()` asks the loop to stop; `IsOpen()` becomes false. `Shutdown()`
releases everything: the element tree, textures, fonts, sounds, the device,
and the window. Call it once, after the loop. It is safe to call while a
hosted world still exists; see [Hosting a world](hosting-a-world.md).

## Other calls

```cpp
app.SetTitle("Untitled - My Application");
app.SetIcon("icon.png");            // see Your program's icon
app.SetClearColor(opane::Color::FromBytes(20, 22, 28));
app.GetClearColor();

app.GetWindowSize();                // in interface units
app.GetPixelSize();                 // in pixels
app.GetTimeSeconds();               // seconds since StartApp
app.GetFrameCount();

app.GetClipboardText();             // UTF-8, empty when the clipboard holds no text
app.SetClipboardText("Copied");

app.GetGraphicsBackend();           // Direct3D12, Vulkan, or Metal
app.GetRoot();                      // the element that fills the window
app.GetOverlay();                   // the layer for popups and floating windows
app.GetFocusedElement();            // the element that has the keyboard, or null
```

## Limitations

- One window per application, and one application per process. There is no
  support for opening a second window; floating panels such as
  [Windows](../interface/containers.md) and
  [docked panels](../interface/docking.md) live inside the main window.
- The frame loop runs on the thread that called `StartApp`. The element tree
  and the draw list are not thread-safe, so touch them only from that thread.
- The clipboard holds text only. Images and files cannot be copied or pasted
  through opane.
- There is no support for files dragged onto the window.
