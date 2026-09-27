#include <opane/opane.h>

#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace opane
{
namespace
{

bool SameTexture(const TextureId& a, const TextureId& b)
{
    return a.Index == b.Index && a.Generation == b.Generation;
}

bool SameMaterial(const MaterialId& a, const MaterialId& b)
{
    return a.Index == b.Index && a.Generation == b.Generation;
}

bool SameRect(const Rect& a, const Rect& b)
{
    return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height;
}

Rect IntersectRect(const Rect& a, const Rect& b)
{
    const float left = std::max(a.X, b.X);
    const float top = std::max(a.Y, b.Y);
    const float right = std::min(a.X + a.Width, b.X + b.Width);
    const float bottom = std::min(a.Y + a.Height, b.Y + b.Height);

    return Rect{ left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top) };
}

// The axis-aligned box around a rectangle carried through a transform.
Rect TransformedBounds(const Affine2D& transform, const Rect& rect)
{
    const Vec2 corners[4] = { transform.Apply(Vec2{ rect.X, rect.Y }),
                              transform.Apply(Vec2{ rect.X + rect.Width, rect.Y }),
                              transform.Apply(Vec2{ rect.X + rect.Width, rect.Y + rect.Height }),
                              transform.Apply(Vec2{ rect.X, rect.Y + rect.Height }) };
    float left = corners[0].X, right = corners[0].X, top = corners[0].Y, bottom = corners[0].Y;
    for (const Vec2& corner : corners)
    {
        left = std::min(left, corner.X);
        right = std::max(right, corner.X);
        top = std::min(top, corner.Y);
        bottom = std::max(bottom, corner.Y);
    }
    return Rect{ left, top, right - left, bottom - top };
}

Color Lerp(const Color& a, const Color& b, float t)
{
    return Color{ a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t };
}

// The colour a gradient has at t, stops in order.
Color GradientAt(const std::vector<GradientStop>& stops, float t)
{
    if (stops.empty())
    {
        return Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    }
    if (t <= stops.front().Position)
    {
        return stops.front().Color;
    }
    for (size_t index = 1; index < stops.size(); ++index)
    {
        if (t <= stops[index].Position)
        {
            const float span = stops[index].Position - stops[index - 1].Position;
            const float along = span > 1e-6f ? (t - stops[index - 1].Position) / span : 1.0f;
            return Lerp(stops[index - 1].Color, stops[index].Color, along);
        }
    }
    return stops.back().Color;
}

// Stops in order, and a transparent stop given its neighbour's colour, so a
// gradient fading to nothing fades through its own hue rather than through
// black.
std::vector<GradientStop> TidyStops(std::vector<GradientStop> stops)
{
    std::stable_sort(stops.begin(), stops.end(),
                     [](const GradientStop& a, const GradientStop& b) { return a.Position < b.Position; });
    for (size_t index = 0; index < stops.size(); ++index)
    {
        if (stops[index].Color.A > 0.0f)
        {
            continue;
        }
        const GradientStop* neighbour = nullptr;
        if (index + 1 < stops.size() && stops[index + 1].Color.A > 0.0f)
        {
            neighbour = &stops[index + 1];
        }
        else if (index > 0 && stops[index - 1].Color.A > 0.0f)
        {
            neighbour = &stops[index - 1];
        }
        if (neighbour != nullptr)
        {
            stops[index].Color = Color{ neighbour->Color.R, neighbour->Color.G, neighbour->Color.B, 0.0f };
        }
    }
    return stops;
}

CornerRadii Shrink(const CornerRadii& radii, float by)
{
    return CornerRadii{ std::max(radii.TopLeft - by, 0.0f), std::max(radii.TopRight - by, 0.0f),
                        std::max(radii.BottomRight - by, 0.0f), std::max(radii.BottomLeft - by, 0.0f) };
}

Rect Deflate(const Rect& rect, float by)
{
    return Rect{ rect.X + by, rect.Y + by, std::max(rect.Width - by * 2.0f, 0.0f),
                 std::max(rect.Height - by * 2.0f, 0.0f) };
}

// What each quad draws; material.hlsli names the same numbers.
constexpr float ModePlain = 0.0f;
constexpr float ModeTexture = 1.0f;
constexpr float ModePremultiplied = 3.0f;
constexpr float ModeLinearGradient = 5.0f;
constexpr float ModeRadialGradient = 6.0f;
constexpr float ModeShadow = 7.0f;
constexpr float ModeInsetShadow = 8.0f;
constexpr float ModeTiled = 9.0f;
constexpr float ModeBackdrop = 10.0f;

// Room past a shape's edge for its anti-aliased rim.
constexpr float EdgePadding = 1.5f;

} // namespace

