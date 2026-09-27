// The built-in widgets.
//
// These use only the public API in <opane/opane.h>: the same Element
// interface, DrawList, and Theme available to any program.

#include <opane/opane.h>

#include "WidgetKit.h"

#include <algorithm>
#include <cmath>

namespace opane
{
namespace
{

Color Mix(const Color& a, const Color& b, float t)
{
    return Color{ a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t,
                  a.A + (b.A - a.A) * t };
}

bool IsTransparent(const Color& color)
{
    return color.A <= 0.0f;
}

// Moves toward a target at a rate that is frame-rate independent, so hover and
// press animations feel the same at 60 and 144 Hz.
float Approach(float current, float target, float rate, float deltaSeconds)
{
    const float step = rate * deltaSeconds;
    if (current < target)
    {
        return std::min(target, current + step);
    }
    return std::max(target, current - step);
}

} // namespace

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

const Style* Panel::GetDefaultAppearance(const Theme& theme) const
{
    // Only a panel that would show the theme's surface takes the theme's look.
    return UseThemeSurface && IsTransparent(BackgroundColor) ? &theme.Styles.Panel : nullptr;
}

void Panel::Paint(DrawList& drawList)
{
    if (HasAppearance())
    {
        return;
    }

    const Theme& theme = GetTheme();
    const float radius = CornerRadius >= 0.0f ? CornerRadius : theme.CornerRadius;

    Color fill = BackgroundColor;
    if (IsTransparent(fill) && UseThemeSurface)
    {
        fill = theme.Surface;
    }

    if (!IsTransparent(fill))
    {
        drawList.FillRoundedRect(GetBounds(), radius, fill);
    }

    if (DrawBorder && theme.BorderWidth > 0.0f)
    {
        drawList.StrokeRect(GetBounds(), theme.BorderWidth, theme.Border, radius);
    }
}

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

Vec2 Label::Measure(Vec2 available)
{
    // A label defaults to hugging its text, but an explicit Size still wins.
    const Vec2 requested = Size.Resolve(available);
    if (requested.X > 0.0f && requested.Y > 0.0f)
    {
        return requested;
    }

    const Vec2 text = MeasureText(Text, Font);
    return Vec2{ requested.X > 0.0f ? requested.X : text.X + Padding * 2.0f,
                 requested.Y > 0.0f ? requested.Y : text.Y + Padding * 2.0f };
}

void Label::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();

    Color color = TextColor;
    if (IsTransparent(color))
    {
        color = Muted ? theme.TextMuted : theme.Text;
    }

    drawList.DrawTextInRect(Text, GetContentBounds(), Font.IsValid() ? Font : theme.Font, color, Align);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

Button::Button()
{
    Size = Size2::FromOffset(160.0f, 40.0f);
    Padding = 12.0f;
    Focusable = true;
}

Vec2 Button::Measure(Vec2 available)
{
    const Vec2 requested = Size.Resolve(available);
    if (requested.X > 0.0f)
    {
        return requested;
    }

    const Vec2 text = MeasureText(Text);
    return Vec2{ text.X + Padding * 2.0f, requested.Y > 0.0f ? requested.Y : text.Y + Padding };
}

const Style* Button::GetDefaultAppearance(const Theme& theme) const
{
    return Accent ? &theme.Styles.AccentButton : &theme.Styles.Button;
}

void Button::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();

    // A look draws the box, with whatever hover and press it describes.
    if (HasAppearance())
    {
        kit::DrawFocusRing(drawList, *this, bounds, GetCurrentLook().Radius.Largest());
        drawList.DrawTextInRect(Text, bounds, theme.Font, kit::LookText(*this, theme.Text), TextAlign::Center);
        return;
    }

    const Color base = Accent ? theme.Accent : theme.Surface;
    const Color hovered = Accent ? theme.AccentHovered : theme.SurfaceHovered;
    const Color pressed = Accent ? theme.Accent : theme.SurfacePressed;

    Color fill = Mix(base, hovered, m_HoverAmount);
    fill = Mix(fill, pressed, m_PressAmount);

    // A pressed button sinks by a pixel. Small, but it is the difference
    // between a control that responds and one that merely changes color.
    const float sink = m_PressAmount * 1.0f;
    const Rect drawBounds{ bounds.X, bounds.Y + sink, bounds.Width, bounds.Height };

    drawList.FillRoundedRect(drawBounds, theme.CornerRadius, fill);
    drawList.StrokeRect(drawBounds, theme.BorderWidth, theme.Border, theme.CornerRadius);

    // A ring only when the keyboard brought focus here: after a click it
    // would say nothing the click did not.
    kit::DrawFocusRing(drawList, *this, drawBounds, theme.CornerRadius);

    drawList.DrawTextInRect(Text, drawBounds, theme.Font, theme.Text, TextAlign::Center);
}

void Button::OnUpdate(float deltaSeconds)
{
    m_HoverAmount = Approach(m_HoverAmount, IsHovered() ? 1.0f : 0.0f, 8.0f, deltaSeconds);
    m_PressAmount = Approach(m_PressAmount, IsPressed() ? 1.0f : 0.0f, 16.0f, deltaSeconds);
}

