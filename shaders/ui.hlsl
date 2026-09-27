// opane's default interface shader.
//
// One pipeline draws every built-in primitive: filled and stroked rounded
// rectangles, circles, textured quads, and glyphs. Keeping them on one pipeline
// lets the batcher merge a whole screen into a few draw calls.
//
// The fragment stage below is what a custom material replaces, and it uses the
// same material.hlsli that custom materials use.

#include "material.hlsli"

struct VertexInput
{
    float2 Position : TEXCOORD0; // pixels, top-left origin
    float2 Uv       : TEXCOORD1;
    float4 Color    : TEXCOORD2; // straight alpha
    float4 ShapeA   : TEXCOORD3; // xy local offset from center, zw half size
    float4 ShapeB   : TEXCOORD4; // x radius, y stroke width, z texture mode, w skip-shape flag
    float4 Radii    : TEXCOORD5; // each corner's radius: top left, top right, bottom right, bottom left
};

cbuffer VertexUniforms : register(b0, space1)
{
    float2 ViewportSize;

    // The window position of the target's top-left corner. Zero for the
    // window itself; an element's own position when it renders its subtree
    // into a texture, so the same draw list lands at the texture's origin.
    float2 ViewportOrigin;
};

SurfaceInput VertexMain(VertexInput input)
{
    SurfaceInput output;

    // Pixel space (y down, origin top-left) to clip space.
    float2 normalized = (input.Position - ViewportOrigin) / max(ViewportSize, float2(1.0, 1.0));
    output.Position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);

    output.Uv = input.Uv;
    output.Color = input.Color;
    output.ShapeA = input.ShapeA;
    output.ShapeB = input.ShapeB;
    output.Radii = input.Radii;
    return output;
}

// The error function, closely enough for a shadow's edge: the integral of a
// Gaussian, which is what a box blurred by one looks like across its edge.
float Erf(float x)
{
    float s = sign(x);
    float a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

// How much of a shape blurred by sigma covers a point this far outside it.
float BlurredCoverage(float distance, float sigma)
{
    if (sigma < 0.5)
    {
        return AntiAlias(distance);
    }
    return 0.5 - 0.5 * Erf(distance / (sigma * 1.41421356));
}

float4 FragmentMain(SurfaceInput input) : SV_Target
{
    int mode = Mode(input);

    // A glyph: the atlas holds a signed distance field, so coverage is
    // computed from it instead of read directly. This keeps large text sharp
    // at any size.
    if (mode == ModeGlyphField)
    {
        float4 glyph = input.Color;
        glyph.a *= GlyphCoverage(input);
        return Premultiply(glyph.rgb, glyph.a);
    }

    // A texture that already holds premultiplied colour, such as a
    // render-target element's output. Multiplying it through Premultiply again would darken
    // every translucent edge, so it is scaled and returned as it is.
    if (mode == ModePremultiplied)
    {
        float4 sample = Surface.Sample(SurfaceSampler, input.Uv);
        return sample * input.Color.a * ShapeCoverage(input);
    }

    // Gradients read a strip holding the gradient, premultiplied, at how far
    // along it this point is; the colour tints it.
    if (mode == ModeLinearGradient || mode == ModeRadialGradient)
    {
        float t = mode == ModeLinearGradient ? input.Uv.x : length(input.Uv);
        float4 gradient = Surface.SampleLevel(SurfaceSampler, float2(saturate(t), 0.5), 0);
        gradient.rgb *= input.Color.rgb;
        return gradient * input.Color.a * ShapeCoverage(input);
    }

    // A shadow is the shape blurred: coverage falls off across its edge as a
    // Gaussian does, rather than in the single pixel of an ordinary edge.
    if (mode == ModeShadow)
    {
        float distance = RoundedBoxDistance(input.ShapeA.xy, input.ShapeA.zw, input.Radii);
        return Premultiply(input.Color.rgb, input.Color.a * BlurredCoverage(distance, input.ShapeB.y));
    }

    // An inset shadow darkens the inside of the shape wherever it is not
    // covered by a smaller copy of the shape, moved by the offset and blurred.
    if (mode == ModeInsetShadow)
    {
        float inside = AntiAlias(RoundedBoxDistance(input.ShapeA.xy, input.ShapeA.zw, input.Radii));
        float spread = input.ShapeB.x;
        float2 innerHalf = max(input.ShapeA.zw - spread, float2(0.0, 0.0));
        float4 innerRadii = max(input.Radii - spread, float4(0.0, 0.0, 0.0, 0.0));
        float inner = BlurredCoverage(RoundedBoxDistance(input.ShapeA.xy - input.Uv, innerHalf, innerRadii),
                                      input.ShapeB.y);
        return Premultiply(input.Color.rgb, input.Color.a * inside * (1.0 - inner));
    }

    // A tiled texture: the coordinates run past 1 and wrap, and the
    // derivatives are taken before the wrap so the seams do not flash.
    if (mode == ModeTiled)
    {
        float4 tile = Surface.SampleGrad(SurfaceSampler, frac(input.Uv), ddx(input.Uv), ddy(input.Uv));
        float4 color = input.Color * tile;
        color.a *= ShapeCoverage(input);
        return Premultiply(color.rgb, color.a);
    }

    // Frosted glass: what was behind, blurred, with the tint laid over it by
    // the tint's alpha. ShapeB.x carries the opacity.
    if (mode == ModeBackdrop)
    {
        float3 behind = Surface.SampleLevel(SurfaceSampler, input.Uv, 0).rgb;
        float3 glass = lerp(behind, input.Color.rgb, input.Color.a);
        float alpha = ShapeCoverage(input) * input.ShapeB.x;
        return float4(glass * alpha, alpha);
    }

    float4 color = input.Color;

    if (mode == ModeMask)
    {
        // Alpha mask, used by the glyph atlas. Coverage comes from the
        // texture; the color is the caller's.
        color.a *= Surface.Sample(SurfaceSampler, input.Uv).r;
    }
    else if (mode == ModeTexture)
    {
        color *= Surface.Sample(SurfaceSampler, input.Uv);
    }

    color.a *= ShapeCoverage(input);

    return Premultiply(color.rgb, color.a);
}
