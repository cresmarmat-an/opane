// The styling value types: radii, transforms, fills, and styles.

#include <opane/opane.h>

#include <algorithm>
#include <cmath>

namespace opane
{

float CornerRadii::Largest() const
{
    return std::max(std::max(TopLeft, TopRight), std::max(BottomRight, BottomLeft));
}

// --- Affine2D -----------------------------------------------------------------

Affine2D Affine2D::Translation(Vec2 offset)
{
    Affine2D result;
    result.E = offset.X;
    result.F = offset.Y;
    return result;
}

Affine2D Affine2D::Rotation(float degrees)
{
    const float radians = degrees * 3.14159265358979f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Affine2D result;
    result.A = cosine;
    result.B = sine;
    result.C = -sine;
    result.D = cosine;
    return result;
}

Affine2D Affine2D::Scaling(Vec2 scale)
{
    Affine2D result;
    result.A = scale.X;
    result.D = scale.Y;
    return result;
}

Affine2D Affine2D::About(Vec2 pivot, float degrees, Vec2 scale)
{
    return Translation(pivot) * Rotation(degrees) * Scaling(scale) * Translation(Vec2{ -pivot.X, -pivot.Y });
}

Affine2D Affine2D::operator*(const Affine2D& other) const
{
    Affine2D result;
    result.A = A * other.A + C * other.B;
    result.B = B * other.A + D * other.B;
    result.C = A * other.C + C * other.D;
    result.D = B * other.C + D * other.D;
    result.E = A * other.E + C * other.F + E;
    result.F = B * other.E + D * other.F + F;
    return result;
}

Vec2 Affine2D::Apply(Vec2 point) const
{
    return Vec2{ A * point.X + C * point.Y + E, B * point.X + D * point.Y + F };
}

Affine2D Affine2D::Inverse() const
{
    const float determinant = A * D - B * C;
    if (std::fabs(determinant) < 1e-12f)
    {
        // Squashed flat: nothing maps back, so the point goes nowhere useful
        // rather than to infinity.
        return Scaling(Vec2{ 0.0f, 0.0f });
    }
    const float inverse = 1.0f / determinant;
    Affine2D result;
    result.A = D * inverse;
    result.B = -B * inverse;
    result.C = -C * inverse;
    result.D = A * inverse;
    result.E = -(result.A * E + result.C * F);
    result.F = -(result.B * E + result.D * F);
    return result;
}

bool Affine2D::IsIdentity() const
{
    return A == 1.0f && B == 0.0f && C == 0.0f && D == 1.0f && E == 0.0f && F == 0.0f;
}

// --- Fill ----------------------------------------------------------------------

Fill Fill::Solid(struct Color color)
{
    Fill fill;
    fill.Kind = FillKind::Solid;
    fill.Color = color;
    return fill;
}

Fill Fill::Linear(struct Color from, struct Color to, float angle)
{
    return Linear({ GradientStop{ 0.0f, from }, GradientStop{ 1.0f, to } }, angle);
}

Fill Fill::Linear(std::vector<GradientStop> stops, float angle)
{
    Fill fill;
    fill.Kind = FillKind::LinearGradient;
    fill.Stops = std::move(stops);
    fill.Angle = angle;
    return fill;
}

Fill Fill::Radial(struct Color inner, struct Color outer, Vec2 center, float radius)
{
    Fill fill;
    fill.Kind = FillKind::RadialGradient;
    fill.Stops = { GradientStop{ 0.0f, inner }, GradientStop{ 1.0f, outer } };
    fill.Center = center;
    fill.Radius = radius;
    return fill;
}

Fill Fill::FromImage(TextureId image, ImageFit fit, struct Color tint)
{
    Fill fill;
    fill.Kind = FillKind::Image;
    fill.Image = image;
    fill.Fit = fit;
    fill.Color = tint;
    return fill;
}

Fill Fill::NineSliced(TextureId image, Insets slice, struct Color tint)
{
    Fill fill = FromImage(image, ImageFit::NineSlice, tint);
    fill.Slice = slice;
    return fill;
}

// --- styles ----------------------------------------------------------------------

bool BoxStyle::IsEmpty() const
{
    return Background.IsEmpty() && (BorderWidth <= 0.0f || BorderColor.A <= 0.0f) && Shadows.empty() &&
           BackdropBlur <= 0.0f && Opacity == 1.0f && TextColor.A <= 0.0f && Scale == 1.0f &&
           Offset.X == 0.0f && Offset.Y == 0.0f;
}

bool Style::IsEmpty() const
{
    return Normal.IsEmpty() && !Hovered && !Pressed && !Focused && !Selected && !Disabled;
}

const Style* Theme::FindStyle(const std::string& name) const
{
    for (const auto& named : NamedStyles)
    {
        if (named.first == name)
        {
            return &named.second;
        }
    }
    return nullptr;
}

bool ElementTransform::IsIdentity() const
{
    return Translate.X == 0.0f && Translate.Y == 0.0f && Scale.X == 1.0f && Scale.Y == 1.0f && Rotation == 0.0f;
}

} // namespace opane
