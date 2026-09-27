// Small controls: a progress bar, radio buttons, a toggle, and a number field.
//
// Written against the public API alone, like every built-in widget.

#include <opane/opane.h>

#include "WidgetKit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace opane
{

// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------

ProgressBar::ProgressBar()
{
    Size = Size2{ Dim::FromScale(1.0f), Dim::FromOffset(22.0f) };
    Interactive = false;
}

Vec2 ProgressBar::Measure(Vec2 available)
{
    return Size.Resolve(available);
}

void ProgressBar::OnUpdate(float deltaSeconds)
{
    m_Sweep += deltaSeconds * 0.9f;
    if (m_Sweep > 1.0f)
    {
        m_Sweep -= std::floor(m_Sweep);
    }

    // The bar eases toward its value rather than jumping, so progress
    // reported in large steps still reads as motion.
    const float target = std::clamp(Value, 0.0f, 1.0f);
    m_Shown = kit::Approach(m_Shown, target, std::max(0.5f, std::abs(target - m_Shown) * 6.0f), deltaSeconds);
}

void ProgressBar::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    const float radius = std::min(bounds.Height * 0.5f, theme.CornerRadius);

    const BoxStyle* track = kit::PartLook(theme.Styles.ProgressTrack, *this, false, false);
    const BoxStyle* fill = kit::PartLook(theme.Styles.ProgressFill, *this, false, false);
    if (track != nullptr)
    {
        drawList.DrawBox(bounds, *track);
    }
    else
    {
        drawList.FillRoundedRect(bounds, radius, theme.SurfacePressed);
    }

    auto Fill = [&](const Rect& where) {
        if (fill != nullptr)
        {
            drawList.DrawBox(where, *fill);
        }
        else
        {
            drawList.FillRoundedRect(where, radius, theme.Accent);
        }
    };

    if (Indeterminate)
    {
        // A third of the bar sweeping across, clipped to the track.
        drawList.PushClip(bounds);
        const float width = bounds.Width * 0.33f;
        const float x = bounds.X - width + (bounds.Width + width) * m_Sweep;
        Fill(Rect{ x, bounds.Y, width, bounds.Height });
        drawList.PopClip();
    }
    else if (m_Shown > 0.0f)
    {
        const float width = std::max(bounds.Height, bounds.Width * m_Shown);
        drawList.PushClip(Rect{ bounds.X, bounds.Y, bounds.Width * m_Shown + 0.5f, bounds.Height });
        Fill(Rect{ bounds.X, bounds.Y, width, bounds.Height });
        drawList.PopClip();
    }

    drawList.StrokeRect(bounds, theme.BorderWidth, theme.Border, radius);

    if (!Text.empty())
    {
        std::string label = Text;
        const size_t marker = label.find("{}");
        if (marker != std::string::npos)
        {
            char percent[16];
            std::snprintf(percent, sizeof(percent), "%d", static_cast<int>(std::lround(std::clamp(Value, 0.0f, 1.0f) * 100.0f)));
            label.replace(marker, 2, percent);
        }
        drawList.DrawTextInRect(label, bounds, theme.Font, theme.Text, TextAlign::Center);
    }
}

// ---------------------------------------------------------------------------
// RadioButton
// ---------------------------------------------------------------------------

RadioButton::RadioButton()
{
    Size = Size2::FromOffset(180.0f, 28.0f);
    Focusable = true;
}

Vec2 RadioButton::Measure(Vec2 available)
{
    const Vec2 requested = Size.Resolve(available);
    if (requested.X > 0.0f)
    {
        return requested;
    }
    const Vec2 text = MeasureText(Text);
    return Vec2{ text.X + 34.0f, std::max(text.Y, 20.0f) };
}

void RadioButton::Select()
{
    if (Element* parent = GetParent())
    {
        for (Element* sibling : parent->GetChildren())
        {
            auto* other = dynamic_cast<RadioButton*>(sibling);
            if (other != nullptr && other != this && other->Group == Group)
            {
                other->Checked = false;
            }
        }
    }

    const bool changed = !Checked;
    Checked = true;
    if (changed && OnSelected)
    {
        kit::Invoke(OnSelected);
    }
}

