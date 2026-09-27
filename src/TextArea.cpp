// Multi-line text editing.
//
// The text is one UTF-8 string with newlines, edited by byte offset. What is
// on screen is a list of visual lines (the paragraphs, broken again at spaces
// when word wrap is on), recomputed only when the text or the width changes.
// Every edit goes through one function, so undo sees every edit.
//
// Written against the public API alone.

#include <opane/opane.h>

#include "WidgetKit.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace opane
{
namespace
{

constexpr float AreaInset = 8.0f;
constexpr float BlinkSeconds = 1.0f;
constexpr float DoubleClickSeconds = 0.4f;
constexpr size_t UndoLimit = 500;

} // namespace

TextArea::TextArea()
{
    Size = Size2{ Dim::FromScale(1.0f), Dim::FromOffset(140.0f) };
    Focusable = true;
    Cursor = CursorShape::Text;
    ClipChildren = true;
}

bool TextArea::WantsText() const
{
    return !ReadOnly;
}

Vec2 TextArea::Measure(Vec2 available)
{
    return Size.Resolve(available);
}

Rect TextArea::TextBounds() const
{
    return kit::Inset(GetBounds(), AreaInset);
}

void TextArea::Reflow()
{
    const float width = WordWrap ? std::max(1.0f, TextBounds().Width) : -1.0f;
    if (m_FlowedText == Text && m_FlowedWidth == width && !m_Lines.empty())
    {
        return;
    }
    m_FlowedText = Text;
    m_FlowedWidth = width;
    m_Lines.clear();

    // Each character's advance, measured once per distinct character. Summing
    // advances ignores kerning, which moves a break by a pixel at most, and
    // makes wrapping linear in the length of the text instead of quadratic.
    std::unordered_map<std::string, float> advances;
    auto Advance = [&](size_t at, size_t next) {
        const std::string character = Text.substr(at, next - at);
        auto found = advances.find(character);
        if (found != advances.end())
        {
            return found->second;
        }
        const float width = MeasureText(character).X;
        advances.emplace(character, width);
        return width;
    };

    size_t paragraph = 0;
    while (true)
    {
        size_t end = Text.find('\n', paragraph);
        if (end == std::string::npos)
        {
            end = Text.size();
        }

        if (!WordWrap)
        {
            m_Lines.push_back(Line{ paragraph, end });
        }
        else
        {
            // Greedy: as many words as fit, breaking after a space. A word
            // wider than the whole line is broken between characters.
            size_t start = paragraph;
            while (start < end || start == paragraph)
            {
                float used = 0.0f;
                size_t fit = start;
                size_t lastBreak = std::string::npos;
                size_t at = start;
                while (at < end)
                {
                    const size_t next = kit::NextCharacter(Text, at);
                    const float advance = Advance(at, next);
                    // A space may hang past the edge; it is never what forces
                    // a break.
                    if (used + advance > width && Text[at] != ' ')
                    {
                        break;
                    }
                    used += advance;
                    fit = next;
                    if (Text[at] == ' ')
                    {
                        lastBreak = next;
                    }
                    at = next;
                }

                size_t cut = fit;
                if (fit < end && lastBreak != std::string::npos && lastBreak > start)
                {
                    cut = lastBreak;
                }
                if (cut == start && start < end)
                {
                    cut = kit::NextCharacter(Text, start);
                }
                m_Lines.push_back(Line{ start, cut });
                if (cut >= end)
                {
                    break;
                }
                start = cut;
            }
        }

        if (end >= Text.size())
        {
            break;
        }
        paragraph = end + 1;
        if (paragraph == Text.size())
        {
            // A trailing newline starts an empty last line the caret can sit on.
            m_Lines.push_back(Line{ paragraph, paragraph });
            break;
        }
    }

    if (m_Lines.empty())
    {
        m_Lines.push_back(Line{ 0, 0 });
    }
}

int TextArea::LineOf(size_t offset) const
{
    // The last line starting at or before the offset. A wrapped line's end is
    // the next line's start, and there the caret belongs to the next line.
    int low = 0;
    int high = static_cast<int>(m_Lines.size()) - 1;
    int found = 0;
    while (low <= high)
    {
        const int middle = (low + high) / 2;
        if (m_Lines[static_cast<size_t>(middle)].Start <= offset)
        {
            found = middle;
            low = middle + 1;
        }
        else
        {
            high = middle - 1;
        }
    }
    return found;
}

float TextArea::XOf(size_t offset) const
{
    const Line& line = m_Lines[static_cast<size_t>(LineOf(offset))];
    const size_t to = std::clamp(offset, line.Start, std::max(line.Start, line.End));
    return MeasureText(Text.substr(line.Start, to - line.Start)).X;
}

size_t TextArea::OffsetAt(int lineIndex, float x) const
{
    lineIndex = std::clamp(lineIndex, 0, static_cast<int>(m_Lines.size()) - 1);
    const Line& line = m_Lines[static_cast<size_t>(lineIndex)];

    // The end a caret may take on this line: a wrapped line's last position
    // belongs to the next line, so it stops one character short.
    size_t limit = line.End;
    const bool wrapped = line.End < Text.size() && Text[line.End] != '\n' && lineIndex + 1 < static_cast<int>(m_Lines.size());
    if (wrapped && limit > line.Start)
    {
        limit = kit::PreviousCharacter(Text, limit);
    }

    size_t best = line.Start;
    float bestDistance = std::abs(x);
    size_t at = line.Start;
    while (at < limit)
    {
        at = kit::NextCharacter(Text, at);
        const float distance = std::abs(MeasureText(Text.substr(line.Start, at - line.Start)).X - x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = at;
        }
        else if (distance > bestDistance + 40.0f)
        {
            break;
        }
    }
    return best;
}

size_t TextArea::OffsetAtPoint(Vec2 point) const
{
    const Rect text = TextBounds();
    const float lineHeight = std::max(1.0f, GetLineHeight());
    const int line = static_cast<int>(std::floor((point.Y - text.Y + m_Scroll) / lineHeight));
    return OffsetAt(line, point.X - text.X + m_ScrollX);
}

void TextArea::SetCaret(size_t offset)
{
    offset = std::min(offset, Text.size());
    while (offset > 0 && offset < Text.size() && (static_cast<unsigned char>(Text[offset]) & 0xC0) == 0x80)
    {
        --offset;
    }
    m_Caret = offset;
    m_Anchor = offset;
    m_GoalX = -1.0f;
}

void TextArea::SelectAll()
{
    m_Anchor = 0;
    m_Caret = Text.size();
}

std::string TextArea::GetSelectedText() const
{
    const size_t from = std::min(std::min(m_Caret, m_Anchor), Text.size());
    const size_t to = std::min(std::max(m_Caret, m_Anchor), Text.size());
    return Text.substr(from, to - from);
}

void TextArea::MoveCaret(size_t offset, bool extend)
{
    m_Caret = std::min(offset, Text.size());
    if (!extend)
    {
        m_Anchor = m_Caret;
    }
    m_Blink = 0.0f;
}

void TextArea::Replace(size_t from, size_t to, const std::string& text, bool mergeable)
{
    from = std::min(from, Text.size());
    to = std::clamp(to, from, Text.size());

    Change change;
    change.At = from;
    change.Removed = Text.substr(from, to - from);
    change.Inserted = text;
    change.CaretBefore = m_Caret;
    change.CaretAfter = from + text.size();

    // Typing in a burst undoes as one step, and so does a held Backspace.
    bool merged = false;
    if (mergeable && !m_Undo.empty() && m_Clock - m_LastEdit < 1.0f)
    {
        Change& last = m_Undo.back();
        const bool typing = change.Removed.empty() && last.Removed.empty() &&
                            last.At + last.Inserted.size() == from;
        const bool erasing = change.Inserted.empty() && last.Inserted.empty() && to == last.At;
        const bool deleting = change.Inserted.empty() && last.Inserted.empty() && from == last.At;
        if (typing)
        {
            last.Inserted += text;
            last.CaretAfter = change.CaretAfter;
            merged = true;
        }
        else if (erasing)
        {
            last.At = from;
            last.Removed = change.Removed + last.Removed;
            last.CaretAfter = from;
            merged = true;
        }
        else if (deleting)
        {
            last.Removed += change.Removed;
            merged = true;
        }
    }
    if (!merged)
    {
        m_Undo.push_back(change);
        if (m_Undo.size() > UndoLimit)
        {
            m_Undo.erase(m_Undo.begin());
        }
    }
    m_Redo.clear();
    m_LastEdit = mergeable ? m_Clock : -10.0f;

    Text.replace(from, to - from, text);
    m_Caret = m_Anchor = from + text.size();
    m_GoalX = -1.0f;
    m_Blink = 0.0f;

    if (OnChanged)
    {
        kit::Invoke(OnChanged, Text);
    }
}

void TextArea::InsertText(const std::string& text)
{
    if (ReadOnly)
    {
        return;
    }
    const size_t from = std::min(m_Caret, m_Anchor);
    const size_t to = std::max(m_Caret, m_Anchor);

    // Carriage returns are dropped, so text pasted from anywhere ends lines
    // the way this field does.
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char c : text)
    {
        if (c != '\r')
        {
            cleaned.push_back(c);
        }
    }
    Replace(from, to, cleaned, from == to && cleaned.size() <= 4 && cleaned.find('\n') == std::string::npos);
}

