# Skins and theme files

## Skinning the built-in widgets

A theme can restyle every widget of one kind at once. `Theme::Styles` holds a
`Style` for each of these:

| Style | Applies to |
|---|---|
| `Button`, `AccentButton` | Buttons, and buttons with `Accent` set. |
| `Panel` | Panels that show the theme's surface. |
| `Window` | Windows. |
| `Popup` | Menus, dialogs, drop-down lists, and your own popups. |
| `TextInput` | Text fields, number fields, and text areas. |
| `Dropdown` | The closed drop-down. |
| `ListView` | Lists and trees. |
| `Tooltip` | Tooltips. |
| `CheckboxBox` | The box of a checkbox. |
| `ToggleTrack`, `ToggleKnob` | The parts of a toggle. |
| `SliderTrack`, `SliderFill`, `SliderThumb` | The parts of a slider. |
| `ProgressTrack`, `ProgressFill` | The parts of a progress bar. |
| `ScrollbarThumb` | Scroll bars in scroll views, lists, trees, and text areas. |

For the parts, the `Selected` state means a checkbox that is checked, a toggle
that is on, or a slider or scroll bar being dragged. A style left empty keeps
the built-in look, and an element's own `Appearance` wins over its theme's
skin.

You can also keep looks of your own in the theme, by name, and use them from
any element:

```cpp
opane::Theme theme = app.GetTheme();
theme.Styles.Button = myButtonStyle;
theme.NamedStyles.push_back({ "card", myCardStyle });
app.SetTheme(theme);

panel->StyleName = "card";
```

An element uses, in order: its own `Appearance`, the named look in its
`StyleName`, then its default from the theme. A custom element can take a
default look from the theme the way the widgets do, by overriding
`GetDefaultAppearance`.

## Theme files

A theme can live in a JSON file. It is loaded on top of the current theme, and
by default it is reloaded whenever the file is saved, so you can restyle a
running program:

```cpp
app.LoadTheme("theme.json");          // reloads on save
app.LoadTheme("theme.json", false);   // loads once
```

`LoadTheme` returns false, with the reason in the log, when the file cannot be
loaded.

```json
{
  "colors": { "background": "#0b0e16", "surface": "#1a2030", "accent": "#7c9cff", "text": "#eef2ff" },
  "cornerRadius": 12,
  "font": { "path": "fonts/Inter.ttf", "size": 16 },
  "widgets": {
    "button": {
      "normal": { "background": { "linear": ["#2b3550", "#1f2740"] }, "radius": 10,
                  "shadows": [ { "color": "#00000066", "offset": [0, 3], "blur": 8 } ] },
      "hovered": { "scale": 1.03, "glow": { "color": "#7c9cff55", "blur": 16 } },
      "pressed": { "scale": 0.97 },
      "transition": 0.14
    },
    "sliderThumb": { "background": "#ffffff", "radius": 10 }
  },
  "styles": {
    "glass": { "background": "#1a2135a8", "backdropBlur": 22, "radius": 16,
               "border": { "color": "#ffffff26", "width": 1 } }
  }
}
```

### Top-level keys

| Key | Holds |
|---|---|
| `colors` | Theme colours: `background`, `surface`, `surfaceHovered`, `surfacePressed`, `accent`, `accentHovered`, `text`, `textMuted`, `border`. |
| `cornerRadius`, `padding`, `spacing`, `borderWidth` | Numbers. |
| `font` | `{ "path": ..., "size": ... }`. |
| `widgets` | Skins, by the names above in camel case: `button`, `accentButton`, `panel`, `window`, `popup`, `textInput`, `dropdown`, `listView`, `tooltip`, `checkboxBox`, `toggleTrack`, `toggleKnob`, `sliderTrack`, `sliderFill`, `sliderThumb`, `progressTrack`, `progressFill`, `scrollbarThumb`. |
| `styles` | Named looks, for `StyleName`. |

A colour is `"#rgb"`, `"#rgba"`, `"#rrggbb"`, `"#rrggbbaa"`, `"white"`,
`"black"`, `"transparent"`, or `[r, g, b, a]` with values from 0 to 1.

### Styles and looks

A style is either a single look, or an object with `normal` and any of
`hovered`, `pressed`, `focused`, `selected`, and `disabled` (each listing only
what differs from `normal`), plus `transition` in seconds and `curve`
(`linear`, `smooth`, `in`, or `out`).

A look can have:

| Key | Value |
|---|---|
| `background` | A colour; or `{ "linear": [...], "angle": ... }`; or `{ "radial": [...], "center": [x, y], "radius": ... }`; or `{ "image": path, "fit": ..., "tint": ..., "slice": ..., "tileScale": ... }`. Gradient stops are colours, or `{ "at": 0.3, "color": ... }`. |
| `border` | A colour, or `{ "color": ..., "width": ... }`. |
| `radius` | One number, or four. |
| `shadows` | A list of `{ "color", "offset", "blur", "spread", "inset" }`. |
| `glow` | A shadow with no offset. |
| `backdropBlur`, `opacity`, `scale` | Numbers. |
| `textColor` | A colour. |
| `offset` | `[x, y]`. |

### Paths and mistakes

Images and fonts named in the file are looked for beside the theme file first,
then through the [asset roots](../getting-started/finding-files.md).

A file with a JSON error keeps the theme that worked and logs the line and
column of the mistake. An unknown key is a warning, so a typo in one look does
not discard the rest. Comments and trailing commas are allowed.

## Limitations

- Theme files set colours, sizes, the font, and looks. They cannot create
  elements, change layout, or attach behaviour.
- Reloading checks the theme file a few times a second, so a save shows up
  after a short delay rather than instantly.
- Only the widget kinds listed above can be skinned from a theme. Your own
  elements use `StyleName` or `GetDefaultAppearance`.