void RadioButton::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();

    const float radius = 9.0f;
    const Vec2 center{ bounds.X + radius, bounds.Y + bounds.Height * 0.5f };

    drawList.FillCircle(center, radius, IsHovered() ? theme.SurfaceHovered : theme.Surface);
    drawList.StrokeCircle(center, radius, theme.BorderWidth, Checked ? theme.Accent : theme.Border);
    if (Checked)
    {
        drawList.FillCircle(center, 4.5f, theme.Accent);
    }
    if (IsFocusVisible())
    {
        drawList.StrokeCircle(center, radius + 3.0f, 2.0f, theme.Accent);
    }

    const Rect textBounds{ bounds.X + radius * 2.0f + 10.0f, bounds.Y, bounds.Width - radius * 2.0f - 10.0f,
                           bounds.Height };
    drawList.DrawTextInRect(Text, textBounds, theme.Font, theme.Text, TextAlign::Left);
}

void RadioButton::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerDown:
            event.Handled = true;
            break;

        case EventType::PointerUp:
            if (event.Button == MouseButton::Left && HitTest(event.Position))
            {
                Select();
            }
            event.Handled = true;
            break;

        case EventType::KeyDown:
        {
            if (event.KeyCode == Key::Space && !event.Repeat)
            {
                Select();
                event.Handled = true;
                break;
            }

            // The arrows move through the set and select as they go, the way a
            // radio group behaves everywhere.
            const bool forward = event.KeyCode == Key::Down || event.KeyCode == Key::Right;
            const bool backward = event.KeyCode == Key::Up || event.KeyCode == Key::Left;
            if ((forward || backward) && GetParent() != nullptr)
            {
                std::vector<RadioButton*> set;
                for (Element* sibling : GetParent()->GetChildren())
                {
                    auto* other = dynamic_cast<RadioButton*>(sibling);
                    if (other != nullptr && other->Group == Group && other->Visible && other->Interactive)
                    {
                        set.push_back(other);
                    }
                }
                const auto at = std::find(set.begin(), set.end(), this);
                if (at != set.end() && set.size() > 1)
                {
                    const size_t index = static_cast<size_t>(at - set.begin());
                    const size_t next = forward ? (index + 1) % set.size() : (index + set.size() - 1) % set.size();
                    set[next]->Select();
                    set[next]->RequestFocus();
                }
                event.Handled = true;
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Toggle
// ---------------------------------------------------------------------------

Toggle::Toggle()
{
    Size = Size2::FromOffset(180.0f, 28.0f);
    Focusable = true;
}

Vec2 Toggle::Measure(Vec2 available)
{
    const Vec2 requested = Size.Resolve(available);
    if (requested.X > 0.0f)
    {
        return requested;
    }
    const Vec2 text = MeasureText(Text);
    return Vec2{ text.X + 52.0f, std::max(text.Y, 22.0f) };
}

void Toggle::OnUpdate(float deltaSeconds)
{
    m_Knob = kit::Approach(m_Knob, On ? 1.0f : 0.0f, 7.0f, deltaSeconds);
}

void Toggle::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();

    const float width = 38.0f;
    const float height = 20.0f;
    const Rect track{ bounds.X, bounds.Y + (bounds.Height - height) * 0.5f, width, height };

    // Smoothed, so the knob eases rather than sliding at a constant speed.
    const float eased = m_Knob * m_Knob * (3.0f - 2.0f * m_Knob);

    if (const BoxStyle* part = kit::PartLook(theme.Styles.ToggleTrack, *this, On, IsPressed()))
    {
        drawList.DrawBox(track, *part);
    }
    else
    {
        drawList.FillRoundedRect(track, height * 0.5f, kit::Mix(theme.SurfacePressed, theme.Accent, eased));
        drawList.StrokeRect(track, theme.BorderWidth, theme.Border, height * 0.5f);
    }
    kit::DrawFocusRing(drawList, *this, track, height * 0.5f);

    const float knobRadius = height * 0.5f - 3.0f;
    const Vec2 knob{ track.X + height * 0.5f + (width - height) * eased, track.Y + height * 0.5f };
    if (const BoxStyle* part = kit::PartLook(theme.Styles.ToggleKnob, *this, On, IsPressed()))
    {
        drawList.DrawBox(Rect{ knob.X - knobRadius, knob.Y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f },
                         *part);
    }
    else
    {
        drawList.FillCircle(knob, knobRadius, Color{ 1.0f, 1.0f, 1.0f, 0.96f });
    }

    const Rect textBounds{ bounds.X + width + 12.0f, bounds.Y, bounds.Width - width - 12.0f, bounds.Height };
    drawList.DrawTextInRect(Text, textBounds, theme.Font, kit::LookText(*this, theme.Text), TextAlign::Left);
}

void Toggle::OnEvent(Event& event)
{
    const bool clicked =
        event.Type == EventType::PointerUp && event.Button == MouseButton::Left && HitTest(event.Position);
    const bool pressed = event.Type == EventType::KeyDown && !event.Repeat &&
                         (event.KeyCode == Key::Space || event.KeyCode == Key::Enter);

    if (clicked || pressed)
    {
        On = !On;
        if (OnChanged)
        {
            kit::Invoke(OnChanged, On);
        }
        event.Handled = true;
    }
    else if (event.Type == EventType::PointerDown || event.Type == EventType::PointerUp)
    {
        event.Handled = true;
    }
}

// ---------------------------------------------------------------------------
// NumberField
// ---------------------------------------------------------------------------

namespace
{

// Moving this far before release turns a press into a drag; less, and a
// double press opens the field for typing.
constexpr float DragThreshold = 3.0f;

} // namespace

NumberField::NumberField()
{
    Size = Size2{ Dim::FromScale(1.0f), Dim::FromOffset(30.0f) };
    Focusable = true;
    Cursor = CursorShape::ResizeHorizontal;
}

bool NumberField::WantsText() const
{
    return m_Editing;
}

std::string NumberField::Format(double value) const
{
    char text[64];
    std::snprintf(text, sizeof(text), "%.*f", std::clamp(Precision, 0, 12), value);
    return text;
}

void NumberField::SetValue(double value)
{
    if (!std::isfinite(value))
    {
        return;
    }
    const double low = std::min(Minimum, Maximum);
    const double high = std::max(Minimum, Maximum);
    const double clamped = std::clamp(value, low, high);
    if (clamped != Value)
    {
        Value = clamped;
        if (OnChanged)
        {
            kit::Invoke(OnChanged, Value);
        }
    }
}

Vec2 NumberField::Measure(Vec2 available)
{
    const Vec2 requested = Size.Resolve(available);
    if (requested.X > 0.0f && requested.Y > 0.0f)
    {
        return requested;
    }
    const Vec2 text = MeasureText(Prefix + " " + Format(Value) + " " + Suffix);
    return Vec2{ requested.X > 0.0f ? requested.X : text.X + 24.0f,
                 requested.Y > 0.0f ? requested.Y : text.Y + 12.0f };
}

void NumberField::BeginEditing()
{
    m_Editing = true;
    m_EditText = Format(Value);
    m_Blink = 0.0f;
    Cursor = CursorShape::Text;
    RequestFocus();
}

void NumberField::FinishEditing(bool commit)
{
    if (!m_Editing)
    {
        return;
    }
    m_Editing = false;
    Cursor = CursorShape::ResizeHorizontal;

    if (commit)
    {
        // Parsed in the C locale's manner, so "1.5" means one and a half
        // wherever the program runs.
        const char* begin = m_EditText.c_str();
        char* end = nullptr;
        const double parsed = std::strtod(begin, &end);
        if (end != begin)
        {
            SetValue(parsed);
        }
    }
    m_EditText.clear();
}

void NumberField::OnUpdate(float deltaSeconds)
{
    m_Clock += deltaSeconds;
    m_Blink += deltaSeconds;
    if (m_Blink > 1.0f)
    {
        m_Blink -= 1.0f;
    }
}

const Style* NumberField::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.TextInput;
}

