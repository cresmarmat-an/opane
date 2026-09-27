# Designing your own look

The built-in look is only a starting point. Any element, whether a panel, a
button, a label, or one you wrote, can have a background image, a gradient,
frosted glass, soft shadows and glows, different rounding on each corner, a
different look for each state that blends in over time, and a transform. A
tool, an app, a game's menus, and an editor can each have their own style, and
a [theme file](skins-and-theme-files.md) can restyle all of it while the
program runs. The
[custom UI example](https://github.com/cresmarmat-an/opane-examples/tree/main/03-custom-ui)
uses everything on this page.

## An element's look

`Appearance` gives an element a look for each state. It is drawn beneath
whatever the element paints itself; a built-in widget with an `Appearance`
draws it instead of its own box.

```cpp
auto* card = root->Add<opane::Panel>();
card->Appearance.Normal.Background = opane::Fill::Linear(opane::Color::FromBytes(34, 43, 68),
                                                         opane::Color::FromBytes(22, 28, 46), 120.0f);
card->Appearance.Normal.Radius = 18.0f;
card->Appearance.Normal.BorderColor = opane::Color{ 1.0f, 1.0f, 1.0f, 0.1f };
card->Appearance.Normal.BorderWidth = 1.0f;
card->Appearance.Normal.Shadows = { { .Color = { 0, 0, 0, 0.4f }, .Offset = { 0, 8 }, .Blur = 24 } };

opane::BoxStyle lifted = card->Appearance.Normal;   // hovered: the same, raised and glowing
lifted.Scale = 1.03f;
lifted.Shadows = { { .Color = { 0.49f, 0.61f, 1.0f, 0.3f }, .Offset = { 0, 10 }, .Blur = 32, .Spread = 2 } };
card->Appearance.Hovered = lifted;

card->Appearance.Transition = 0.2f;                 // seconds
card->Appearance.Curve = opane::Easing::Smooth;
```

The states are `Normal`, `Hovered`, `Pressed`, `Focused`, `Selected`, and
`Disabled`. Any you leave unset look like `Normal`. When several apply, the
first of Disabled, Pressed, Selected, Hovered, and Focused wins.

- Hovered, Pressed, and Focused include the element's children, so a card
  counts as hovered while the pointer is over a label on it.
- Selected applies to elements you mark with `SetSelected(true)`, such as the
  current tab or a chosen swatch, and to checkboxes, toggles, and radio
  buttons while they are on.
- Changes between states blend over `Transition` seconds: colours, gradients,
  radii, borders, shadows, opacity, scale, and offset.

## BoxStyle

| Field | What it does |
|---|---|
| `Background` | A `Fill`: a colour, a gradient, or an image. |
| `BorderColor`, `BorderWidth` | A border drawn inside the edge, so it never changes the size. |
| `Radius` | `CornerRadii`: one number for all corners, or `{ topLeft, topRight, bottomRight, bottomLeft }`. |
| `Shadows` | Soft shadows beneath the box, or inside it with `Inset`. A glow is a bright shadow with no offset. |
| `BackdropBlur` | Blurs whatever is behind the box by this many units: frosted glass. The background needs some transparency for the blur to show. |
| `Opacity` | Multiplies the element and everything inside it. |
| `TextColor` | The text colour built-in widgets use on this box. Transparent keeps the theme's. |
| `Scale`, `Offset` | A scale (about the centre) and a shift that blend with the state, such as a pressed button moving down a pixel. They do not affect layout. |

A `Shadow` has a `Color`, an `Offset`, a `Blur` distance, a `Spread` that
grows the shape before blurring, and `Inset`.

## Fills

```cpp
opane::Fill::Solid(color);
opane::Fill::Linear(from, to, 90.0f);                          // degrees: 0 is left to right, 90 top to bottom
opane::Fill::Linear({ { 0.0f, a }, { 0.6f, b }, { 1.0f, c } }, 30.0f);   // any number of stops
opane::Fill::Radial(inner, outer, { 0.4f, 0.35f });            // from a point to the farthest corner
opane::Fill::FromImage(app.LoadTexture("sky.png"), opane::ImageFit::Cover);
opane::Fill::NineSliced(app.LoadTexture("frame.png"), opane::Insets{ 22.0f });
```

Gradients span the whole box, corner to corner along their direction, as CSS
gradients do. A radial gradient's `Center` runs from `{0, 0}` at the top left
to `{1, 1}` at the bottom right, and a `Radius` of 0 reaches the farthest
corner.

An image fills the box in one of six ways:

| `ImageFit` | Result |
|---|---|
| `Stretch` | Fills the box exactly, distorting the image if the shapes differ. |
| `Contain` | The whole image, as large as fits, with empty space around it. |
| `Cover` | Fills the box, cropping whatever overhangs. |
| `Center` | At its own size, centred, cropped if larger. |
| `Tile` | Repeated from the top left; `TileScale` sizes the repeats. |
| `NineSlice` | Corners kept at their own size, edges and middle stretched. For frames and buttons drawn as images; `Slice` sets the border in image pixels. |

Every fill follows the box's rounded corners. To put an image behind the whole
window, give the root a look:

```cpp
app.GetRoot()->Appearance.Normal.Background = opane::Fill::FromImage(app.LoadTexture("wallpaper.png"));
```

## Transforms, opacity, and disabling

```cpp
icon->Transform.Rotation = angle;           // degrees, clockwise, about Pivot (the centre by default)
card->Transform.Scale = { 1.1f, 1.1f };
panel->Transform.Translate = { 0.0f, slide };
panel->Opacity = 0.8f;                      // the element and everything in it
button->Enabled = false;                    // ignores input, and dims or shows its Disabled look
```

A transform changes how an element and its children are drawn, not the layout
around them. Hit testing follows the transform, so a rotated button is pressed
where it appears, and event positions arrive in the element's own layout
coordinates, so a rotated slider still drags the right way.

## Painting your own

`Painter` replaces an element's own painting with a function. Its
`Appearance` is still drawn beneath and its children on top:

```cpp
meter->Painter = [&](opane::DrawList& list, opane::Element& self) {
    const opane::Rect box = self.GetBounds();
    list.DrawBox(box, glass);                                  // a whole BoxStyle
    list.FillRoundedRect(box, opane::CornerRadii{ 12, 12, 2, 2 }, color);
    list.DrawShadow(box, 12.0f, { .Color = glow, .Offset = {}, .Blur = 20 });
    list.DrawImage(box, texture, opane::ImageFit::Cover, tint, 12.0f);
    list.PushTransform(opane::Affine2D::About({ box.X + 20, box.Y + 20 }, spin, { 1, 1 }));
    list.PushOpacity(0.5f);
    list.DrawLine(from, to, 2.0f, color);
    list.PopOpacity();
    list.PopTransform();
};
```

The same calls are available in any element's `Paint`; see
[Drawing directly](../extending/drawing-directly.md).

## Limitations

- Borders are solid. There are no dashed or dotted borders, and one border per
  box.
- Frosted glass blurs what was drawn beneath the box earlier in the same
  frame. It adds a render pass for each glass box, so many large glass boxes
  cost noticeably more GPU time.
- Transforms are 2D. There is no perspective or 3D rotation.
- Only state changes are animated. To animate a look over time in other ways,
  change it yourself each frame, or use a
  [custom shader](../extending/custom-shaders.md) with
  [animated uniforms](../interface/animating-values.md).
