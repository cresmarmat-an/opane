# Your program's icon

Unless you give it one, a program shows the system's generic icon. Set yours in
CMake:

```cmake
opane_set_app_icon(my_app art/icon.png)
```

The image can be a PNG, JPEG, BMP, TGA, GIF, or PSD. It should be square and at
least 256 pixels across (1024 for a macOS app bundle). An image that is not
square is centred on a transparent square, and one that is too small is scaled
up with a warning. When your program builds, the image is turned into every
size the platform needs:

- **Windows:** the `.exe` carries the icon at every size from 16 to 256
  pixels, so File Explorer, the taskbar, Alt+Tab, and the window's title bar
  all show it.
- **macOS:** an app bundle (`add_executable(my_app MACOSX_BUNDLE ...)`) carries
  it as its `.icns` file, and the window uses it too.
- **Linux:** an executable file has no icon, so the image is built into the
  program and its window shows it.

A ready-made `.ico` (Windows) or `.icns` (macOS) file is used as it is. The
image can also be one your build generates, as the output of an
`add_custom_command` in the same directory.

## Changing the icon while the program runs

The window's icon can be set when the program starts or changed later, for
example to show a badge for unread messages. Both look the image up through
the [asset roots](../getting-started/finding-files.md):

```cpp
opane::App app = opane::StartApp({ .Title = "Mail", .Icon = "icon.png" });

app.SetIcon("icon-unread.png");   // false, with a log message, if it cannot be loaded
```

## Limitations

- `SetIcon` and `AppConfig::Icon` change the window's icon only. The file's
  icon in File Explorer or Finder is the one the build gave it.
- A macOS icon is only used when the program is built as an app bundle.
