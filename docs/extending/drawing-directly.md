# Drawing directly

You do not have to use the element tree to draw. `app.GetDrawList()` returns
the frame's draw list, and anything recorded on it between `PollEvents` and
`BeginFrame` (for example inside `Run`'s function) is drawn beneath the
interface. The same draw list is passed to every element's `Paint` method and
to `Painter` functions.

Every shape is drawn from a signed distance field and anti-aliased in the
shader, so edges and rounded corners stay sharp at any size and interface
scale. Coordinates are in interface units.

```cpp
opane::DrawList& drawList = app.GetDrawList();

drawList.FillRect({ 10, 10, 200, 40 }, opane::Color::FromBytes(40, 44, 52));
drawList.FillRoundedRect({ 10, 60, 200, 40 }, 12.0f, opane::Color::FromBytes(60, 110, 200));
drawList.FillCircle({ 120, 160 }, 30.0f, opane::Color::FromBytes(230, 160, 80));
drawList.DrawLine({ 10, 220 }, { 210, 240 }, 3.0f, opane::Color{ 1, 1, 1, 1 });

drawList.StrokeRect({ 10, 260, 200, 40 }, 2.0f, opane::Color::FromBytes(255, 255, 255, 60), 8.0f);
drawList.StrokeCircle({ 120, 350 }, 30.0f, 3.0f, opane::Color::FromBytes(120, 210, 140));

drawList.DrawText("Hello", { 10, 400 }, app.GetDefaultFont(), opane::Color{ 1, 1, 1, 1 });
```

Colours use straight (not premultiplied) alpha. `Color::FromBytes(r, g, b, a)`
takes values from 0 to 255; `Color{ r, g, b, a }` takes 0 to 1.

## Shapes

| Call | Draws |
|---|---|
| `FillRect(rect, color)` | A filled rectangle. |
| `FillRoundedRect(rect, radius, color)` | A rounded rectangle; `radius` can also be `CornerRadii` for a radius per corner. |
| `FillCircle(center, radius, color)` | A filled circle. |
| `StrokeRect(rect, thickness, color, radius)` | A rectangle's outline, centred on its edge. |
| `StrokeRoundedRect(rect, radii, thickness, color)` | An outline with a radius per corner. |
| `StrokeCircle(center, radius, thickness, color)` | A circle's outline. |
| `DrawLine(from, to, thickness, color)` | A straight line with round ends. |

## Styled boxes

The same fills, shadows, and frosted glass that element looks use (see
[Designing your own look](../styling/your-own-look.md)):

| Call | Draws |
|---|---|
| `DrawFill(rect, fill, radii)` | A box filled with a colour, gradient, or image. |
| `DrawShadow(rect, radii, shadow)` | A soft shadow or glow of a rounded box. |
| `DrawBox(rect, style)` | A whole `BoxStyle`: shadows, background, inset shadows, border. |
| `DrawImage(rect, texture, fit, tint, radii, slice, tileScale)` | An image fitted to a box. |
| `BackdropBlur(rect, radius, radii, tint)` | Blurs everything drawn beneath the box so far: frosted glass. |

## Textures and text

| Call | Draws |
|---|---|
| `DrawTexture(rect, texture, tint)` | A whole texture. |
| `DrawTextureRegion(rect, texture, uv, tint)` | Part of a texture; `uv` is in 0..1. |
| `DrawMaskedTextureRegion(rect, texture, uv, color)` | Uses the texture's red channel as coverage, filled with `color`, as glyphs are drawn. |
| `DrawPremultipliedTexture(rect, texture, opacity)` | A texture that already holds premultiplied colour, such as a render target's output. |
| `DrawText(text, position, font, color, align)` | One line; `position` is the top of the line. |
| `DrawTextInRect(text, rect, font, color, align)` | One line centred vertically in a rectangle. |
| `DrawRichText(runs, position, font, color, align)` | One line of runs in their own fonts and colours. |
| `DrawTextWrapped(text, rect, font, color, align)` | Text wrapped to the rectangle's width; returns its height. |

See [Text and fonts](../interface/text-and-fonts.md) for fonts and measuring.

## Clipping, transforms, and opacity

These are stacks: each push applies until the matching pop, on top of any
push already in effect.

```cpp
drawList.PushClip({ 0, 0, 400, 300 });        // confined to the intersection with the current clip
drawList.PushTransform(opane::Affine2D::About({ 200, 150 }, 30.0f, { 1.5f, 1.5f }));
drawList.PushOpacity(0.5f);

// ... draw here

drawList.PopOpacity();
drawList.PopTransform();
drawList.PopClip();
```

`Affine2D` builds 2D transforms: `Translation`, `Rotation` (degrees,
clockwise), `Scaling`, and `About(pivot, degrees, scale)`, combined with `*`.
`Apply`, `Inverse`, and `IsIdentity` work on them too.

## Materials

`SetMaterial(material)` draws every shape recorded after it with a custom
shader, until `SetMaterial({})` returns to the default. See
[Custom shaders](custom-shaders.md).

## Pixels

`GetPixelScale()` is the number of pixels per interface unit. To draw
something exactly one pixel wide, make it `1 / GetPixelScale()` units wide.

## Batching

Recorded commands are merged into as few draw calls as possible. Shapes
without a texture all sample one white pixel, so a screen of panels, outlines,
and circles is usually a single draw call. A change of texture, clip
rectangle, or material starts a new one, and each frosted-glass box ends the
render pass to blur what is behind it.

## Limitations

- Only rectangles, rounded rectangles, circles, and straight lines. There are
  no arbitrary paths, polygons, curves, arcs, or triangle meshes.
- Strokes are solid. There are no dashed lines or gradient strokes.
- The draw list is 2D. For 3D, host a world; see
  [Hosting a world](../application/hosting-a-world.md).
