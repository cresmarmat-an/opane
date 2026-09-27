# Custom shaders

A **material** is a custom fragment shader plus a set of values called
uniforms. You can put one on any element, and only its pixels change: it still
lays out, clips, batches, and receives input as before, because the vertex
stage stays opane's. Shaders are written in HLSL and compiled for every
backend.

## Writing the shader

Include `material.hlsli`. It defines the input your shader receives, the
uniform block your values arrive in, the texture binding, and helpers for the
things that are easy to get wrong, mainly anti-aliasing:

```hlsl
#include "material.hlsli"

float4 FragmentMain(SurfaceInput input) : SV_Target
{
    float2 uv = LocalUv(input);                 // 0..1 across the element
    float3 color = lerp(Params[0].rgb, Params[1].rgb, uv.y);

    // AntiAlias instead of step(), so the band's edges are smooth.
    float band = frac(Seconds() * 0.2);
    color += 0.3 * AntiAlias(abs(uv.x - band) - 0.05);

    // ShapeCoverage keeps the element's own rounded corners.
    float alpha = ShapeCoverage(input) * input.Color.a;

    return Premultiply(color, alpha);
}
```

What `material.hlsli` provides:

| Name | Purpose |
|---|---|
| `Params[0..7]` | Your uniforms, as `float4`, in the order you declared them. |
| `input.Color` | The colour the element drew with. |
| `input.Uv` | Texture coordinates. |
| `LocalUv(input)` | 0..1 across the element. |
| `ElementSize(input)` | The element's size in pixels. |
| `ShapeCoverage(input)` | Anti-aliased coverage of the element's own shape. |
| `ShapeDistance(input)` | Signed distance to the element's rounded box. |
| `AntiAlias(distance)` | Coverage for any signed distance. |
| `AntiAliasedStep(edge, value)` | A smooth replacement for `step`. |
| `Seconds()`, `DeltaSeconds()` | The clock. |
| `Surface`, `SurfaceSampler` | The texture bound to the draw. |
| `IsGlyph(input)` | True when the quad is a letter. |
| `IsGlyphField(input)` | True when that letter comes from a distance field. |
| `GlyphCoverage(input)` | Anti-aliased coverage of the letter. |
| `GlyphDistance(input)` | Pixels from the letter's edge, negative inside. |
| `GlyphCoverageAt(input, pixels)` | Coverage of the letter grown by this many pixels. |
| `Premultiply(rgb, alpha)` | Converts your colour to the premultiplied form opane blends with. |

Always return premultiplied colour, through `Premultiply`.

## Using a material

```cpp
opane::MaterialId aurora = app.CreateMaterial({
    .ShaderPath = "shaders/aurora.hlsl",
    .Uniforms = {
        { "ColorA", opane::Color::FromBytes(38, 66, 150) },
        { "ColorB", opane::Color::FromBytes(156, 62, 176) },
        { "Speed", 1.0f },
    },
});
if (!aurora.IsValid())
{
    // the compiler's message is in the log
}

panel->Material = aurora;
app.SetMaterialUniform(aurora, "Speed", 2.5f);
app.DestroyMaterial(aurora);
```

The order of `Uniforms` decides which `Params[]` slot each name uses, up to
eight. A uniform is up to four floats, or a `Color`. Setting a name that was
not declared does nothing and logs a warning. Uniforms can also be
[animated](../interface/animating-values.md).

The entry point is `FragmentMain` unless you set `EntryPoint`.

Inside a custom element, you can switch materials while painting:

```cpp
void Paint(opane::DrawList& drawList) override
{
    drawList.SetMaterial(m_Glow);
    drawList.FillRoundedRect(GetBounds(), 12.0f, opane::Color{ 1, 1, 1, 1 });
    drawList.SetMaterial({});     // back to the default
}
```

A change of material starts a new draw call. That is cheap, so there is no
need to rearrange a layout to group elements by material.

## Shading text

A material on a label receives the label's letters, one quad each. Letters
above about 26 pixels come from a distance field, which tells the shader how
far each pixel is from the edge of the letter. That is what outlines, glows,
and gradients inside letters are made from:

```hlsl
float4 FragmentMain(SurfaceInput input) : SV_Target
{
    if (!IsGlyph(input))
    {
        return Premultiply(input.Color.rgb, input.Color.a * ShapeCoverage(input));
    }

    float3 fill = lerp(Params[0].rgb, Params[1].rgb, saturate(input.Uv.y));

    float letter = GlyphCoverage(input);
    float grown = GlyphCoverageAt(input, Params[3].x);   // the outline's width, in pixels

    return Premultiply(lerp(Params[2].rgb, fill, letter), grown * input.Color.a);
}
```