void Button::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerEnter:
            if (OnHoverStart)
            {
                kit::Invoke(OnHoverStart);
            }
            break;

        case EventType::PointerLeave:
            if (OnHoverEnd)
            {
                kit::Invoke(OnHoverEnd);
            }
            break;

        case EventType::PointerDown:
            event.Handled = true;
            break;

        case EventType::PointerUp:
        {
            // A click only counts when the release lands inside the button, so
            // dragging off it cancels the press. Only the primary button
            // clicks.
            if (event.Button == MouseButton::Left && HitTest(event.Position) && OnClick)
            {
                kit::Invoke(OnClick);
            }
            event.Handled = true;
            break;
        }

        case EventType::KeyDown:
            // A held key does not click again and again.
            if ((event.KeyCode == Key::Space || event.KeyCode == Key::Enter) && !event.Repeat)
            {
                if (OnClick)
                {
                    kit::Invoke(OnClick);
                }
                event.Handled = true;
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------

Checkbox::Checkbox()
{
    Size = Size2::FromOffset(180.0f, 28.0f);
    Focusable = true;
}

Vec2 Checkbox::Measure(Vec2 available)
{
    const Vec2 requested = Size.Resolve(available);
    if (requested.X > 0.0f)
    {
        return requested;
    }

    const Vec2 text = MeasureText(Text);
    return Vec2{ text.X + 34.0f, std::max(text.Y, 20.0f) };
}

void Checkbox::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();

    const float boxSize = 18.0f;
    const Rect box{ bounds.X, bounds.Y + (bounds.Height - boxSize) * 0.5f, boxSize, boxSize };

    Color tick{ 1.0f, 1.0f, 1.0f, 0.97f };
    if (const BoxStyle* part = kit::PartLook(theme.Styles.CheckboxBox, *this, Checked, IsPressed()))
    {
        drawList.DrawBox(box, *part);
        tick = kit::Mix(tick, part->TextColor, part->TextColor.A);
        tick.A = 0.97f;
    }
    else
    {
        const Color fill = Checked ? (IsHovered() ? theme.AccentHovered : theme.Accent)
                                   : (IsHovered() ? theme.SurfaceHovered : theme.Surface);
        drawList.FillRoundedRect(box, 5.0f, fill);
        drawList.StrokeRect(box, theme.BorderWidth, theme.Border, 5.0f);
    }
    kit::DrawFocusRing(drawList, *this, box, 5.0f);

    if (Checked)
    {
        // Drawn from lines rather than a glyph, so the mark does not depend on
        // the font having a check character.
        kit::DrawTick(drawList, kit::Center(box), boxSize * 0.62f, tick, 2.0f);
    }

    const Rect textBounds{ bounds.X + boxSize + 10.0f, bounds.Y, bounds.Width - boxSize - 10.0f,
                           bounds.Height };
    drawList.DrawTextInRect(Text, textBounds, theme.Font, kit::LookText(*this, theme.Text), TextAlign::Left);
}

void Checkbox::OnEvent(Event& event)
{
    const bool clicked =
        event.Type == EventType::PointerUp && event.Button == MouseButton::Left && HitTest(event.Position);
    const bool pressed = event.Type == EventType::KeyDown && event.KeyCode == Key::Space && !event.Repeat;

    if (clicked || pressed)
    {
        Checked = !Checked;
        if (OnChanged)
        {
            kit::Invoke(OnChanged, Checked);
        }
        event.Handled = true;
    }
    else if (event.Type == EventType::PointerDown || event.Type == EventType::PointerUp)
    {
        event.Handled = true;
    }
}

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------

Image::Image()
{
    // Sized by its texture unless told otherwise.
    Size = Size2{};
    Interactive = false;
}

Vec2 Image::Measure(Vec2 available)
{
    // An explicit Size wins, as it does for every widget. An axis left at zero
    // follows the texture, keeping its aspect ratio when the other axis was
    // given, so setting only a width scales the image rather than squashing it.
    const Vec2 requested = Size.Resolve(available);
    const Vec2 natural = GetApp().GetTextureSize(Texture);

    if (requested.X > 0.0f && requested.Y > 0.0f)
    {
        return requested;
    }
    if (natural.X <= 0.0f || natural.Y <= 0.0f)
    {
        return requested;
    }
    if (requested.X > 0.0f)
    {
        return Vec2{ requested.X, requested.X * natural.Y / natural.X };
    }
    if (requested.Y > 0.0f)
    {
        return Vec2{ requested.Y * natural.X / natural.Y, requested.Y };
    }
    return natural;
}