void TextArea::Undo()
{
    if (m_Undo.empty() || ReadOnly)
    {
        return;
    }
    Change change = m_Undo.back();
    m_Undo.pop_back();
    const size_t at = std::min(change.At, Text.size());
    Text.replace(at, std::min(change.Inserted.size(), Text.size() - at), change.Removed);
    m_Caret = m_Anchor = std::min(change.CaretBefore, Text.size());
    m_Redo.push_back(change);
    m_LastEdit = -10.0f;
    if (OnChanged)
    {
        kit::Invoke(OnChanged, Text);
    }
}

void TextArea::Redo()
{
    if (m_Redo.empty() || ReadOnly)
    {
        return;
    }
    Change change = m_Redo.back();
    m_Redo.pop_back();
    const size_t at = std::min(change.At, Text.size());
    Text.replace(at, std::min(change.Removed.size(), Text.size() - at), change.Inserted);
    m_Caret = m_Anchor = std::min(change.CaretAfter, Text.size());
    m_Undo.push_back(change);
    m_LastEdit = -10.0f;
    if (OnChanged)
    {
        kit::Invoke(OnChanged, Text);
    }
}

void TextArea::EnsureCaretVisible()
{
    const Rect text = TextBounds();
    const float lineHeight = std::max(1.0f, GetLineHeight());
    const float top = static_cast<float>(LineOf(m_Caret)) * lineHeight;
    if (top < m_Scroll)
    {
        m_Scroll = top;
    }
    else if (top + lineHeight > m_Scroll + text.Height)
    {
        m_Scroll = top + lineHeight - text.Height;
    }

    if (!WordWrap)
    {
        const float x = XOf(m_Caret);
        if (x < m_ScrollX)
        {
            m_ScrollX = std::max(0.0f, x - 20.0f);
        }
        else if (x > m_ScrollX + text.Width - 2.0f)
        {
            m_ScrollX = x - text.Width + 20.0f;
        }
    }
    else
    {
        m_ScrollX = 0.0f;
    }

    const float maximum = std::max(0.0f, static_cast<float>(m_Lines.size()) * lineHeight - text.Height);
    m_Scroll = std::clamp(m_Scroll, 0.0f, maximum);
}

