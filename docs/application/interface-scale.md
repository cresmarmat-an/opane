# Interface scale

Everything in the interface (sizes, offsets, font sizes, and pointer
positions) is measured in **interface units**. The draw list multiplies them by
the interface scale on the way to the GPU, so an interface you lay out once is
the same physical size on every display and stays sharp.

By default the scale follows the display: 1 at 100%, 1.5 on a laptop set to
150%, and 2 on a Retina screen. When the window moves to a display with a
different scale, the scale changes with it.

```cpp
opane::App app = opane::StartApp({ .UiScale = 0.0f });   // 0 follows the display

app.SetUiScale(1.25f);     // choose a scale, for example from an options screen
app.SetUiScale(0.0f);      // follow the display again

app.GetUiScale();          // pixels per interface unit
app.GetWindowSize();       // the window, in interface units
app.GetPixelSize();        // the window, in pixels
```

A scale you set is clamped between 0.25 and 8.

## What changes with the scale

- **Text** is rasterized at the size it covers on screen, not magnified: a
  16-unit font at a scale of 1.5 is drawn as a 24-pixel font.
- **Viewports** render their world at the element's size in pixels, and
  **render-target elements** make their textures at their size in pixels, so
  neither is upscaled.
- **Images** keep their size in interface units and are scaled like everything
  else. Mip maps keep them smooth when they are drawn smaller.

## In your own elements

A custom element does not need to know the scale; it draws in interface units
like everything else. To draw a line exactly one pixel wide, make it
`1 / drawList.GetPixelScale()` units wide.

## Limitations

- The scale applies to the whole window. There is no per-element zoom, though
  an element's [transform](../styling/your-own-look.md) can scale how it is
  drawn.
- Images are not swapped for higher-resolution versions at higher scales. Load
  a larger image yourself if you need more detail.
