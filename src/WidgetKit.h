// Small drawing and arithmetic helpers shared by the built-in widgets.
// Not installed and not part of the public API.
//
// Everything here is written against <opane/opane.h> alone, so the widgets
// that use it stay what they have always been: code a user could have written.

#pragma once

#include <utility>

#include <opane/opane.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

namespace opane::kit
{

// Calls an element's callback through a copy of it. A callback may replace
// itself (a button whose click assigns a new OnClick, for example), which
// destroys the function while it runs; the copy keeps running instead.
//
// A callback that returns something returns it through here too; an empty one
// returns what its type makes by default.
template <typename Function, typename... Arguments>
auto Invoke(const Function& function, Arguments&&... arguments)
{
    using Result = decltype(function(std::forward<Arguments>(arguments)...));
    if (!function)
    {
        if constexpr (std::is_void_v<Result>)
        {
            return;
        }
        else
        {
            return Result{};
        }
    }
    const Function running = function;
    return running(std::forward<Arguments>(arguments)...);
}

inline Color Mix(const Color& a, const Color& b, float t)
{
    return Color{ a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t,
                  a.A + (b.A - a.A) * t };
}

inline Color WithAlpha(Color color, float alpha)
{
    color.A = alpha;
    return color;
}

// The text colour over an element's look: the look's TextColor, weighed
// against the fallback by its alpha, so a look that sets none keeps the
// theme's and one easing toward a colour passes smoothly through.
inline Color LookText(const Element& element, Color fallback)
{
    if (!element.HasAppearance())
    {
        return fallback;
    }
    const Color& wanted = element.GetCurrentLook().TextColor;
    return Color{ fallback.R + (wanted.R - fallback.R) * wanted.A, fallback.G + (wanted.G - fallback.G) * wanted.A,
                  fallback.B + (wanted.B - fallback.B) * wanted.A, fallback.A };
}

// A part of a widget (a slider's thumb, a checkbox's box) in the widget's
// state, from the theme's style for it. Null when the theme gives the part
// no style, and the widget draws its own.
inline const BoxStyle* PartLook(const Style& style, const Element& element, bool selected, bool pressed)
{
    if (style.IsEmpty())
    {
        return nullptr;
    }
    if (!element.IsEnabled() && style.Disabled)
    {
        return &*style.Disabled;
    }
    if (pressed && style.Pressed)
    {
        return &*style.Pressed;
    }
    if (selected && style.Selected)
    {
        return &*style.Selected;
    }
    if (element.IsHovered() && style.Hovered)
    {
        return &*style.Hovered;
    }
    if (element.IsFocused() && style.Focused)
    {
        return &*style.Focused;
    }
    return &style.Normal;
}

inline bool IsTransparent(const Color& color)
{
    return color.A <= 0.0f;
}

// Moves toward a target at a rate that is frame-rate independent, so hover and
// press animations feel the same at 60 and 144 Hz.
inline float Approach(float current, float target, float rate, float deltaSeconds)
{
    const float step = rate * deltaSeconds;
    if (current < target)
    {
        return std::min(target, current + step);
    }
    return std::max(target, current - step);
}

inline bool Contains(const Rect& rect, Vec2 point)
{
    return point.X >= rect.X && point.X < rect.X + rect.Width && point.Y >= rect.Y &&
           point.Y < rect.Y + rect.Height;
}

inline Rect Inset(const Rect& rect, float amount)
{
    return Rect{ rect.X + amount, rect.Y + amount, std::max(0.0f, rect.Width - amount * 2.0f),
                 std::max(0.0f, rect.Height - amount * 2.0f) };
}

inline Vec2 Center(const Rect& rect)
{
    return Vec2{ rect.X + rect.Width * 0.5f, rect.Y + rect.Height * 0.5f };
}

// The accent ring round a control that has the keyboard, drawn only when the
// keyboard is how it got there.
inline void DrawFocusRing(DrawList& drawList, const Element& element, const Rect& bounds, float radius)
{
    if (!element.IsFocusVisible())
    {
        return;
    }
    const Theme& theme = element.GetTheme();
    drawList.StrokeRect(Rect{ bounds.X - 2.0f, bounds.Y - 2.0f, bounds.Width + 4.0f, bounds.Height + 4.0f },
                        2.0f, theme.Accent, radius + 2.0f);
}

enum class Direction
{
    Down,
    Up,
    Right,
    Left
};

// A chevron pointing one way, centred on a point. Drawn from lines, not a
// glyph, so it does not depend on the font.
inline void DrawChevron(DrawList& drawList, Vec2 center, float size, Direction direction, Color color,
                        float thickness = 1.6f)
{
    const float half = size * 0.5f;
    const float depth = size * 0.28f;
    switch (direction)
    {
        case Direction::Down:
            drawList.DrawLine({ center.X - half, center.Y - depth }, { center.X, center.Y + depth }, thickness, color);
            drawList.DrawLine({ center.X, center.Y + depth }, { center.X + half, center.Y - depth }, thickness, color);
            break;
        case Direction::Up:
            drawList.DrawLine({ center.X - half, center.Y + depth }, { center.X, center.Y - depth }, thickness, color);
            drawList.DrawLine({ center.X, center.Y - depth }, { center.X + half, center.Y + depth }, thickness, color);
            break;
        case Direction::Right:
            drawList.DrawLine({ center.X - depth, center.Y - half }, { center.X + depth, center.Y }, thickness, color);
            drawList.DrawLine({ center.X + depth, center.Y }, { center.X - depth, center.Y + half }, thickness, color);
            break;
        case Direction::Left:
            drawList.DrawLine({ center.X + depth, center.Y - half }, { center.X - depth, center.Y }, thickness, color);
            drawList.DrawLine({ center.X - depth, center.Y }, { center.X + depth, center.Y + half }, thickness, color);
            break;
    }
}

inline void DrawCross(DrawList& drawList, Vec2 center, float size, Color color, float thickness = 1.5f)
{
    const float half = size * 0.5f;
    drawList.DrawLine({ center.X - half, center.Y - half }, { center.X + half, center.Y + half }, thickness, color);
    drawList.DrawLine({ center.X - half, center.Y + half }, { center.X + half, center.Y - half }, thickness, color);
}

inline void DrawTick(DrawList& drawList, Vec2 center, float size, Color color, float thickness = 1.8f)
{
    const Vec2 start{ center.X - size * 0.42f, center.Y + size * 0.02f };
    const Vec2 elbow{ center.X - size * 0.12f, center.Y + size * 0.32f };
    const Vec2 end{ center.X + size * 0.45f, center.Y - size * 0.30f };
    drawList.DrawLine(start, elbow, thickness, color);
    drawList.DrawLine(elbow, end, thickness, color);
}

// A floating surface: a soft shadow, then the fill and its border.
inline void DrawRaised(DrawList& drawList, const Theme& theme, const Rect& bounds, float radius)
{
    drawList.FillRoundedRect(Rect{ bounds.X - 1.0f, bounds.Y + 3.0f, bounds.Width + 2.0f, bounds.Height + 2.0f },
                             radius + 2.0f, Color{ 0.0f, 0.0f, 0.0f, 0.22f });
    drawList.FillRoundedRect(Rect{ bounds.X, bounds.Y + 1.0f, bounds.Width, bounds.Height }, radius,
                             Color{ 0.0f, 0.0f, 0.0f, 0.18f });
    drawList.FillRoundedRect(bounds, radius, theme.Surface);
    drawList.StrokeRect(bounds, theme.BorderWidth, theme.Border, radius);
}

// Text cut to fit a width, ending in "...". The measure is passed in, since
// measuring text is the element's own business. The cut lands on a character
// boundary, never inside a UTF-8 sequence.
template <typename Measure>
std::string Ellipsize(const std::string& text, float width, Measure measure)
{
    if (measure(text) <= width)
    {
        return text;
    }
    const std::string dots = "...";

    // Every character boundary, then a binary search over them.
    std::vector<size_t> cuts;
    for (size_t at = 1; at < text.size(); ++at)
    {
        if ((static_cast<unsigned char>(text[at]) & 0xC0) != 0x80)
        {
            cuts.push_back(at);
        }
    }

    size_t best = 0;
    size_t low = 0;
    size_t high = cuts.size();
    while (low < high)
    {
        const size_t middle = (low + high) / 2;
        if (measure(text.substr(0, cuts[middle]) + dots) <= width)
        {
            best = cuts[middle];
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return best == 0 ? dots : text.substr(0, best) + dots;
}

// The previous and next character boundaries in UTF-8.
inline size_t PreviousCharacter(const std::string& text, size_t offset)
{
    if (offset == 0)
    {
        return 0;
    }
    size_t at = offset - 1;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80)
    {
        --at;
    }
    return at;
}

inline size_t NextCharacter(const std::string& text, size_t offset)
{
    if (offset >= text.size())
    {
        return text.size();
    }
    size_t at = offset + 1;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80)
    {
        ++at;
    }
    return at;
}

inline bool IsWordCharacter(unsigned char c)
{
    return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
}

inline size_t PreviousWord(const std::string& text, size_t offset)
{
    size_t at = offset;
    while (at > 0 && !IsWordCharacter(static_cast<unsigned char>(text[at - 1])))
    {
        --at;
    }
    while (at > 0 && IsWordCharacter(static_cast<unsigned char>(text[at - 1])))
    {
        --at;
    }
    return at;
}

inline size_t NextWord(const std::string& text, size_t offset)
{
    size_t at = offset;
    while (at < text.size() && !IsWordCharacter(static_cast<unsigned char>(text[at])))
    {
        ++at;
    }
    while (at < text.size() && IsWordCharacter(static_cast<unsigned char>(text[at])))
    {
        ++at;
    }
    return at;
}

} // namespace opane::kit
