// opane material contract.
//
// Include this from a custom interface shader. It defines the inputs opane's
// vertex stage produces, the uniform block your Params land in, the texture
// binding, and helpers for things that are easy to get wrong, mainly
// anti-aliasing procedural edges.
//
// A custom material replaces the fragment stage only. The vertex stage stays
// opane's, so layout, batching, and positioning behave identically no matter
// what a material draws.
//
//   #include "material.hlsli"
//
//   float4 FragmentMain(SurfaceInput input) : SV_Target
//   {
//       float2 uv = LocalUv(input);
//       float3 color = lerp(Params[0].rgb, Params[1].rgb, uv.y);
//       float alpha = ShapeCoverage(input) * input.Color.a;
//       return Premultiply(color, alpha);
//   }

#ifndef OPANE_MATERIAL_HLSLI
#define OPANE_MATERIAL_HLSLI

// One source serves Direct3D 12, Vulkan, and Metal. The registers below are
// what Direct3D reads; for Vulkan and Metal, which are compiled through
// SPIR-V, a texture and its sampler also share one slot as a combined image
// sampler. None of this is anything a material has to repeat.
#ifdef __spirv__
#define OPANE_SAMPLED(slot, set) [[vk::combinedImageSampler]] [[vk::binding(slot, set)]]
#else
#define OPANE_SAMPLED(slot, set)
#endif

struct SurfaceInput
{
    float4 Position : SV_Position;
    float2 Uv       : TEXCOORD0; // texture coordinates, when a texture is bound
    float4 Color    : TEXCOORD1; // the color the caller asked for, straight alpha
    float4 ShapeA   : TEXCOORD2; // xy offset from the element center, zw half size
    float4 ShapeB   : TEXCOORD3; // x corner radius, y stroke width, z texture mode, w skip flag
    float4 Radii    : TEXCOORD4; // each corner's radius: top left, top right, bottom right, bottom left
};

// What a quad draws, from ShapeB.z. A material usually needs only the glyph
// tests below, but every mode is here for one that wants to tell them apart.
static const int ModePlain = 0;          // a shape in its colour
static const int ModeTexture = 1;        // a texture times the colour
static const int ModeMask = 2;           // a coverage glyph: the texture's red is coverage
static const int ModePremultiplied = 3;  // a texture already premultiplied
static const int ModeGlyphField = 4;     // a distance-field glyph
static const int ModeLinearGradient = 5; // a gradient along Uv.x
static const int ModeRadialGradient = 6; // a gradient out along length(Uv)
static const int ModeShadow = 7;         // a soft shadow: ShapeB.y is its blur
static const int ModeInsetShadow = 8;    // inside the shape: ShapeB.x spread, ShapeB.y blur, Uv offset
static const int ModeTiled = 9;          // a texture repeated across the shape
static const int ModeBackdrop = 10;      // the blurred backdrop, tinted by the colour

int Mode(SurfaceInput input)
{
    return (int)round(input.ShapeB.z);
}

// Your values, in the order you named them when creating the material.
// Anything you did not set reads as zero.
cbuffer OpaneMaterial : register(b0, space3)
{
    float4 Params[8];
    float4 OpaneTime; // x = seconds since start, y = seconds since last frame
};

OPANE_SAMPLED(0, 2) Texture2D Surface : register(t0, space2);
OPANE_SAMPLED(0, 2) SamplerState SurfaceSampler : register(s0, space2);

// --- geometry -------------------------------------------------------------