void TextArea::OnUpdate(float deltaSeconds)
{
    m_Clock += deltaSeconds;
    m_Blink += deltaSeconds;
    if (m_Blink >= BlinkSeconds)
    {
        m_Blink -= BlinkSeconds;
    }

    // Text assigned from outside may leave the caret past its end.
    m_Caret = std::min(m_Caret, Text.size());
    m_Anchor = std::min(m_Anchor, Text.size());
    Reflow();

    const float lineHeight = std::max(1.0f, GetLineHeight());
    const float maximum = std::max(0.0f, static_cast<float>(m_Lines.size()) * lineHeight - TextBounds().Height);
    m_Scroll = std::clamp(m_Scroll, 0.0f, maximum);
}

const Style* TextArea::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.TextInput;
}

void TextArea::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    const float radius = std::min(theme.CornerRadius, 8.0f);

    m_Caret = std::min(m_Caret, Text.size());
    m_Anchor = std::min(m_Anchor, Text.size());
    Reflow();

    if (!HasAppearance())
    {
        drawList.FillRoundedRect(bounds, radius, theme.Background);
        drawList.StrokeRect(bounds, theme.BorderWidth, IsFocused() ? theme.Accent : theme.Border, radius);
    }

    const Rect text = TextBounds();
    const float lineHeight = std::max(1.0f, GetLineHeight());
    drawList.PushClip(text);

    if (Text.empty() && !Placeholder.empty() && !IsFocused())
    {
        drawList.DrawTextInRect(Placeholder, Rect{ text.X, text.Y, text.Width, lineHeight }, theme.Font, theme.TextMuted,
                                TextAlign::Left);
    }

    const size_t selectionFrom = std::min(m_Caret, m_Anchor);
    const size_t selectionTo = std::max(m_Caret, m_Anchor);

    const int first = std::max(0, static_cast<int>(m_Scroll / lineHeight));
    const int last = std::min(static_cast<int>(m_Lines.size()), first + static_cast<int>(text.Height / lineHeight) + 2);

    for (int index = first; index < last; ++index)
    {
        const Line& line = m_Lines[static_cast<size_t>(index)];
        const float y = text.Y + static_cast<float>(index) * lineHeight - m_Scroll;
        const float x = text.X - m_ScrollX;

        // The part of the selection on this line, with a sliver past the end
        // when the selection carries on to the next line.
        if (selectionFrom != selectionTo && selectionFrom <= line.End && selectionTo >= line.Start)
        {
            const size_t from = std::max(selectionFrom, line.Start);
            const size_t to = std::min(selectionTo, line.End);
            const float left = MeasureText(Text.substr(line.Start, from - line.Start)).X;
            float right = MeasureText(Text.substr(line.Start, to - line.Start)).X;
            if (selectionTo > line.End)
            {
                right += 6.0f;
            }
            if (right > left)
            {
                drawList.FillRect(Rect{ x + left, y, right - left, lineHeight },
                                  kit::WithAlpha(theme.Accent, IsFocused() ? 0.45f : 0.25f));
            }
        }

        if (line.End > line.Start)
        {
            drawList.DrawTextInRect(Text.substr(line.Start, line.End - line.Start),
                                    Rect{ x, y, text.Width + m_ScrollX + 4000.0f, lineHeight }, theme.Font, theme.Text,
                                    TextAlign::Left);
        }
    }

    if (IsFocused() && m_Blink < BlinkSeconds * 0.5f)
    {
        const int line = LineOf(m_Caret);
        const float y = text.Y + static_cast<float>(line) * lineHeight - m_Scroll;
        const float x = text.X + XOf(m_Caret) - m_ScrollX;
        drawList.FillRect(Rect{ x, y + 2.0f, 1.5f, lineHeight - 4.0f }, theme.Text);
    }

    drawList.PopClip();

    const float maximum = std::max(0.0f, static_cast<float>(m_Lines.size()) * lineHeight - text.Height);
    if (maximum > 0.0f)
    {
        const float view = text.Height;
        const float length = std::max(20.0f, view * view / (view + maximum));
        const float barY = text.Y + (view - length) * (m_Scroll / maximum);
        drawList.FillRoundedRect(Rect{ bounds.X + bounds.Width - 6.0f, barY, 3.0f, length }, 1.5f,
                                 kit::WithAlpha(theme.TextMuted, 0.55f));
    }
}