void DrawList::Clear()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_Batches.clear();
    m_ClipStack.clear();
    m_TransformStack.clear();
    m_OpacityStack.clear();
    m_CurrentMaterial = MaterialId{};
}

void DrawList::SetMaterial(MaterialId material)
{
    m_CurrentMaterial = material;
}

size_t DrawList::GetCommandCount() const
{
    return m_Indices.size() / 6;
}

Rect DrawList::PixelClip() const
{
    if (!m_ClipStack.empty())
    {
        return m_ClipStack.back();
    }
    return m_Viewport;
}

Rect DrawList::GetClip() const
{
    const Rect clip = PixelClip();
    const float inverse = m_Scale > 0.0f ? 1.0f / m_Scale : 1.0f;
    return Rect{ clip.X * inverse, clip.Y * inverse, clip.Width * inverse, clip.Height * inverse };
}

void DrawList::PushClip(const Rect& bounds)
{
    const Rect pixels{ bounds.X * m_Scale, bounds.Y * m_Scale, bounds.Width * m_Scale, bounds.Height * m_Scale };

    // A clip is a scissor rectangle, which cannot turn; under a transform it
    // is the box around where the rectangle ends up.
    const Rect placed = m_TransformStack.empty() ? pixels : TransformedBounds(m_TransformStack.back(), pixels);
    m_ClipStack.push_back(IntersectRect(PixelClip(), placed));
}

void DrawList::PopClip()
{
    if (!m_ClipStack.empty())
    {
        m_ClipStack.pop_back();
    }
}

void DrawList::PushTransform(const Affine2D& transform)
{
    // Given in units; kept in pixels, where the vertices are.
    const Affine2D pixels = Affine2D::Scaling(Vec2{ m_Scale, m_Scale }) * transform *
                            Affine2D::Scaling(Vec2{ 1.0f / m_Scale, 1.0f / m_Scale });
    m_TransformStack.push_back(m_TransformStack.empty() ? pixels : m_TransformStack.back() * pixels);
}

void DrawList::PopTransform()
{
    if (!m_TransformStack.empty())
    {
        m_TransformStack.pop_back();
    }
}

Affine2D DrawList::GetTransform() const
{
    if (m_TransformStack.empty())
    {
        return Affine2D{};
    }
    return Affine2D::Scaling(Vec2{ 1.0f / m_Scale, 1.0f / m_Scale }) * m_TransformStack.back() *
           Affine2D::Scaling(Vec2{ m_Scale, m_Scale });
}

void DrawList::PushOpacity(float opacity)
{
    m_OpacityStack.push_back(GetOpacity() * std::clamp(opacity, 0.0f, 1.0f));
}

void DrawList::PopOpacity()
{
    if (!m_OpacityStack.empty())
    {
        m_OpacityStack.pop_back();
    }
}

float DrawList::GetOpacity() const
{
    return m_OpacityStack.empty() ? 1.0f : m_OpacityStack.back();
}

void DrawList::SpreadUv(Quad& quad, const Rect& uv)
{
    // UVs follow the unpadded rectangle, so padding does not skew sampling.
    const float halfWidth = quad.Bounds.Width * 0.5f;
    const float halfHeight = quad.Bounds.Height * 0.5f;
    const float uScale = halfWidth > 0.0f ? (halfWidth + quad.Padding) / halfWidth : 1.0f;
    const float vScale = halfHeight > 0.0f ? (halfHeight + quad.Padding) / halfHeight : 1.0f;
    const float uCenter = uv.X + uv.Width * 0.5f;
    const float vCenter = uv.Y + uv.Height * 0.5f;
    const float uHalf = uv.Width * 0.5f * uScale;
    const float vHalf = uv.Height * 0.5f * vScale;

    const float u[4] = { uCenter - uHalf, uCenter + uHalf, uCenter + uHalf, uCenter - uHalf };
    const float v[4] = { vCenter - vHalf, vCenter - vHalf, vCenter + vHalf, vCenter + vHalf };
    for (int corner = 0; corner < 4; ++corner)
    {
        quad.CornerU[corner] = u[corner];
        quad.CornerV[corner] = v[corner];
    }
}