void Image::Paint(DrawList& drawList)
{
    const Rect bounds = GetBounds();
    const Vec2 natural = GetApp().GetTextureSize(Texture);

    if (!Texture.IsValid() || natural.X <= 0.0f || natural.Y <= 0.0f || bounds.Width <= 0.0f ||
        bounds.Height <= 0.0f)
    {
        return;
    }

    Rect target = bounds;
    Rect uv{ 0.0f, 0.0f, 1.0f, 1.0f };

    const float scaleX = bounds.Width / natural.X;
    const float scaleY = bounds.Height / natural.Y;

    switch (Fit)
    {
        case ImageFit::Stretch:
            break;

        case ImageFit::Contain:
        {
            // The smaller scale keeps the whole image inside, with the spare
            // space split evenly on the long side.
            const float scale = std::min(scaleX, scaleY);
            target.Width = natural.X * scale;
            target.Height = natural.Y * scale;
            target.X = bounds.X + (bounds.Width - target.Width) * 0.5f;
            target.Y = bounds.Y + (bounds.Height - target.Height) * 0.5f;
            break;
        }

        case ImageFit::Cover:
        {
            // The larger scale fills the element; the overhang is cropped by
            // narrowing the texture coordinates rather than by clipping, so
            // the image stays in the same batch as its neighbours.
            const float scale = std::max(scaleX, scaleY);
            const float visibleWidth = bounds.Width / (natural.X * scale);
            const float visibleHeight = bounds.Height / (natural.Y * scale);
            uv = Rect{ (1.0f - visibleWidth) * 0.5f, (1.0f - visibleHeight) * 0.5f, visibleWidth,
                       visibleHeight };
            break;
        }

        case ImageFit::Center:
        {
            const float width = std::min(natural.X, bounds.Width);
            const float height = std::min(natural.Y, bounds.Height);
            target = Rect{ bounds.X + (bounds.Width - width) * 0.5f, bounds.Y + (bounds.Height - height) * 0.5f,
                           width, height };
            uv = Rect{ (1.0f - width / natural.X) * 0.5f, (1.0f - height / natural.Y) * 0.5f,
                       width / natural.X, height / natural.Y };
            break;
        }
    }

    drawList.DrawTextureRegion(target, Texture, uv, Tint);
}

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

Viewport::Viewport()
{
    Size = Size2::Fill();
}

Viewport::~Viewport()
{
    ClearWorld();
}

void Viewport::SetWorld(const WorldHooks& hooks)
{
    ClearWorld();
    if (!hooks.IsValid())
    {
        LogMessage(LogLevel::Warning, "ui", "Viewport::SetWorld was given an incomplete world; ignoring it.");
        return;
    }
    m_World = hooks;
}

void Viewport::ClearWorld()
{
    // The texture was wrapped here, so it is released here; the world's own
    // target is untouched, since wrapping never takes ownership.
    if (m_World.IsValid() && Texture.IsValid())
    {
        App app = GetApp();
        if (app.IsValid())
        {
            app.DestroyTexture(Texture);
        }
        Texture = TextureId{};
    }
    m_World = WorldHooks{};
    m_WorldTarget = nullptr;
    m_WorldWidth = 0;
    m_WorldHeight = 0;
}

void Viewport::OnUpdate(float deltaSeconds)
{
    if (!m_World.IsValid())
    {
        return;
    }

    App app = GetApp();
    const Rect bounds = GetBounds();

    // The world draws at the pixel size the element covers, so at 150% it
    // renders at the higher resolution instead of being magnified.
    const float scale = app.IsValid() ? app.GetUiScale() : 1.0f;
    const int width = static_cast<int>(std::lround(bounds.Width * scale));
    const int height = static_cast<int>(std::lround(bounds.Height * scale));
    if (!app.IsValid() || width <= 0 || height <= 0 || !Visible)
    {
        return;
    }

    // The world draws at exactly the element's size, so its image is never
    // stretched and its pixels map one to one.
    if (width != m_WorldWidth || height != m_WorldHeight)
    {
        m_World.SetRenderSize(m_World.Object, width, height);
        m_WorldWidth = width;
        m_WorldHeight = height;
    }

    void* target = m_World.GetRenderTarget(m_World.Object);
    if (target != m_WorldTarget)
    {
        if (Texture.IsValid())
        {
            app.DestroyTexture(Texture);
        }
        Texture = target != nullptr ? app.WrapExternalTexture(target, width, height) : TextureId{};
        m_WorldTarget = target;
    }

    if (UpdatesWorld)
    {
        m_World.Update(m_World.Object, deltaSeconds);
    }
    m_World.Render(m_World.Object);
}

Vec2 Viewport::ToNormalized(Vec2 windowPoint) const
{
    const Rect bounds = GetBounds();
    if (bounds.Width <= 0.0f || bounds.Height <= 0.0f)
    {
        return Vec2{};
    }

    return Vec2{ std::clamp((windowPoint.X - bounds.X) / bounds.Width, 0.0f, 1.0f),
                 std::clamp((windowPoint.Y - bounds.Y) / bounds.Height, 0.0f, 1.0f) };
}

void Viewport::Paint(DrawList& drawList)
{
    const Rect bounds = GetBounds();

    if (Texture.IsValid())
    {
        drawList.DrawTexture(bounds, Texture);
    }
    else
    {
        drawList.FillRect(bounds, EmptyColor);
    }

    if (DrawBorder)
    {
        const Theme& theme = GetTheme();
        drawList.StrokeRect(bounds, theme.BorderWidth, theme.Border, 0.0f);
    }
}