// Signed distance to the element's rounded box. Negative inside.
float RoundedBoxDistance(float2 samplePoint, float2 halfSize, float radius)
{
    radius = min(radius, min(halfSize.x, halfSize.y));
    float2 q = abs(samplePoint) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// The same with a radius for each corner: top left, top right, bottom right,
// bottom left. Y points down.
float RoundedBoxDistance(float2 samplePoint, float2 halfSize, float4 radii)
{
    float radius = samplePoint.x < 0.0 ? (samplePoint.y < 0.0 ? radii.x : radii.w)
                                       : (samplePoint.y < 0.0 ? radii.y : radii.z);
    return RoundedBoxDistance(samplePoint, halfSize, radius);
}

// Distance to this element's own shape, honoring its corner radii and stroke.
float ShapeDistance(SurfaceInput input)
{
    float distance = RoundedBoxDistance(input.ShapeA.xy, input.ShapeA.zw, input.Radii);
    if (input.ShapeB.y > 0.0)
    {
        distance = abs(distance) - input.ShapeB.y * 0.5;
    }
    return distance;
}

// --- anti-aliasing --------------------------------------------------------

// Coverage of any signed distance, anti-aliased from the screen-space
// derivative. Use this instead of a step() on every procedural edge you draw;
// a hard step is what makes a custom material look harsher than the built-ins.
float AntiAlias(float distance)
{
    float width = max(fwidth(distance), 1e-5);
    return saturate(0.5 - distance / width);
}

// Anti-aliased coverage of this element's shape.
float ShapeCoverage(SurfaceInput input)
{
    if (input.ShapeB.w > 0.5)
    {
        return 1.0;
    }
    return AntiAlias(ShapeDistance(input));
}

// A hard threshold, anti-aliased. Use in place of step(edge, value).
float AntiAliasedStep(float edge, float value)
{
    return AntiAlias(edge - value);
}

// --- text -----------------------------------------------------------------
//
// A glyph arrives as a quad whose texture holds either coverage or a signed
// distance field. Small text is coverage, rasterized at the size it is drawn,
// because strokes at those sizes are thinner than any practical field can
// describe. Larger text is a field, so a material can shade the letters
// themselves, for example with an outline, a glow, or a gradient.

// True when this quad is a glyph, of either kind. A material may draw text and
// shapes differently while staying on one pipeline.
bool IsGlyph(SurfaceInput input)
{
    return Mode(input) == ModeMask || Mode(input) == ModeGlyphField;
}

// True when the glyph comes from a distance field, which the effects below
// need. A material that offers an outline should check this and fall
// back to plain coverage for small text.
bool IsGlyphField(SurfaceInput input)
{
    return Mode(input) == ModeGlyphField;
}

// Distance from the letter's edge, in screen pixels: negative inside, positive
// outside. Readable several pixels out, which is the budget an outline or a
// glow has to work in. Field glyphs only.
float GlyphDistance(SurfaceInput input)
{
    float field = Surface.Sample(SurfaceSampler, input.Uv).r;

    // 128/255 is the edge; below it is outside the letter.
    float distance = 0.50196 - field;
    return distance / max(fwidth(distance), 1e-5);
}

// Anti-aliased coverage of the letter itself, whichever kind it is.
float GlyphCoverage(SurfaceInput input)
{
    if (!IsGlyphField(input))
    {
        return Surface.Sample(SurfaceSampler, input.Uv).r;
    }
    return saturate(0.5 - GlyphDistance(input));
}

// Coverage of the letter grown by this many screen pixels, for outlines and
// soft shadows. Field glyphs only; coverage glyphs cannot grow, and return
// their own coverage unchanged.
float GlyphCoverageAt(SurfaceInput input, float pixelsOutside)
{
    if (!IsGlyphField(input))
    {
        return GlyphCoverage(input);
    }
    return saturate(0.5 - (GlyphDistance(input) - pixelsOutside));
}

// --- coordinates ----------------------------------------------------------

// 0..1 across the element, with (0,0) at its top-left corner.
float2 LocalUv(SurfaceInput input)
{
    return input.ShapeA.xy / max(input.ShapeA.zw, float2(1e-5, 1e-5)) * 0.5 + 0.5;
}

// The element's size in pixels, useful for aspect-correct effects.
float2 ElementSize(SurfaceInput input)
{
    return input.ShapeA.zw * 2.0;
}

// Pixels from the element center, aspect preserved.
float2 CenteredPixels(SurfaceInput input)
{
    return input.ShapeA.xy;
}

float Seconds()
{
    return OpaneTime.x;
}

float DeltaSeconds()
{
    return OpaneTime.y;
}

// --- output ---------------------------------------------------------------

// opane blends with premultiplied alpha. Return through this so translucent
// materials composite correctly over whatever is beneath them.
float4 Premultiply(float3 color, float alpha)
{
    return float4(color * alpha, alpha);
}

#endif // OPANE_MATERIAL_HLSLI