void DrawList::ShapeQuad(Quad& quad, const Rect& unitBounds, const CornerRadii& radii, float scale)
{
    quad.Bounds = Rect{ unitBounds.X * scale, unitBounds.Y * scale, unitBounds.Width * scale,
                        unitBounds.Height * scale };
    quad.Radii[0] = std::max(radii.TopLeft, 0.0f) * scale;
    quad.Radii[1] = std::max(radii.TopRight, 0.0f) * scale;
    quad.Radii[2] = std::max(radii.BottomRight, 0.0f) * scale;
    quad.Radii[3] = std::max(radii.BottomLeft, 0.0f) * scale;
    quad.ShapeX = std::max(std::max(quad.Radii[0], quad.Radii[1]), std::max(quad.Radii[2], quad.Radii[3]));
    quad.Padding = EdgePadding;
}

void DrawList::Emit(Quad& quad)
{
    if (quad.Bounds.Width <= 0.0f || quad.Bounds.Height <= 0.0f)
    {
        return;
    }

    const float opacity = GetOpacity();
    bool visible = false;
    for (Color& color : quad.Colors)
    {
        color.A *= opacity;
        visible = visible || color.A > 0.0f;
    }
    if (quad.Mode == ModeBackdrop)
    {
        // Frosted glass shows even with no tint; its opacity rides in ShapeX.
        quad.ShapeX *= opacity;
        visible = quad.ShapeX > 0.0f;
    }
    if (!visible)
    {
        return;
    }

    const Rect clip = PixelClip();
    if (clip.Width <= 0.0f || clip.Height <= 0.0f)
    {
        return;
    }

    const float halfWidth = quad.Bounds.Width * 0.5f;
    const float halfHeight = quad.Bounds.Height * 0.5f;
    const float centerX = quad.Bounds.X + halfWidth;
    const float centerY = quad.Bounds.Y + halfHeight;
    const float extentX = halfWidth + quad.Padding;
    const float extentY = halfHeight + quad.Padding;

    const float localX[4] = { -extentX, extentX, extentX, -extentX };
    const float localY[4] = { -extentY, -extentY, extentY, extentY };

    const Affine2D* transform = m_TransformStack.empty() ? nullptr : &m_TransformStack.back();
    Vec2 positions[4];
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    for (int corner = 0; corner < 4; ++corner)
    {
        const Vec2 point{ centerX + localX[corner], centerY + localY[corner] };
        positions[corner] = transform != nullptr ? transform->Apply(point) : point;
        left = corner == 0 ? positions[corner].X : std::min(left, positions[corner].X);
        right = corner == 0 ? positions[corner].X : std::max(right, positions[corner].X);
        top = corner == 0 ? positions[corner].Y : std::min(top, positions[corner].Y);
        bottom = corner == 0 ? positions[corner].Y : std::max(bottom, positions[corner].Y);
    }

    // Cheap rejection before any vertex work. This is what keeps a long scroll
    // list cheap: rows outside the clip never reach the GPU.
    if (left > clip.X + clip.Width || right < clip.X || top > clip.Y + clip.Height || bottom < clip.Y)
    {
        return;
    }

    // Frosted glass samples a blur of the whole target at its own place on
    // screen, whatever transform carried it there.
    if (quad.Mode == ModeBackdrop)
    {
        const float width = std::max(m_Viewport.Width, 1.0f);
        const float height = std::max(m_Viewport.Height, 1.0f);
        for (int corner = 0; corner < 4; ++corner)
        {
            quad.CornerU[corner] = (positions[corner].X - m_Viewport.X) / width;
            quad.CornerV[corner] = (positions[corner].Y - m_Viewport.Y) / height;
        }
    }

    // A batch continues as long as the texture, material, and clip hold.
    // Anything else merges, which is why one theme can draw a screen in a few
    // calls. A material change is a pipeline change, so it must split, and a
    // backdrop stands alone because it interrupts the pass.
    const bool backdrop = quad.BackdropBlur > 0.0f;
    if (m_Batches.empty() || backdrop || m_Batches.back().BackdropBlur > 0.0f ||
        !SameTexture(m_Batches.back().Texture, quad.Texture) ||
        !SameMaterial(m_Batches.back().Material, m_CurrentMaterial) || !SameRect(m_Batches.back().Clip, clip))
    {
        Batch batch;
        batch.FirstIndex = static_cast<uint32_t>(m_Indices.size());
        batch.IndexCount = 0;
        batch.Texture = quad.Texture;
        batch.Material = m_CurrentMaterial;
        batch.Clip = clip;
        batch.BackdropBlur = quad.BackdropBlur;
        m_Batches.push_back(batch);
    }

    const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
    for (int corner = 0; corner < 4; ++corner)
    {
        Vertex vertex;
        vertex.PositionX = positions[corner].X;
        vertex.PositionY = positions[corner].Y;
        vertex.U = quad.CornerU[corner];
        vertex.V = quad.CornerV[corner];
        vertex.R = quad.Colors[corner].R;
        vertex.G = quad.Colors[corner].G;
        vertex.B = quad.Colors[corner].B;
        vertex.A = quad.Colors[corner].A;
        vertex.LocalX = localX[corner];
        vertex.LocalY = localY[corner];
        vertex.HalfWidth = halfWidth;
        vertex.HalfHeight = halfHeight;
        vertex.CornerRadius = quad.ShapeX;
        vertex.StrokeWidth = quad.Stroke;
        vertex.TextureMode = quad.Mode;
        vertex.ShapeFlags = quad.SkipShape ? 1.0f : 0.0f;
        vertex.RadiusTopLeft = quad.Radii[0];
        vertex.RadiusTopRight = quad.Radii[1];
        vertex.RadiusBottomRight = quad.Radii[2];
        vertex.RadiusBottomLeft = quad.Radii[3];
        m_Vertices.push_back(vertex);
    }

    const uint32_t indices[6] = { base + 0, base + 1, base + 2, base + 0, base + 2, base + 3 };
    m_Indices.insert(m_Indices.end(), indices, indices + 6);
    m_Batches.back().IndexCount += 6;
}