void Viewport::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerDown:
        case EventType::PointerUp:
        case EventType::PointerMove:
        case EventType::Wheel:
        {
            if (OnViewEvent)
            {
                // The handler is given view coordinates rather than window
                // coordinates, so it never has to know where the viewport sits
                // in the layout.
                Event viewEvent = event;
                viewEvent.Position = ToNormalized(event.Position);
                kit::Invoke(OnViewEvent, viewEvent);
            }

            // Handled, so a click meant for the world does not also reach
            // whatever is behind the viewport.
            event.Handled = true;
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------

Slider::Slider()
{
    Size = Size2::FromOffset(220.0f, 28.0f);
    Focusable = true;
}

void Slider::SetValue(float value)
{
    const float low = std::min(Minimum, Maximum);
    const float high = std::max(Minimum, Maximum);
    float updated = std::clamp(value, low, high);

    // Snapped to the step from the minimum, so a slider stepping by 0.25
    // from 0 can only land on quarters.
    if (Step > 0.0f)
    {
        updated = low + std::round((updated - low) / Step) * Step;
        updated = std::clamp(updated, low, high);
    }

    if (updated != Value)
    {
        Value = updated;
        if (OnChanged)
        {
            kit::Invoke(OnChanged, Value);
        }
    }
}

Vec2 Slider::Measure(Vec2 available)
{
    return Size.Resolve(available);
}

void Slider::SetFromPointer(float pointerX)
{
    const Rect bounds = GetBounds();
    if (bounds.Width <= 0.0f)
    {
        return;
    }

    const float fraction = std::clamp((pointerX - bounds.X) / bounds.Width, 0.0f, 1.0f);
    SetValue(Minimum + (Maximum - Minimum) * fraction);
}

void Slider::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();

    const float trackHeight = 6.0f;
    const Rect track{ bounds.X, bounds.Y + (bounds.Height - trackHeight) * 0.5f, bounds.Width,
                      trackHeight };

    const float span = (Maximum - Minimum);
    const float fraction = span > 0.0f ? std::clamp((Value - Minimum) / span, 0.0f, 1.0f) : 0.0f;

    const Rect filled{ track.X, track.Y, track.Width * fraction, track.Height };
    if (const BoxStyle* part = kit::PartLook(theme.Styles.SliderTrack, *this, m_Dragging, m_Dragging))
    {
        drawList.DrawBox(track, *part);
    }
    else
    {
        drawList.FillRoundedRect(track, trackHeight * 0.5f, theme.SurfacePressed);
    }
    if (const BoxStyle* part = kit::PartLook(theme.Styles.SliderFill, *this, m_Dragging, m_Dragging))
    {
        drawList.DrawBox(filled, *part);
    }
    else
    {
        drawList.FillRoundedRect(filled, trackHeight * 0.5f, theme.Accent);
    }

    const float knobRadius = IsHovered() || m_Dragging ? 10.0f : 8.0f;
    const Vec2 knobCenter{ track.X + track.Width * fraction, bounds.Y + bounds.Height * 0.5f };

    if (const BoxStyle* part = kit::PartLook(theme.Styles.SliderThumb, *this, m_Dragging, m_Dragging))
    {
        drawList.DrawBox(Rect{ knobCenter.X - knobRadius, knobCenter.Y - knobRadius, knobRadius * 2.0f,
                               knobRadius * 2.0f },
                         *part);
    }
    else
    {
        drawList.FillCircle(knobCenter, knobRadius, theme.Text);
        drawList.StrokeCircle(knobCenter, knobRadius, theme.BorderWidth, theme.Border);
    }
    if (IsFocusVisible())
    {
        drawList.StrokeCircle(knobCenter, knobRadius + 3.0f, 2.0f, theme.Accent);
    }
}

void Slider::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerDown:
            if (event.Button == MouseButton::Left)
            {
                m_Dragging = true;
                SetFromPointer(event.Position.X);
            }
            event.Handled = true;
            break;

        case EventType::KeyDown:
        {
            const float range = std::abs(Maximum - Minimum);
            const float step = Step > 0.0f ? Step : range / 100.0f;
            const float direction = Maximum >= Minimum ? 1.0f : -1.0f;
            switch (event.KeyCode)
            {
                case Key::Left:
                case Key::Down:
                    SetValue(Value - step * direction);
                    event.Handled = true;
                    break;
                case Key::Right:
                case Key::Up:
                    SetValue(Value + step * direction);
                    event.Handled = true;
                    break;
                case Key::PageDown:
                    SetValue(Value - step * 10.0f * direction);
                    event.Handled = true;
                    break;
                case Key::PageUp:
                    SetValue(Value + step * 10.0f * direction);
                    event.Handled = true;
                    break;
                case Key::Home:
                    SetValue(Minimum);
                    event.Handled = true;
                    break;
                case Key::End:
                    SetValue(Maximum);
                    event.Handled = true;
                    break;
                default:
                    break;
            }
            break;
        }

        case EventType::FocusLost:
            m_Dragging = false;
            break;

        case EventType::PointerMove:
            // The tree keeps sending moves to the element that captured the
            // pointer, so a drag continues past the slider's own bounds.
            if (m_Dragging)
            {
                SetFromPointer(event.Position.X);
                event.Handled = true;
            }
            break;

        case EventType::PointerUp:
            m_Dragging = false;
            event.Handled = true;
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------