void TextArea::OnEvent(Event& event)
{
    m_Caret = std::min(m_Caret, Text.size());
    m_Anchor = std::min(m_Anchor, Text.size());
    Reflow();

    const float lineHeight = std::max(1.0f, GetLineHeight());

    switch (event.Type)
    {
        case EventType::PointerDown:
        {
            event.Handled = true;
            if (event.Button != MouseButton::Left)
            {
                break;
            }
            RequestFocus();
            const size_t offset = OffsetAtPoint(event.Position);
            if (m_Clock - m_LastClick < DoubleClickSeconds && !event.Shift)
            {
                m_Anchor = kit::PreviousWord(Text, kit::NextCharacter(Text, offset));
                m_Caret = kit::NextWord(Text, offset);
                m_LastClick = -10.0f;
                m_Dragging = false;
                break;
            }
            m_LastClick = m_Clock;
            MoveCaret(offset, event.Shift);
            m_GoalX = -1.0f;
            m_Dragging = true;
            break;
        }

        case EventType::PointerMove:
            if (m_Dragging)
            {
                MoveCaret(OffsetAtPoint(event.Position), true);
                EnsureCaretVisible();
                event.Handled = true;
            }
            break;

        case EventType::PointerUp:
            m_Dragging = false;
            event.Handled = true;
            break;

        case EventType::Wheel:
        {
            const float maximum = std::max(0.0f, static_cast<float>(m_Lines.size()) * lineHeight - TextBounds().Height);
            m_Scroll = std::clamp(m_Scroll - event.WheelDelta * lineHeight * 3.0f, 0.0f, maximum);
            event.Handled = true;
            break;
        }

        case EventType::Text:
        {
            InsertText(event.Text);
            EnsureCaretVisible();
            event.Handled = true;
            break;
        }

        case EventType::FocusLost:
            m_Dragging = false;
            m_LastEdit = -10.0f;
            break;

        case EventType::KeyDown:
        {
            const bool shift = event.Shift;
            event.Handled = true;

            if (event.Control)
            {
                switch (event.KeyCode)
                {
                    case Key::A: SelectAll(); return;
                    case Key::C:
                        if (HasSelection())
                        {
                            GetApp().SetClipboardText(GetSelectedText());
                        }
                        return;
                    case Key::X:
                        if (HasSelection() && !ReadOnly)
                        {
                            GetApp().SetClipboardText(GetSelectedText());
                            Replace(std::min(m_Caret, m_Anchor), std::max(m_Caret, m_Anchor), {}, false);
                            EnsureCaretVisible();
                        }
                        return;
                    case Key::V:
                        InsertText(GetApp().GetClipboardText());
                        EnsureCaretVisible();
                        return;
                    case Key::Z:
                        if (shift)
                        {
                            Redo();
                        }
                        else
                        {
                            Undo();
                        }
                        EnsureCaretVisible();
                        return;
                    case Key::Y:
                        Redo();
                        EnsureCaretVisible();
                        return;
                    case Key::Left:
                        MoveCaret(kit::PreviousWord(Text, m_Caret), shift);
                        m_GoalX = -1.0f;
                        EnsureCaretVisible();
                        return;
                    case Key::Right:
                        MoveCaret(kit::NextWord(Text, m_Caret), shift);
                        m_GoalX = -1.0f;
                        EnsureCaretVisible();
                        return;
                    case Key::Home:
                        MoveCaret(0, shift);
                        EnsureCaretVisible();
                        return;
                    case Key::End:
                        MoveCaret(Text.size(), shift);
                        EnsureCaretVisible();
                        return;
                    case Key::Backspace:
                    case Key::Delete:
                    {
                        if (ReadOnly)
                        {
                            return;
                        }
                        size_t from = std::min(m_Caret, m_Anchor);
                        size_t to = std::max(m_Caret, m_Anchor);
                        if (from == to)
                        {
                            if (event.KeyCode == Key::Backspace)
                            {
                                from = kit::PreviousWord(Text, m_Caret);
                            }
                            else
                            {
                                to = kit::NextWord(Text, m_Caret);
                            }
                        }
                        if (from != to)
                        {
                            Replace(from, to, {}, false);
                            EnsureCaretVisible();
                        }
                        return;
                    }
                    case Key::Enter:
                        // Ctrl+Enter is left to the program (for "send" or
                        // "run") and does not insert a new line.
                        event.Handled = false;
                        return;
                    default:
                        event.Handled = false;
                        return;
                }
            }

            const int line = LineOf(m_Caret);
            const int pageLines = std::max(1, static_cast<int>(TextBounds().Height / lineHeight) - 1);

            auto Vertical = [&](int lines) {
                if (m_GoalX < 0.0f)
                {
                    m_GoalX = XOf(m_Caret);
                }
                const int target = line + lines;
                size_t offset = 0;
                if (target < 0)
                {
                    offset = 0;
                }
                else if (target >= static_cast<int>(m_Lines.size()))
                {
                    offset = Text.size();
                }
                else
                {
                    offset = OffsetAt(target, m_GoalX);
                }
                const float goal = m_GoalX;
                MoveCaret(offset, shift);
                m_GoalX = goal;
            };

            switch (event.KeyCode)
            {
                case Key::Left:
                    MoveCaret(HasSelection() && !shift ? std::min(m_Caret, m_Anchor)
                                                       : kit::PreviousCharacter(Text, m_Caret),
                              shift);
                    m_GoalX = -1.0f;
                    break;
                case Key::Right:
                    MoveCaret(HasSelection() && !shift ? std::max(m_Caret, m_Anchor) : kit::NextCharacter(Text, m_Caret),
                              shift);
                    m_GoalX = -1.0f;
                    break;
                case Key::Up:
                    Vertical(-1);
                    break;
                case Key::Down:
                    Vertical(1);
                    break;
                case Key::PageUp:
                    Vertical(-pageLines);
                    m_Scroll -= static_cast<float>(pageLines) * lineHeight;
                    break;
                case Key::PageDown:
                    Vertical(pageLines);
                    m_Scroll += static_cast<float>(pageLines) * lineHeight;
                    break;
                case Key::Home:
                    MoveCaret(m_Lines[static_cast<size_t>(line)].Start, shift);
                    m_GoalX = -1.0f;
                    break;
                case Key::End:
                    MoveCaret(OffsetAt(line, 1.0e9f), shift);
                    m_GoalX = -1.0f;
                    break;
                case Key::Enter:
                    if (ReadOnly)
                    {
                        break;
                    }
                    Replace(std::min(m_Caret, m_Anchor), std::max(m_Caret, m_Anchor), "\n", false);
                    break;
                case Key::Tab:
                    if (TabSpaces <= 0 || ReadOnly || shift)
                    {
                        // Tab goes back to moving focus.
                        event.Handled = false;
                        return;
                    }
                    Replace(std::min(m_Caret, m_Anchor), std::max(m_Caret, m_Anchor),
                            std::string(static_cast<size_t>(TabSpaces), ' '), false);
                    break;
                case Key::Backspace:
                {
                    if (ReadOnly)
                    {
                        break;
                    }
                    if (HasSelection())
                    {
                        Replace(std::min(m_Caret, m_Anchor), std::max(m_Caret, m_Anchor), {}, false);
                    }
                    else if (m_Caret > 0)
                    {
                        Replace(kit::PreviousCharacter(Text, m_Caret), m_Caret, {}, true);
                    }
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
                        Replace(std::min(m_Caret, m_Anchor), std::max(m_Caret, m_Anchor), {}, false);
                    }
                    else if (m_Caret < Text.size())
                    {
                        Replace(m_Caret, kit::NextCharacter(Text, m_Caret), {}, true);
                    }
                    break;
                }
                default:
                    event.Handled = false;
                    return;
            }

            Reflow();
            EnsureCaretVisible();
            break;
        }

        default:
            break;
    }
}

} // namespace opane