void NumberField::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    const float radius = std::min(theme.CornerRadius, bounds.Height * 0.5f);

    if (!HasAppearance())
    {
        const Color fill = m_Editing ? theme.Background
                                     : (IsHovered() || m_Dragging ? theme.SurfaceHovered : theme.Surface);
        drawList.FillRoundedRect(bounds, radius, fill);
        drawList.StrokeRect(bounds, theme.BorderWidth, m_Editing ? theme.Accent : theme.Border, radius);
    }
    kit::DrawFocusRing(drawList, *this, bounds, radius);

    const Rect inner = kit::Inset(bounds, 8.0f);
    drawList.PushClip(bounds);

    if (m_Editing)
    {
        drawList.DrawTextInRect(m_EditText, Rect{ inner.X, bounds.Y, inner.Width, bounds.Height }, theme.Font,
                                theme.Text, TextAlign::Left);
        if (m_Blink < 0.5f)
        {
            const float x = inner.X + MeasureText(m_EditText).X + 1.0f;
            const float lineHeight = GetLineHeight();
            const float top = bounds.Y + (bounds.Height - lineHeight) * 0.5f;
            drawList.FillRect(Rect{ x, top, 1.5f, lineHeight }, theme.Text);
        }
    }
    else
    {
        // The prefix dimmed on the left, the number on the right: a column
        // of fields then lines up by its digits.
        if (!Prefix.empty())
        {
            drawList.DrawTextInRect(Prefix, Rect{ inner.X, bounds.Y, inner.Width, bounds.Height }, theme.Font,
                                    theme.TextMuted, TextAlign::Left);
        }
        const std::string number = Suffix.empty() ? Format(Value) : Format(Value) + " " + Suffix;
        drawList.DrawTextInRect(number, Rect{ inner.X, bounds.Y, inner.Width, bounds.Height }, theme.Font,
                                theme.Text, TextAlign::Right);
    }

    drawList.PopClip();
}