namespace
{

// Text is held as UTF-8 and edited by byte offset, so moving about has to step
// whole characters: a continuation byte is never a place the caret can sit.
bool IsContinuation(char byte)
{
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

size_t PreviousCharacter(const std::string& text, size_t offset)
{
    if (offset == 0)
    {
        return 0;
    }
    --offset;
    while (offset > 0 && IsContinuation(text[offset]))
    {
        --offset;
    }
    return offset;
}

size_t NextCharacter(const std::string& text, size_t offset)
{
    if (offset >= text.size())
    {
        return text.size();
    }
    ++offset;
    while (offset < text.size() && IsContinuation(text[offset]))
    {
        ++offset;
    }
    return offset;
}

size_t CountCharacters(const std::string& text)
{
    size_t count = 0;
    for (const char byte : text)
    {
        count += IsContinuation(byte) ? 0u : 1u;
    }
    return count;
}

// A single-line field flattens what is pasted into it rather than silently
// splitting it in two.
std::string SingleLine(const std::string& text)
{
    std::string flattened;
    flattened.reserve(text.size());
    for (const char byte : text)
    {
        flattened.push_back((byte == '\n' || byte == '\r' || byte == '\t') ? ' ' : byte);
    }
    return flattened;
}

constexpr float TextInputInset = 10.0f;
constexpr float CaretWidth = 1.5f;
constexpr float BlinkSeconds = 1.06f;
const char* const MaskCharacter = "\xE2\x80\xA2"; // U+2022 BULLET

} // namespace

TextInput::TextInput()
{
    Size = Size2{ Dim::FromScale(1.0f), Dim::FromOffset(36.0f) };
    Focusable = true;
    Cursor = CursorShape::Text;
}

void TextInput::Sanitize()
{
    m_Caret = std::min(m_Caret, Text.size());
    m_Anchor = std::min(m_Anchor, Text.size());
    while (m_Caret > 0 && m_Caret < Text.size() && IsContinuation(Text[m_Caret]))
    {
        --m_Caret;
    }
    while (m_Anchor > 0 && m_Anchor < Text.size() && IsContinuation(Text[m_Anchor]))
    {
        --m_Anchor;
    }
}

void TextInput::Remember(bool mergeable)
{
    // A burst of typing undoes as one step: an edit within a second of the
    // last, of the same kind, joins it rather than starting a new one.
    if (mergeable && !m_Undo.empty() && m_Clock - m_LastEdit < 1.0f)
    {
        m_LastEdit = m_Clock;
        return;
    }
    m_Undo.push_back(Snapshot{ Text, m_Caret });
    if (m_Undo.size() > 200)
    {
        m_Undo.erase(m_Undo.begin());
    }
    m_Redo.clear();
    m_LastEdit = mergeable ? m_Clock : -10.0f;
}

void TextInput::Changed()
{
    m_Blink = 0.0f;
    if (OnChanged)
    {
        kit::Invoke(OnChanged, Text);
    }
}

void TextInput::Undo()
{
    Sanitize();
    if (m_Undo.empty() || ReadOnly)
    {
        return;
    }
    m_Redo.push_back(Snapshot{ Text, m_Caret });
    Text = m_Undo.back().Text;
    m_Caret = m_Anchor = std::min(m_Undo.back().Caret, Text.size());
    m_Undo.pop_back();
    m_LastEdit = -10.0f;
    Changed();
}

void TextInput::Redo()
{
    Sanitize();
    if (m_Redo.empty() || ReadOnly)
    {
        return;
    }
    m_Undo.push_back(Snapshot{ Text, m_Caret });
    Text = m_Redo.back().Text;
    m_Caret = m_Anchor = std::min(m_Redo.back().Caret, Text.size());
    m_Redo.pop_back();
    m_LastEdit = -10.0f;
    Changed();
}

bool TextInput::WantsText() const
{
    return !ReadOnly;
}

std::string TextInput::Visible() const
{
    if (!Masked)
    {
        return Text;
    }

    std::string dots;
    const size_t characters = CountCharacters(Text);
    for (size_t index = 0; index < characters; ++index)
    {
        dots += MaskCharacter;
    }
    return dots;
}

Vec2 TextInput::Measure(Vec2 available)
{
    return Size.Resolve(available);
}

void TextInput::SetCaret(size_t offset)
{
    m_Caret = std::min(offset, Text.size());
    m_Anchor = m_Caret;
    m_Blink = 0.0f;
}

void TextInput::SelectAll()
{
    m_Anchor = 0;
    m_Caret = Text.size();
    m_Blink = 0.0f;
}

float TextInput::XAtOffset(size_t offset) const
{
    offset = std::min(offset, Text.size());

    // A masked field measures dots rather than letters, so the caret lands
    // where the dots are.
    if (!Masked)
    {
        return MeasureText(Text.substr(0, offset)).X;
    }

    std::string dots;
    const size_t characters = CountCharacters(Text.substr(0, offset));
    for (size_t index = 0; index < characters; ++index)
    {
        dots += MaskCharacter;
    }
    return MeasureText(dots).X;
}

size_t TextInput::OffsetAtX(float x) const
{
    const float wanted = x - (GetBounds().X + TextInputInset) + m_ScrollX;

    size_t best = 0;
    float bestDistance = std::abs(wanted);

    size_t offset = 0;
    while (offset < Text.size())
    {
        offset = NextCharacter(Text, offset);
        const float distance = std::abs(XAtOffset(offset) - wanted);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = offset;
        }
    }
    return best;
}

