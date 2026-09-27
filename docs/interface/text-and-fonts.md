# Text and fonts

A system font is found at startup, so text works before you load anything:

```cpp
opane::FontId font = app.GetDefaultFont();
```

## Loading fonts

Load a font file at a size. Loading the same file at another size gives a
second `FontId` that shares the file with the first:

```cpp
opane::FontId heading = app.LoadFont("fonts/Inter-Bold.ttf", 28.0f);
opane::FontId body    = app.LoadFont("fonts/Inter-Regular.ttf", 15.0f);
```

Fonts are found through the [asset roots](../getting-started/finding-files.md).
Sizes are in interface units, and text is rasterized at the size it covers on
screen, so it stays sharp at every [interface scale](../application/interface-scale.md).

To use a font across the interface, set it on the theme (`Theme::Font`) or in a
[theme file](../styling/skins-and-theme-files.md). A `Label` can also have a
font of its own.

## Measuring and drawing

```cpp
opane::Vec2 size = app.MeasureText(font, "Hello");
float lineHeight = app.GetFontLineHeight(font);

drawList.DrawText("Left aligned", { 20, 20 }, font, color);
drawList.DrawText("Centered", { 400, 20 }, font, color, opane::TextAlign::Center);
drawList.DrawTextInRect("Vertically centred", bounds, font, color, opane::TextAlign::Center);
```

The position given to `DrawText` is the **top** of the line, not the baseline,
so text lands where you expect without looking up font metrics. Inside an
element, `MeasureText` and `GetLineHeight` measure with the theme's font or
one you pass.

## Mixed styles and wrapping

```cpp
drawList.DrawRichText({
    { "A line " },
    { "of its own", heading, opane::Color::FromBytes(255, 196, 92) },
    { " sizes and colours." },
}, { 20, 20 }, font, color);

float used = drawList.DrawTextWrapped(paragraph, bounds, font, color);
opane::Vec2 needed = app.MeasureTextWrapped(font, paragraph, bounds.Width);
opane::Vec2 lineSize = app.MeasureRichText(runs, font);
```

Each `TextRun` has its own text, and optionally its own font and colour; an
invalid font or a transparent colour uses the ones passed to the call. Runs
share a baseline, so a larger word sits on the same line as its neighbours.

Wrapped text breaks at spaces, and inside a word only when the word is wider
than the whole line. `DrawTextWrapped` returns the height of what it drew, and
`MeasureTextWrapped` gives the size without drawing.

## Characters

Text is UTF-8 throughout. Characters are measured, drawn, and counted whole,
so an accented letter counts as one character everywhere. Invalid UTF-8 is
drawn as replacement characters. A character the font does not have is drawn
as the font's missing-glyph box. Kerning from the font is applied.

## How text is rendered

Glyphs are rasterized the first time they are drawn and packed into an atlas
per font file and size, which grows as needed up to 4096 by 4096 pixels. A
screen of text in one font stays in one draw call.

Text up to 26 pixels tall on screen is rasterized as coverage at exactly the
size it is drawn, because thin strokes at small sizes (a hyphen, a comma's
tail) are thinner than one texel of a distance field. Larger text is
rasterized once as a signed distance field and magnified, so a heading stays
sharp at any size.

The difference matters for custom shaders: a distance field tells a shader how
far each pixel is from a letter's edge, which is what outlines, glows, and
gradients inside letters need. A text shader can check which kind it has and
fall back to plain drawing for small text; see
[Custom shaders](../extending/custom-shaders.md).

## Limitations

- **No complex text shaping.** Scripts that need shaping, such as Arabic,
  Devanagari, or Thai, are drawn one character at a time in logical order,
  which is wrong for them. There are no ligatures.
- **No right-to-left text.** Hebrew and Arabic are not reordered for display.
- **No font fallback.** A character missing from the font draws as a box; opane
  does not look for it in another font. There is no colour emoji.
- **No synthetic styles.** Bold and italic need their own font files, and there
  is no underline or strikethrough.
- Line breaking uses spaces only. Languages written without spaces, such as
  Chinese and Japanese, break only where a line overflows.
- Fonts are read with stb_truetype, which handles TrueType and most OpenType
  font files. Variable font axes are not supported.
