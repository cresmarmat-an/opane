# Diagnostics and escape hatches

opane reports problems through a log instead of exceptions or crashes, and it
gives you the SDL objects underneath when you need something it does not
cover.

## The log

```cpp
opane::SetLogHandler([](opane::LogLevel level, const char* category, const char* message) {
    // send it to your own console or log file
});

opane::SetMinimumLogLevel(opane::LogLevel::Warning);

opane::LogMessage(opane::LogLevel::Info, "game", "Loaded %d levels", levelCount);
```

Levels are `Trace`, `Info`, `Warning`, and `Error`. The default minimum is
`Info`. Without a handler, messages go to standard output and standard error.

opane's own messages use these categories:

| Category | Covers |
|---|---|
| `app` | Startup, the window, and shutdown. |
| `gpu` | The device, shaders, and drawing. |
| `text` | Fonts and the glyph atlas. |
| `image` | Loading images. |
| `audio` | The audio engine and sound files. |
| `material` | Compiling and reloading custom shaders. |
| `theme` | Theme files. |
| `ui` | The element tree, such as a refused `MoveTo`. |

Mistakes in how the library is used, such as a missing file, an unknown
uniform name, or a stale handle, are logged and then ignored, so the program
keeps running.

## Escape hatches

The SDL window and GPU device that opane uses are available for anything the
library does not cover:

```cpp
SDL_Window* window = app.GetWindow();
SDL_GPUDevice* device = app.GetGpuDevice();
```

Include SDL's headers yourself to use them. Changing state that opane relies
on, such as destroying the window or changing the swapchain, will break it.

## Limitations

- There is no built-in on-screen console or profiler overlay; route the log to
  your own.
- The log handler is process-wide and is called from the thread that logged,
  which is the main thread for almost everything.