void TextInput::DeleteSelection()
{
    if (!HasSelection())
    {
        return;
    }

    const size_t from = std::min(m_Caret, m_Anchor);
    const size_t to = std::max(m_Caret, m_Anchor);
    Text.erase(from, to - from);
    m_Caret = from;
    m_Anchor = from;
}

void TextInput::Insert(const std::string& text)
{
    if (ReadOnly || text.empty())
    {
        return;
    }

    DeleteSelection();

    std::string addition = SingleLine(text);
    if (MaxLength > 0)
    {
        const size_t limit = static_cast<size_t>(MaxLength);
        const size_t used = CountCharacters(Text);
        if (used >= limit)
        {
            return;
        }

        // Trimmed by characters rather than by bytes, so an accented letter is
        // never cut in half.
        const size_t room = limit - used;
        size_t kept = 0;
        size_t offset = 0;
        while (offset < addition.size() && kept < room)
        {
            offset = NextCharacter(addition, offset);
            ++kept;
        }
        addition.resize(offset);
        if (addition.empty())
        {
            return;
        }
    }

    Text.insert(m_Caret, addition);
    m_Caret += addition.size();
    m_Anchor = m_Caret;
    m_Blink = 0.0f;

    if (OnChanged)
    {
        kit::Invoke(OnChanged, Text);
    }
}

void TextInput::MoveCaret(size_t offset, bool extend)
{
    m_Caret = std::min(offset, Text.size());
    if (!extend)
    {
        m_Anchor = m_Caret;
    }
    m_Blink = 0.0f;
}

const Style* TextInput::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.TextInput;
}

void TextInput::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    const bool focused = IsFocused();

    if (!HasAppearance())
    {
        drawList.FillRoundedRect(bounds, theme.CornerRadius, focused ? theme.SurfacePressed : theme.Surface);
        drawList.StrokeRect(bounds, focused ? 2.0f : theme.BorderWidth, focused ? theme.Accent : theme.Border,
                            theme.CornerRadius);
    }
    const Color textColor = kit::LookText(*this, theme.Text);

    const Rect inner{ bounds.X + TextInputInset, bounds.Y, bounds.Width - TextInputInset * 2.0f,
                      bounds.Height };
    const float lineHeight = GetLineHeight();
    const float textY = bounds.Y + (bounds.Height - lineHeight) * 0.5f;

    drawList.PushClip(inner);

    const std::string shown = Visible();
    if (shown.empty() && !Placeholder.empty())
    {
        drawList.DrawText(Placeholder, Vec2{ inner.X, textY }, theme.Font, theme.TextMuted);
    }

    if (focused && HasSelection())
    {
        const float from = XAtOffset(std::min(m_Caret, m_Anchor)) - m_ScrollX;
        const float to = XAtOffset(std::max(m_Caret, m_Anchor)) - m_ScrollX;
        const Color highlight{ theme.Accent.R, theme.Accent.G, theme.Accent.B, 0.35f };
        drawList.FillRect(Rect{ inner.X + from, textY, to - from, lineHeight }, highlight);
    }

    if (!shown.empty())
    {
        drawList.DrawText(shown, Vec2{ inner.X - m_ScrollX, textY }, theme.Font, textColor);
    }

    // The caret blinks on a one-second cycle, and one that has just moved is
    // always shown, so typing never hides it.
    if (focused && !ReadOnly && m_Blink < BlinkSeconds * 0.5f)
    {
        const float caretX = inner.X + XAtOffset(m_Caret) - m_ScrollX;
        drawList.FillRect(Rect{ caretX, textY, CaretWidth, lineHeight }, textColor);
    }

    drawList.PopClip();
}

void TextInput::OnUpdate(float deltaSeconds)
{
    Sanitize();
    m_Clock += deltaSeconds;
    m_Blink += deltaSeconds;
    if (m_Blink >= BlinkSeconds)
    {
        m_Blink -= BlinkSeconds;
    }

    // The caret is kept in view, so a field can hold more text than it
    // shows.
    const Rect bounds = GetBounds();
    const float width = std::max(0.0f, bounds.Width - TextInputInset * 2.0f);
    const float caret = XAtOffset(m_Caret);

    if (caret - m_ScrollX > width)
    {
        m_ScrollX = caret - width;
    }
    if (caret - m_ScrollX < 0.0f)
    {
        m_ScrollX = caret;
    }

    const float total = XAtOffset(Text.size());
    m_ScrollX = std::clamp(m_ScrollX, 0.0f, std::max(0.0f, total - width));
}