Small text is rasterized as coverage, not a field, so `GlyphDistance` and
`GlyphCoverageAt` have nothing to work with. `IsGlyphField` tells you which
kind you have, `GlyphCoverage` works for both, and `GlyphCoverageAt` returns
the plain letter for small text. A shader that draws outlines should skip
them on small text.

## During development and when shipping

**During development**, set `ShaderPath` to an `.hlsl` file. It is compiled
when the material is created and **recompiled whenever you save it**, keeping
the uniform values. A compile error keeps the last working version on screen
and logs the compiler's message with the file, line, and column.
`HotReload = false` turns reloading off.

**When shipping**, compile the shader while your program builds, so no
compiler and no `.hlsl` file ship with it. `opane_add_material` compiles it for
every backend the build targets and embeds the result:

```cmake
opane_add_material(my_app shaders/glow.hlsl SYMBOL GlowShader)
```

```cpp
#include "GlowShader.h"

opane::MaterialId glow = app.CreateMaterial({
    .Bytecode = GlowShader(),
    .Uniforms = { /* as before */ },
});
```

`GlowShader()` returns an `opane::ShaderBytecode` holding the shader for each
backend. It is compiled against the `material.hlsli` of the opane you build
with, and rebuilt when the shader or that file changes. Add `ENTRY <name>`
when the entry point is not `FragmentMain`. `Bytecode` wins over `ShaderPath`
when both are set, so one description can serve both kinds of build.

### Where runtime compilers are found

A material created from `ShaderPath` is compiled for the running backend, with
compilers looked for the first time they are needed:

| Backend | Looked for, in order |
|---|---|
| Direct3D 12 | `dxc` named by the `OPANE_DXC` environment variable, the `dxc` opane was built with, then the newest Windows SDK's |
| Vulkan | a `dxc` that can output SPIR-V, named by `OPANE_DXC_SPIRV`, the one opane was built with, then the Vulkan SDK's (through `VULKAN_SDK`) |
| Metal | the same `dxc`, plus `spirv-cross` named by `OPANE_SPIRV_CROSS`, the one opane was built with, or the Vulkan SDK's |

The `material.hlsli` the shader includes is built into opane and written next
to the shader cache, so it always matches the library that runs the result.

## Filtering a whole subtree

A material changes one element's own pixels. To filter an element **and
everything inside it**, such as a panel with its buttons and labels, give it a
`PostProcess` chain. The subtree is rendered into a texture of its own size,
each material runs over the result in order, and the final image is drawn
where the element sits.

```cpp
opane::MaterialId ripple = app.CreateMaterial({ .ShaderPath = "shaders/ripple.hlsl",
                                                .Uniforms = { { "Wave", 2.5f, 18.0f } } });
opane::MaterialId crt    = app.CreateMaterial({ .ShaderPath = "shaders/crt.hlsl",
                                                .Uniforms = { { "Strength", 1.0f } } });

sidebar->PostProcess = { ripple, crt };   // ripple first, then crt on its output
sidebar->PostProcess = {};                // back to drawing normally
```

Setting `PostProcess` turns on `RenderToTexture`. Setting `RenderToTexture`
alone renders the subtree to a texture without filtering it. The widgets keep
working normally; only their pixels are filtered. Render targets can be
nested: an inner one finishes first, and the outer one filters the result.

A post-process shader reads the previous stage through `Surface`, with
`input.Uv` running 0..1 across the element:

```hlsl
#include "material.hlsli"

float4 FragmentMain(SurfaceInput input) : SV_Target
{
    float2 size = ElementSize(input);       // the element's size in pixels
    float4 color = Surface.Sample(SurfaceSampler, input.Uv);

    float scan = 0.80 + 0.20 * sin(input.Uv.y * size.y * 3.14159 / 1.5);
    color.rgb *= scan;

    // Surface already holds premultiplied colour, so return it as it is,
    // without Premultiply.
    return color;
}
```

Each render target uses two textures the size of the element and one pass per
material, so use it for effects over a group of elements. For a single panel,
a plain `Material` is cheaper.

## Limitations

- Materials replace the fragment stage only. You cannot write vertex, compute,
  or geometry shaders.
- A material has at most eight uniforms, each four floats.
- A material can sample only the texture bound to the draw (`Surface`). There
  is no way to bind extra textures to a material.
- Compiling from `ShaderPath` at runtime needs `dxc` (and `spirv-cross` for
  Metal) on the machine running the program. Use `opane_add_material` for
  programs you distribute.