void DrawList::PushQuad(const Rect& unitBounds, const Rect& uv, Color color, float unitCornerRadius,
                        float unitStrokeWidth, float textureMode, TextureId texture, bool skipShape)
{
    if (unitBounds.Width <= 0.0f || unitBounds.Height <= 0.0f || color.A <= 0.0f)
    {
        return;
    }

    // Everything above here is in interface units; everything below is in
    // pixels. Radii and strokes scale with the shapes they belong to.
    Quad quad;
    ShapeQuad(quad, unitBounds, CornerRadii{ unitCornerRadius }, m_Scale);
    quad.Stroke = unitStrokeWidth * m_Scale;

    // The quad is grown past the shape so the outer half of a stroke and the
    // anti-aliased edge have somewhere to land. A glyph quad takes no padding,
    // because growing it would sample its neighbours in the atlas.
    quad.Padding = skipShape ? 0.0f : (quad.Stroke * 0.5f + EdgePadding);
    SpreadUv(quad, uv);
    for (Color& corner : quad.Colors)
    {
        corner = color;
    }
    quad.Mode = textureMode;
    quad.SkipShape = skipShape;
    quad.Texture = texture;
    Emit(quad);
}

void DrawList::DrawLine(Vec2 unitFrom, Vec2 unitTo, float unitThickness, Color color)
{
    if (unitThickness <= 0.0f || color.A <= 0.0f)
    {
        return;
    }

    const float dx = unitTo.X - unitFrom.X;
    const float dy = unitTo.Y - unitFrom.Y;
    const float length = std::sqrt(dx * dx + dy * dy);

    // A capsule: a rounded rectangle as long as the line plus its caps, as
    // thick as the line, turned to lie along it. The shape shader works in
    // the quad's own coordinates, so turning it is all it takes for the edge
    // to stay analytically anti-aliased at any angle.
    const Vec2 center{ (unitFrom.X + unitTo.X) * 0.5f, (unitFrom.Y + unitTo.Y) * 0.5f };
    const float degrees = length > 1e-6f ? std::atan2(dy, dx) * 180.0f / 3.14159265358979f : 0.0f;
    const Rect bounds{ center.X - length * 0.5f - unitThickness * 0.5f, center.Y - unitThickness * 0.5f,
                       length + unitThickness, unitThickness };

    PushTransform(Affine2D::About(center, degrees, Vec2{ 1.0f, 1.0f }));
    PushQuad(bounds, Rect{ 0, 0, 1, 1 }, color, unitThickness * 0.5f, 0.0f, ModePlain, TextureId{});
    PopTransform();
}