void TextInput::OnEvent(Event& event)
{
    // Text may have been assigned from outside since the last event, which
    // can leave the caret past its end or inside a character.
    Sanitize();

    switch (event.Type)
    {
        case EventType::PointerDown:
        {
            if (event.Button != MouseButton::Left)
            {
                event.Handled = true;
                break;
            }

            RequestFocus();
            const size_t offset = OffsetAtX(event.Position.X);

            // A second press soon after the first selects the word under it.
            if (m_Clock - m_LastClick < 0.4f && !event.Shift)
            {
                m_Anchor = kit::PreviousWord(Text, std::min(Text.size(), kit::NextCharacter(Text, offset)));
                m_Caret = kit::NextWord(Text, offset);
                m_LastClick = -10.0f;
                m_Dragging = false;
                event.Handled = true;
                break;
            }

            m_LastClick = m_Clock;
            MoveCaret(offset, event.Shift);
            m_Dragging = true;
            event.Handled = true;
            break;
        }

        case EventType::PointerMove:
        {
            if (m_Dragging)
            {
                MoveCaret(OffsetAtX(event.Position.X), true);
                event.Handled = true;
            }
            break;
        }

        case EventType::PointerUp:
        {
            m_Dragging = false;
            event.Handled = true;
            break;
        }

        case EventType::Text:
        {
            if (!ReadOnly)
            {
                Remember(true);
                Insert(event.Text);
            }
            event.Handled = true;
            break;
        }

        case EventType::FocusLost:
        {
            m_Dragging = false;
            m_Anchor = m_Caret;
            m_LastEdit = -10.0f;
            break;
        }

        case EventType::KeyDown:
        {
            event.Handled = true;

            if (event.Control)
            {
                switch (event.KeyCode)
                {
                    case Key::A:
                        SelectAll();
                        return;

                    case Key::C:
                    case Key::X:
                    {
                        // A masked field never hands its contents to the
                        // clipboard.
                        if (HasSelection() && !Masked)
                        {
                            const size_t from = std::min(m_Caret, m_Anchor);
                            const size_t to = std::max(m_Caret, m_Anchor);
                            GetApp().SetClipboardText(Text.substr(from, to - from));

                            if (event.KeyCode == Key::X && !ReadOnly)
                            {
                                Remember(false);
                                DeleteSelection();
                                Changed();
                            }
                        }
                        return;
                    }

                    case Key::V:
                        if (!ReadOnly)
                        {
                            Remember(false);
                            Insert(GetApp().GetClipboardText());
                        }
                        return;

                    case Key::Z:
                        if (event.Shift)
                        {
                            Redo();
                        }
                        else
                        {
                            Undo();
                        }
                        return;

                    case Key::Y:
                        Redo();
                        return;

                    // Word by word, the way every other text field moves.
                    case Key::Left:
                        MoveCaret(kit::PreviousWord(Text, m_Caret), event.Shift);
                        return;

                    case Key::Right:
                        MoveCaret(kit::NextWord(Text, m_Caret), event.Shift);
                        return;

                    case Key::Backspace:
                    case Key::Delete:
                    {
                        if (ReadOnly)
                        {
                            return;
                        }
                        Remember(false);
                        if (!HasSelection())
                        {
                            m_Anchor = event.KeyCode == Key::Backspace ? kit::PreviousWord(Text, m_Caret)
                                                                        : kit::NextWord(Text, m_Caret);
                        }
                        if (HasSelection())
                        {
                            DeleteSelection();
                            Changed();
                        }
                        return;
                    }

                    case Key::Home:
                        MoveCaret(0, event.Shift);
                        return;

                    case Key::End:
                        MoveCaret(Text.size(), event.Shift);
                        return;

                    default:
                        event.Handled = false;
                        return;
                }
            }

            switch (event.KeyCode)
            {
                case Key::Left:
                    MoveCaret(HasSelection() && !event.Shift ? std::min(m_Caret, m_Anchor)
                                                             : PreviousCharacter(Text, m_Caret),
                              event.Shift);
                    break;

                case Key::Right:
                    MoveCaret(HasSelection() && !event.Shift ? std::max(m_Caret, m_Anchor)
                                                             : NextCharacter(Text, m_Caret),
                              event.Shift);
                    break;

                case Key::Home:
                    MoveCaret(0, event.Shift);
                    break;

                case Key::End:
                    MoveCaret(Text.size(), event.Shift);
                    break;

                case Key::Backspace:
                {
                    if (ReadOnly)
                    {
                        break;
                    }
                    if (HasSelection())
                    {
                        Remember(false);
                        DeleteSelection();
                    }
                    else if (m_Caret > 0)
                    {
                        Remember(true);
                        const size_t from = PreviousCharacter(Text, m_Caret);
                        Text.erase(from, m_Caret - from);
                        m_Caret = from;
                        m_Anchor = from;
                    }
                    else
                    {
                        break;
                    }
                    Changed();
                    break;
                }

                case Key::Delete:
                {
                    if (ReadOnly)
                    {
                        break;
                    }
                    if (HasSelection())
                    {
                        Remember(false);
                        DeleteSelection();
                    }
                    else if (m_Caret < Text.size())
                    {
                        Remember(true);
                        const size_t to = NextCharacter(Text, m_Caret);
                        Text.erase(m_Caret, to - m_Caret);
                    }
                    else
                    {
                        break;
                    }
                    Changed();
                    break;
                }

                case Key::Enter:
                {
                    if (event.Repeat)
                    {
                        break;
                    }
                    m_LastEdit = -10.0f;
                    if (OnSubmitted)
                    {
                        kit::Invoke(OnSubmitted, Text);
                    }
                    break;
                }

                default:
                    event.Handled = false;
                    break;
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// ScrollView
// ---------------------------------------------------------------------------

namespace
{

constexpr float BarWidth = 8.0f;
constexpr float BarMargin = 3.0f;
constexpr float BarMinimumLength = 28.0f;

} // namespace

ScrollView::ScrollView()
{
    ChildLayout = LayoutMode::Vertical;
    ClipChildren = true;
}

float ScrollView::GetMaximumOffset() const
{
    const float visible = std::max(0.0f, GetBounds().Height - Padding * 2.0f);
    return std::max(0.0f, m_ContentHeight - visible);
}

void ScrollView::ScrollTo(float offset)
{
    Offset = std::clamp(offset, 0.0f, GetMaximumOffset());
}

Rect ScrollView::GetContentBounds() const
{
    Rect content = Element::GetContentBounds();

    // Children are laid out as tall as they want to be and then shifted up by
    // however far the view has been scrolled. Everything above and below the
    // window is still laid out; clipping is what makes only part of it show.
    content.Y -= m_Shown;
    content.Height = std::max(content.Height, m_ContentHeight);
    return content;
}

void ScrollView::Arrange(const Rect& bounds)
{
    Element::Arrange(bounds);

    // How tall the children are altogether has to be known before they are
    // laid out, because it is what the offset is clamped against.
    const Rect content = Element::GetContentBounds();
    const Vec2 available{ content.Width, content.Height };

    float total = 0.0f;
    size_t shown = 0;
    for (Element* child : GetChildren())
    {
        if (!child->Visible)
        {
            continue;
        }
        total += child->Measure(available).Y;
        ++shown;
    }
    if (shown > 1)
    {
        total += Spacing * static_cast<float>(shown - 1);
    }

    m_ContentHeight = total;
    Offset = std::clamp(Offset, 0.0f, GetMaximumOffset());
    m_Shown = Offset;
}

Rect ScrollView::BarBounds() const
{
    const Rect bounds = GetBounds();
    const float maximum = GetMaximumOffset();
    if (maximum <= 0.0f || bounds.Height <= 0.0f)
    {
        return Rect{};
    }

    const float track = bounds.Height - BarMargin * 2.0f;
    const float fraction = std::clamp(bounds.Height / std::max(1.0f, m_ContentHeight), 0.0f, 1.0f);
    const float length = std::max(BarMinimumLength, track * fraction);
    const float travel = std::max(0.0f, track - length);
    const float position = travel * (Offset / maximum);

    return Rect{ bounds.X + bounds.Width - BarWidth - BarMargin, bounds.Y + BarMargin + position,
                 BarWidth, length };
}

void ScrollView::Paint(DrawList& drawList)
{
    if (!ShowBar || m_BarFade <= 0.01f)
    {
        return;
    }

    const Rect bar = BarBounds();
    if (bar.Height <= 0.0f)
    {
        return;
    }

    const Theme& theme = GetTheme();
    if (const BoxStyle* part = kit::PartLook(theme.Styles.ScrollbarThumb, *this, m_Dragging, m_Dragging))
    {
        drawList.PushOpacity(m_BarFade);
        drawList.DrawBox(bar, *part);
        drawList.PopOpacity();
        return;
    }
    const Color color{ theme.Text.R, theme.Text.G, theme.Text.B, 0.45f * m_BarFade };
    drawList.FillRoundedRect(bar, BarWidth * 0.5f, color);
}

void ScrollView::OnUpdate(float deltaSeconds)
{
    // The bar shows while the pointer is over the view and fades out when it
    // leaves, so it does not sit on top of the content permanently.
    const bool wanted = GetMaximumOffset() > 0.0f && (IsHovered() || m_Dragging);
    m_BarFade = Approach(m_BarFade, wanted ? 1.0f : 0.0f, 4.0f, deltaSeconds);

    if (m_Shown != Offset)
    {
        MarkLayoutDirty();
    }
}

void ScrollView::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::Wheel:
        {
            if (GetMaximumOffset() <= 0.0f)
            {
                break;
            }
            ScrollBy(-event.WheelDelta * WheelStep);
            MarkLayoutDirty();
            event.Handled = true;
            break;
        }

        case EventType::PointerDown:
        {
            const Rect bar = BarBounds();
            const bool onBar = bar.Height > 0.0f && event.Position.X >= bar.X &&
                               event.Position.X <= bar.X + bar.Width && event.Position.Y >= bar.Y &&
                               event.Position.Y <= bar.Y + bar.Height;
            if (!onBar)
            {
                break;
            }

            m_Dragging = true;
            m_DragOrigin = event.Position.Y;
            m_DragOffset = Offset;
            event.Handled = true;
            break;
        }

        case EventType::PointerMove:
        {
            if (!m_Dragging)
            {
                break;
            }

            const Rect bounds = GetBounds();
            const Rect bar = BarBounds();
            const float travel = bounds.Height - BarMargin * 2.0f - bar.Height;
            if (travel > 0.0f)
            {
                const float moved = (event.Position.Y - m_DragOrigin) / travel * GetMaximumOffset();
                ScrollTo(m_DragOffset + moved);
                MarkLayoutDirty();
            }
            event.Handled = true;
            break;
        }

        case EventType::PointerUp:
        {
            if (m_Dragging)
            {
                m_Dragging = false;
                event.Handled = true;
            }
            break;
        }

        default:
            break;
    }
}

} // namespace opane
