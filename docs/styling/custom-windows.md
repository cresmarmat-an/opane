# A window of your own

A borderless window has no frame from the operating system, so the interface
can draw its own title bar, buttons, and edges. This is how many modern
editors and launchers look.

```cpp
opane::App app = opane::StartApp({ .Title = "Studio", .Borderless = true });

titleBar->Region = opane::WindowRegion::Drag;     // drags the window; a double-click maximizes it

closeButton->OnClick = [&] { app.Close(); };
maximizeButton->OnClick = [&] {
    app.IsWindowMaximized() ? app.RestoreWindow() : app.MaximizeWindow();
};
minimizeButton->OnClick = [&] { app.MinimizeWindow(); };
```

## Window regions

An element's `Region` says what pressing on it does to the window:

| `WindowRegion` | Pressing on the element |
|---|---|
| `None` (default) | nothing special |
| `Drag` | moves the window; a double-click maximizes or restores it |
| `ResizeTop`, `ResizeBottom`, `ResizeLeft`, `ResizeRight` | resizes from that edge |
| `ResizeTopLeft`, `ResizeTopRight`, `ResizeBottomLeft`, `ResizeBottomRight` | resizes from that corner |

A region applies to the element and everything inside it that has no region
of its own. Inside a drag region, anything focusable (a button, a field) stays
a normal control, so the title bar's buttons still work.

The window's edges also resize it: pressing within `AppConfig::ResizeBorder`
units of an edge resizes the window while it is not maximized.

## Limitations

- opane does not provide a ready-made title bar. Build one from elements, as
  above; the
  [custom UI example](https://github.com/cresmarmat-an/opane-examples/tree/main/03-custom-ui)
  has a complete one.
- The window's shape is always a rectangle; it cannot be transparent or
  non-rectangular.