void DrawList::FillRect(const Rect& bounds, Color color)
{
    PushQuad(bounds, Rect{ 0, 0, 1, 1 }, color, 0.0f, 0.0f, ModePlain, TextureId{});
}

void DrawList::FillRoundedRect(const Rect& bounds, float cornerRadius, Color color)
{
    PushQuad(bounds, Rect{ 0, 0, 1, 1 }, color, cornerRadius, 0.0f, ModePlain, TextureId{});
}

void DrawList::FillCircle(Vec2 center, float radius, Color color)
{
    const Rect bounds{ center.X - radius, center.Y - radius, radius * 2.0f, radius * 2.0f };
    PushQuad(bounds, Rect{ 0, 0, 1, 1 }, color, radius, 0.0f, ModePlain, TextureId{});
}

void DrawList::StrokeRect(const Rect& bounds, float thickness, Color color, float cornerRadius)
{
    if (thickness <= 0.0f)
    {
        return;
    }
    PushQuad(bounds, Rect{ 0, 0, 1, 1 }, color, cornerRadius, thickness, ModePlain, TextureId{});
}

void DrawList::StrokeCircle(Vec2 center, float radius, float thickness, Color color)
{
    if (thickness <= 0.0f)
    {
        return;
    }
    const Rect bounds{ center.X - radius, center.Y - radius, radius * 2.0f, radius * 2.0f };
    PushQuad(bounds, Rect{ 0, 0, 1, 1 }, color, radius, thickness, ModePlain, TextureId{});
}

void DrawList::DrawTexture(const Rect& bounds, TextureId texture, Color tint)
{
    DrawTextureRegion(bounds, texture, Rect{ 0, 0, 1, 1 }, tint);
}

void DrawList::DrawPremultipliedTexture(const Rect& bounds, TextureId texture, float opacity)
{
    if (!texture.IsValid() || opacity <= 0.0f)
    {
        return;
    }

    // No distance-field edge and no padding: the texture already carries its
    // own anti-aliased edges, and padding would sample past its border.
    PushQuad(bounds, Rect{ 0, 0, 1, 1 }, Color{ 1.0f, 1.0f, 1.0f, opacity }, 0.0f, 0.0f, ModePremultiplied,
             texture, true);
}

void DrawList::DrawTextureRegion(const Rect& bounds, TextureId texture, const Rect& uv, Color tint)
{
    if (!texture.IsValid())
    {
        return;
    }
    PushQuad(bounds, uv, tint, 0.0f, 0.0f, ModeTexture, texture);
}

// --- styling ----------------------------------------------------------------------

void DrawList::FillRoundedRect(const Rect& bounds, const CornerRadii& radii, Color color)
{
    Quad quad;
    ShapeQuad(quad, bounds, radii, m_Scale);
    SpreadUv(quad, Rect{ 0, 0, 1, 1 });
    for (Color& corner : quad.Colors)
    {
        corner = color;
    }
    quad.Mode = ModePlain;
    Emit(quad);
}

void DrawList::StrokeRoundedRect(const Rect& bounds, const CornerRadii& radii, float thickness, Color color)
{
    if (thickness <= 0.0f)
    {
        return;
    }
    Quad quad;
    ShapeQuad(quad, bounds, radii, m_Scale);
    quad.Stroke = thickness * m_Scale;
    quad.Padding = quad.Stroke * 0.5f + EdgePadding;
    SpreadUv(quad, Rect{ 0, 0, 1, 1 });
    for (Color& corner : quad.Colors)
    {
        corner = color;
    }
    quad.Mode = ModePlain;
    Emit(quad);
}