void NumberField::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerDown:
        {
            event.Handled = true;
            if (event.Button != MouseButton::Left || m_Editing)
            {
                break;
            }
            RequestFocus();
            m_Dragging = true;
            m_Moved = false;
            m_DragStartX = event.Position.X;
            m_DragStartValue = Value;
            break;
        }

        case EventType::PointerMove:
        {
            if (!m_Dragging)
            {
                break;
            }
            const float dx = event.Position.X - m_DragStartX;
            if (!m_Moved && std::abs(dx) < DragThreshold)
            {
                break;
            }
            m_Moved = true;

            // A pixel is a step; Shift makes it a tenth of one, for the last
            // bit of precision.
            const bool fine = GetApp().IsValid() && (GetApp().GetInput().IsKeyDown(Key::LeftShift) ||
                                                     GetApp().GetInput().IsKeyDown(Key::RightShift));
            const double scale = fine ? Step * 0.1 : Step;
            SetValue(m_DragStartValue + static_cast<double>(dx) * scale);
            event.Handled = true;
            break;
        }

        case EventType::PointerUp:
        {
            event.Handled = true;
            if (!m_Dragging)
            {
                break;
            }
            m_Dragging = false;

            // A press that did not drag is a click; two in quick succession
            // open the field for typing.
            if (!m_Moved)
            {
                if (m_Clock - m_LastClick < 0.4f)
                {
                    BeginEditing();
                    m_LastClick = -10.0f;
                }
                else
                {
                    m_LastClick = m_Clock;
                }
            }
            break;
        }

        case EventType::Text:
        {
            if (!m_Editing)
            {
                break;
            }
            for (char c : event.Text)
            {
                if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')
                {
                    m_EditText.push_back(c);
                }
            }
            m_Blink = 0.0f;
            event.Handled = true;
            break;
        }

        case EventType::KeyDown:
        {
            if (m_Editing)
            {
                switch (event.KeyCode)
                {
                    case Key::Backspace:
                        if (!m_EditText.empty())
                        {
                            m_EditText.pop_back();
                        }
                        event.Handled = true;
                        break;
                    case Key::Enter:
                        FinishEditing(true);
                        event.Handled = true;
                        break;
                    case Key::Escape:
                        FinishEditing(false);
                        event.Handled = true;
                        break;
                    default:
                        break;
                }
                break;
            }

            const double big = Step * 10.0;
            switch (event.KeyCode)
            {
                case Key::Up:
                case Key::Right:
                    SetValue(Value + (event.Shift ? big : Step));
                    event.Handled = true;
                    break;
                case Key::Down:
                case Key::Left:
                    SetValue(Value - (event.Shift ? big : Step));
                    event.Handled = true;
                    break;
                case Key::Enter:
                    if (!event.Repeat)
                    {
                        BeginEditing();
                        event.Handled = true;
                    }
                    break;
                default:
                    break;
            }
            break;
        }

        case EventType::FocusLost:
            // Leaving the field keeps what was typed, the way a spreadsheet
            // cell does.
            FinishEditing(true);
            m_Dragging = false;
            break;

        default:
            break;
    }
}

} // namespace opane
