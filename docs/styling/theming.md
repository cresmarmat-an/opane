# Theming

A theme is a set of colours, sizes, a font, and looks that every built-in
widget reads. Because it is plain data, you can build one in code, load it
from a file, or swap it while the program runs; the next frame uses it.

```cpp
app.SetTheme(opane::Theme::Light(app.GetDefaultFont()));
app.SetClearColor(app.GetTheme().Background);
```

opane has two built-in themes, `Theme::Dark` (the default) and `Theme::Light`.
Both take the font to use.

## Changing the tokens

Start from a built-in theme and change what you need:

```cpp
opane::Theme theme = opane::Theme::Dark(app.GetDefaultFont());
theme.Accent = opane::Color::FromBytes(220, 90, 140);
theme.AccentHovered = opane::Color::FromBytes(240, 120, 165);
theme.CornerRadius = 14.0f;
theme.Padding = 16.0f;
theme.Spacing = 10.0f;
app.SetTheme(theme);
```

| Token | Used for |
|---|---|
| `Background` | The window background, when you set it as the clear colour. |
| `Surface`, `SurfaceHovered`, `SurfacePressed` | Panels, buttons, fields, and their states. |
| `Accent`, `AccentHovered` | Accent buttons, checked boxes, slider fills, selections, focus rings. |
| `Text`, `TextMuted` | Normal and secondary text. |
| `Border` | Borders and dividers. |
| `CornerRadius` | The rounding of widgets. |
| `Padding` | Space inside widgets. |
| `Spacing` | Space between parts of a widget. |
| `BorderWidth` | The width of widget borders. |
| `Font` | The font widgets use. |

Beyond these tokens, a theme can give every kind of widget a look of its own
(`Theme::Styles`) and hold named looks for your own elements
(`Theme::NamedStyles`). See [Skins and theme files](skins-and-theme-files.md).

## Reading the theme

Inside an element, `GetTheme()` returns the current theme. Custom elements
should take their colours from it, so they follow theme changes like the
built-in widgets do.

## Limitations

- There is one theme per application. Parts of the interface cannot use
  different themes, though any element can have its own
  [look](your-own-look.md).
- Themes are not cascading style sheets: there are no selectors or
  inheritance rules beyond the widget skins and named looks.
- Switching themes changes looks at once; the change is not animated.