void DrawList::DrawShadow(const Rect& bounds, const CornerRadii& radii, const Shadow& shadow)
{
    if (shadow.Color.A <= 0.0f || bounds.Width <= 0.0f || bounds.Height <= 0.0f)
    {
        return;
    }

    // A blur's radius is two standard deviations of the Gaussian, as in CSS.
    const float sigma = std::max(shadow.Blur, 0.0f) * 0.5f * m_Scale;

    Quad quad;
    if (shadow.Inset)
    {
        // Drawn over the box itself; the smaller copy it is cut from moves by
        // the offset, which the shader reads from the texture coordinates.
        ShapeQuad(quad, bounds, radii, m_Scale);
        quad.Mode = ModeInsetShadow;
        quad.ShapeX = std::max(shadow.Spread, 0.0f) * m_Scale;
        for (int corner = 0; corner < 4; ++corner)
        {
            quad.CornerU[corner] = shadow.Offset.X * m_Scale;
            quad.CornerV[corner] = shadow.Offset.Y * m_Scale;
        }
    }
    else
    {
        // The shape, moved and grown, then blurred across three deviations of
        // room: past that the shadow is too faint to see.
        const Rect shape{ bounds.X + shadow.Offset.X - shadow.Spread, bounds.Y + shadow.Offset.Y - shadow.Spread,
                          bounds.Width + shadow.Spread * 2.0f, bounds.Height + shadow.Spread * 2.0f };
        if (shape.Width <= 0.0f || shape.Height <= 0.0f)
        {
            return;
        }
        ShapeQuad(quad, shape,
                  CornerRadii{ std::max(radii.TopLeft + shadow.Spread, 0.0f),
                               std::max(radii.TopRight + shadow.Spread, 0.0f),
                               std::max(radii.BottomRight + shadow.Spread, 0.0f),
                               std::max(radii.BottomLeft + shadow.Spread, 0.0f) },
                  m_Scale);
        quad.Padding = sigma * 3.0f + EdgePadding;
        quad.Mode = ModeShadow;
    }

    for (Color& corner : quad.Colors)
    {
        corner = shadow.Color;
    }
    quad.Stroke = sigma;
    Emit(quad);
}

void DrawList::DrawFill(const Rect& bounds, const Fill& fill, const CornerRadii& radii)
{
    switch (fill.Kind)
    {
    case FillKind::None:
        return;

    case FillKind::Solid:
        FillRoundedRect(bounds, radii, fill.Color);
        return;

    case FillKind::Image:
        DrawImage(bounds, fill.Image, fill.Fit, fill.Color, radii, fill.Slice, fill.TileScale);
        return;

    case FillKind::LinearGradient:
    case FillKind::RadialGradient:
        break;
    }

    const std::vector<GradientStop> stops = TidyStops(fill.Stops);
    if (stops.empty() || bounds.Width <= 0.0f || bounds.Height <= 0.0f)
    {
        return;
    }

    Quad quad;
    ShapeQuad(quad, bounds, radii, m_Scale);
    const float halfWidth = quad.Bounds.Width * 0.5f;
    const float halfHeight = quad.Bounds.Height * 0.5f;
    const float extentX = halfWidth + quad.Padding;
    const float extentY = halfHeight + quad.Padding;
    const float cornerX[4] = { -extentX, extentX, extentX, -extentX };
    const float cornerY[4] = { -extentY, -extentY, extentY, extentY };

    if (fill.Kind == FillKind::LinearGradient)
    {
        // Along the direction, as far as the box reaches: its corners sit at
        // 0 and 1, as CSS has it.
        const float radians = fill.Angle * 3.14159265358979f / 180.0f;
        const float directionX = std::cos(radians);
        const float directionY = std::sin(radians);
        const float length =
            std::fabs(quad.Bounds.Width * directionX) + std::fabs(quad.Bounds.Height * directionY);
        float t[4];
        for (int corner = 0; corner < 4; ++corner)
        {
            t[corner] = length > 1e-6f
                            ? (cornerX[corner] * directionX + cornerY[corner] * directionY) / length + 0.5f
                            : 0.5f;
        }

        // Two stops at the ends need no texture: the colour between them is
        // linear, which the corners' colours interpolate exactly.
        const bool simple = stops.size() == 1 ||
                            (stops.size() == 2 && stops[0].Position <= 0.0f && stops[1].Position >= 1.0f);
        if (simple)
        {
            for (int corner = 0; corner < 4; ++corner)
            {
                const Color color =
                    stops.size() == 1 ? stops[0].Color : Lerp(stops[0].Color, stops[1].Color, t[corner]);
                quad.Colors[corner] = Color{ color.R * fill.Color.R, color.G * fill.Color.G,
                                             color.B * fill.Color.B, color.A * fill.Color.A };
            }
            quad.Mode = ModePlain;
            SpreadUv(quad, Rect{ 0, 0, 1, 1 });
            Emit(quad);
            return;
        }

        quad.Texture = m_Renderer != nullptr ? m_Renderer->GetGradientTexture(stops) : TextureId{};
        if (!quad.Texture.IsValid())
        {
            FillRoundedRect(bounds, radii, GradientAt(stops, 0.5f));
            return;
        }
        for (int corner = 0; corner < 4; ++corner)
        {
            quad.CornerU[corner] = t[corner];
            quad.CornerV[corner] = 0.5f;
            quad.Colors[corner] = fill.Color;
        }
        quad.Mode = ModeLinearGradient;
        Emit(quad);
        return;
    }

    // Radial: out from the centre, reaching the radius, or the farthest
    // corner when there is none.
    const float centerX = (fill.Center.X - 0.5f) * quad.Bounds.Width;
    const float centerY = (fill.Center.Y - 0.5f) * quad.Bounds.Height;
    float radius = fill.Radius * m_Scale;
    if (radius <= 0.0f)
    {
        for (int corner = 0; corner < 4; ++corner)
        {
            const float x = (corner == 0 || corner == 3 ? -halfWidth : halfWidth) - centerX;
            const float y = (corner < 2 ? -halfHeight : halfHeight) - centerY;
            radius = std::max(radius, std::sqrt(x * x + y * y));
        }
    }
    radius = std::max(radius, 1e-3f);

    quad.Texture = m_Renderer != nullptr ? m_Renderer->GetGradientTexture(stops) : TextureId{};
    if (!quad.Texture.IsValid())
    {
        FillRoundedRect(bounds, radii, GradientAt(stops, 0.5f));
        return;
    }
    for (int corner = 0; corner < 4; ++corner)
    {
        quad.CornerU[corner] = (cornerX[corner] - centerX) / radius;
        quad.CornerV[corner] = (cornerY[corner] - centerY) / radius;
        quad.Colors[corner] = fill.Color;
    }
    quad.Mode = ModeRadialGradient;
    Emit(quad);
}

