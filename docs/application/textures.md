# Textures

A texture is an image on the GPU. You load one from a file or make one from
pixels you already have, then draw it through its `TextureId`, either with the
[draw list](../extending/drawing-directly.md) or with an
[`Image` element](../interface/widgets.md).

```cpp
opane::TextureId portrait = app.LoadTexture("portrait.png");

std::vector<uint8_t> pixels(64 * 64 * 4, 255);   // white, four bytes per pixel
opane::TextureId made = app.CreateTexture(64, 64, opane::PixelFormat::Rgba8, pixels.data());

opane::Vec2 size = app.GetTextureSize(portrait);   // in pixels
drawList.DrawTexture({ 20, 20, size.X, size.Y }, portrait);

app.DestroyTexture(made);
```

## Loading from files

`LoadTexture` reads PNG, JPEG, BMP, TGA, GIF (the first frame), PSD, and HDR
files through the [asset roots](../getting-started/finding-files.md). Loading
the same name again returns the same texture, so an image used in many places
is loaded once.

Every loaded image gets a full mip chain, built in linear light and weighted by
alpha so it does not darken as it shrinks or grow dark fringes around
transparent edges. It is sampled with trilinear filtering and 8x anisotropic
filtering, so an image drawn much smaller than its size stays smooth instead
of shimmering.

A file that cannot be found or decoded gives a magenta-and-black checkerboard
texture, and the log says why. A missing image is easy to spot on screen.

## Making textures from pixels

`CreateTexture` takes pixels in one of two formats:

- `PixelFormat::Rgba8`: four bytes per pixel, red, green, blue, alpha, with
  straight (not premultiplied) alpha, rows from top to bottom.
- `PixelFormat::Alpha8`: one byte per pixel, used as coverage, for masks. The
  glyph atlas uses this format.

The pixels are copied, so your buffer can be freed as soon as the call
returns.

## Textures from other libraries

`WrapExternalTexture` lets opane draw a texture that something else owns, such
as a world's render target:

```cpp
opane::TextureId view = app.WrapExternalTexture(sdlGpuTexture, width, height);
```

opane does not take ownership. `DestroyTexture` on a wrapped texture releases
only the handle, never the texture. [Hosting a world](hosting-a-world.md) does
this for you.

## Handles

A `TextureId` holds an index and a generation counter. Once a texture is
destroyed its old handles are recognised as stale, and drawing with one draws
white instead of whatever texture reused the slot.

## Limitations

- A texture cannot be updated in place. To change its pixels, destroy it and
  create a new one.
- Only `Rgba8` and `Alpha8` can be created from pixels. There are no floating
  point or compressed formats (such as BC or ASTC), and HDR files are
  converted to 8 bits per channel when loaded.
- Animated GIFs load only their first frame.
- A texture's size is limited by what the GPU supports.