void DrawList::DrawImage(const Rect& bounds, TextureId texture, ImageFit fit, Color tint, const CornerRadii& radii,
                         const Insets& slice, float tileScale)
{
    if (!texture.IsValid() || bounds.Width <= 0.0f || bounds.Height <= 0.0f)
    {
        return;
    }

    const Vec2 size = m_Renderer != nullptr ? m_Renderer->GetTextureSize(texture) : Vec2{};
    if (size.X <= 0.0f || size.Y <= 0.0f)
    {
        return;
    }

    auto Draw = [&](const Rect& where, const Rect& uv, const CornerRadii& corners, float mode, bool skipShape) {
        Quad quad;
        ShapeQuad(quad, where, corners, m_Scale);
        if (skipShape)
        {
            quad.Padding = 0.0f;
        }
        SpreadUv(quad, uv);
        for (Color& corner : quad.Colors)
        {
            corner = tint;
        }
        quad.Mode = mode;
        quad.SkipShape = skipShape;
        quad.Texture = texture;
        Emit(quad);
    };

    switch (fit)
    {
    case ImageFit::Stretch:
        Draw(bounds, Rect{ 0, 0, 1, 1 }, radii, ModeTexture, false);
        return;

    case ImageFit::Cover:
    {
        // Cropped to the box's shape, from the middle.
        const float boxAspect = bounds.Width / bounds.Height;
        const float imageAspect = size.X / size.Y;
        Rect uv{ 0, 0, 1, 1 };
        if (imageAspect > boxAspect)
        {
            uv.Width = boxAspect / imageAspect;
            uv.X = (1.0f - uv.Width) * 0.5f;
        }
        else
        {
            uv.Height = imageAspect / boxAspect;
            uv.Y = (1.0f - uv.Height) * 0.5f;
        }
        Draw(bounds, uv, radii, ModeTexture, false);
        return;
    }

    case ImageFit::Contain:
    {
        const float scale = std::min(bounds.Width / size.X, bounds.Height / size.Y);
        const Rect where{ bounds.X + (bounds.Width - size.X * scale) * 0.5f,
                          bounds.Y + (bounds.Height - size.Y * scale) * 0.5f, size.X * scale, size.Y * scale };
        Draw(where, Rect{ 0, 0, 1, 1 }, radii, ModeTexture, false);
        return;
    }

    case ImageFit::Center:
    {
        // At one unit a pixel, cropped where it is larger than the box.
        const float width = std::min(size.X, bounds.Width);
        const float height = std::min(size.Y, bounds.Height);
        const Rect where{ bounds.X + (bounds.Width - width) * 0.5f, bounds.Y + (bounds.Height - height) * 0.5f,
                          width, height };
        const Rect uv{ (1.0f - width / size.X) * 0.5f, (1.0f - height / size.Y) * 0.5f, width / size.X,
                       height / size.Y };
        Draw(where, uv, radii, ModeTexture, false);
        return;
    }

    case ImageFit::Tile:
    {
        // Coordinates that run past 1, one repeat per tile, from the box's top
        // left; the shader wraps them.
        const float tile = std::max(tileScale, 1e-3f);
        const Rect uv{ 0.0f, 0.0f, bounds.Width / (size.X * tile), bounds.Height / (size.Y * tile) };
        Draw(bounds, uv, radii, ModeTiled, false);
        return;
    }

    case ImageFit::NineSlice:
    {
        // Corners at one unit a pixel, shrunk together if the box is too small
        // for them; edges stretched one way, the middle both.
        const float left = std::max(slice.Left, 0.0f), right = std::max(slice.Right, 0.0f);
        const float top = std::max(slice.Top, 0.0f), bottom = std::max(slice.Bottom, 0.0f);
        const float fitX = left + right > bounds.Width ? bounds.Width / (left + right) : 1.0f;
        const float fitY = top + bottom > bounds.Height ? bounds.Height / (top + bottom) : 1.0f;
        const float fitBoth = std::min(fitX, fitY);

        const float xs[4] = { bounds.X, bounds.X + left * fitBoth, bounds.X + bounds.Width - right * fitBoth,
                              bounds.X + bounds.Width };
        const float ys[4] = { bounds.Y, bounds.Y + top * fitBoth, bounds.Y + bounds.Height - bottom * fitBoth,
                              bounds.Y + bounds.Height };
        const float us[4] = { 0.0f, left / size.X, 1.0f - right / size.X, 1.0f };
        const float vs[4] = { 0.0f, top / size.Y, 1.0f - bottom / size.Y, 1.0f };

        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                const Rect where{ xs[column], ys[row], xs[column + 1] - xs[column], ys[row + 1] - ys[row] };
                const Rect uv{ us[column], vs[row], us[column + 1] - us[column], vs[row + 1] - vs[row] };
                Draw(where, uv, CornerRadii{}, ModeTexture, true);
            }
        }
        return;
    }
    }
}

void DrawList::BackdropBlur(const Rect& bounds, float radius, const CornerRadii& radii, Color tint)
{
    if (radius <= 0.0f)
    {
        return;
    }
    Quad quad;
    ShapeQuad(quad, bounds, radii, m_Scale);
    for (Color& corner : quad.Colors)
    {
        corner = tint;
    }
    quad.Mode = ModeBackdrop;
    quad.ShapeX = 1.0f; // the opacity, which Emit multiplies
    quad.BackdropBlur = radius * m_Scale;
    Emit(quad);
}

void DrawList::DrawBox(const Rect& bounds, const BoxStyle& style)
{
    const bool nudged = style.Scale != 1.0f || style.Offset.X != 0.0f || style.Offset.Y != 0.0f;
    if (nudged)
    {
        const Vec2 center{ bounds.X + bounds.Width * 0.5f, bounds.Y + bounds.Height * 0.5f };
        PushTransform(Affine2D::Translation(style.Offset) *
                      Affine2D::About(center, 0.0f, Vec2{ style.Scale, style.Scale }));
    }
    PushOpacity(style.Opacity);
    DrawBoxShape(bounds, style);
    PopOpacity();
    if (nudged)
    {
        PopTransform();
    }
}

void DrawList::DrawBoxShape(const Rect& bounds, const BoxStyle& style)
{
    for (const Shadow& shadow : style.Shadows)
    {
        if (!shadow.Inset)
        {
            DrawShadow(bounds, style.Radius, shadow);
        }
    }

    if (style.BackdropBlur > 0.0f)
    {
        BackdropBlur(bounds, style.BackdropBlur, style.Radius);
    }

    DrawFill(bounds, style.Background, style.Radius);

    for (const Shadow& shadow : style.Shadows)
    {
        if (shadow.Inset)
        {
            DrawShadow(bounds, style.Radius, shadow);
        }
    }

    // Inside the edge, so a border never changes the box's size.
    if (style.BorderWidth > 0.0f && style.BorderColor.A > 0.0f)
    {
        const float half = style.BorderWidth * 0.5f;
        StrokeRoundedRect(Deflate(bounds, half), Shrink(style.Radius, half), style.BorderWidth, style.BorderColor);
    }
}

} // namespace opane
