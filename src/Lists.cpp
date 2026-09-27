// Lists and trees of selectable rows.
//
// Both draw only the rows in view, so their cost follows the height of the
// element rather than the length of the data. Written against the public API
// alone.

#include <opane/opane.h>

#include "WidgetKit.h"

#include <algorithm>
#include <cmath>

namespace opane
{
namespace
{

// Two presses within this long, on the same row, are a double-click.
constexpr float DoubleClickSeconds = 0.4f;

constexpr float ScrollBarWidth = 4.0f;

void PaintScrollBar(DrawList& drawList, const Theme& theme, const Rect& bounds, float scroll, float maximum)
{
    if (maximum <= 0.0f)
    {
        return;
    }
    const float view = bounds.Height;
    const float total = view + maximum;
    const float length = std::max(20.0f, view * view / total);
    const float y = bounds.Y + (view - length) * (scroll / maximum);
    drawList.FillRoundedRect(Rect{ bounds.X + bounds.Width - ScrollBarWidth - 3.0f, y + 2.0f, ScrollBarWidth, length - 4.0f },
                             ScrollBarWidth * 0.5f, kit::WithAlpha(theme.TextMuted, 0.55f));
}

} // namespace

// ---------------------------------------------------------------------------
// ListView
// ---------------------------------------------------------------------------

ListView::ListView()
{
    Size = Size2::Fill();
    Focusable = true;
    ClipChildren = true;
}

Vec2 ListView::Measure(Vec2 available)
{
    return Size.Resolve(available);
}

bool ListView::IsSelected(int index) const
{
    return std::find(m_Selection.begin(), m_Selection.end(), index) != m_Selection.end();
}

void ListView::SetSelection(const std::vector<int>& indices)
{
    m_Selection.clear();
    for (int index : indices)
    {
        if (index >= 0 && index < static_cast<int>(Items.size()) && !IsSelected(index))
        {
            m_Selection.push_back(index);
        }
    }
    Selected = m_Selection.empty() ? -1 : m_Selection.back();
    m_Anchor = Selected;
}

float ListView::MaximumScroll() const
{
    return std::max(0.0f, static_cast<float>(Items.size()) * RowHeight - GetBounds().Height);
}

int ListView::RowAt(Vec2 windowPoint) const
{
    const Rect bounds = GetBounds();
    if (!kit::Contains(bounds, windowPoint) || RowHeight <= 0.0f)
    {
        return -1;
    }
    const int row = static_cast<int>(std::floor((windowPoint.Y - bounds.Y + m_Scroll) / RowHeight));
    return row >= 0 && row < static_cast<int>(Items.size()) ? row : -1;
}

void ListView::ScrollTo(int index)
{
    if (index < 0 || index >= static_cast<int>(Items.size()))
    {
        return;
    }
    const float top = static_cast<float>(index) * RowHeight;
    const float view = GetBounds().Height;
    if (top < m_Scroll)
    {
        m_Scroll = top;
    }
    else if (top + RowHeight > m_Scroll + view)
    {
        m_Scroll = top + RowHeight - view;
    }
    m_Scroll = std::clamp(m_Scroll, 0.0f, MaximumScroll());
}

void ListView::SelectRow(int index, bool extend, bool toggle)
{
    if (index < 0 || index >= static_cast<int>(Items.size()))
    {
        return;
    }

    if (MultiSelect && extend && m_Anchor >= 0)
    {
        // Shift: everything between the anchor and here.
        m_Selection.clear();
        const int from = std::min(m_Anchor, index);
        const int to = std::max(m_Anchor, index);
        for (int row = from; row <= to; ++row)
        {
            m_Selection.push_back(row);
        }
    }
    else if (MultiSelect && toggle)
    {
        // Ctrl: this one in or out, the rest untouched.
        auto at = std::find(m_Selection.begin(), m_Selection.end(), index);
        if (at != m_Selection.end())
        {
            m_Selection.erase(at);
        }
        else
        {
            m_Selection.push_back(index);
        }
        m_Anchor = index;
    }
    else
    {
        m_Selection.assign(1, index);
        m_Anchor = index;
    }

    const bool changed = Selected != index;
    Selected = index;
    ScrollTo(index);
    if (changed && OnSelected)
    {
        kit::Invoke(OnSelected, index);
    }
}

void ListView::OnUpdate(float deltaSeconds)
{
    m_Clock += deltaSeconds;

    // Items may have shrunk since the selection was made.
    const int count = static_cast<int>(Items.size());
    m_Selection.erase(std::remove_if(m_Selection.begin(), m_Selection.end(),
                                     [count](int index) { return index < 0 || index >= count; }),
                      m_Selection.end());
    if (Selected >= count)
    {
        Selected = count - 1;
    }
    if (Selected >= 0 && m_Selection.empty())
    {
        m_Selection.push_back(Selected);
    }
    m_Scroll = std::clamp(m_Scroll, 0.0f, MaximumScroll());
}

const Style* ListView::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.ListView;
}

void ListView::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    if (!HasAppearance())
    {
        drawList.FillRect(bounds, theme.Background);
    }

    if (RowHeight <= 0.0f)
    {
        return;
    }

    drawList.PushClip(bounds);
    const int first = std::max(0, static_cast<int>(m_Scroll / RowHeight));
    const int last = std::min(static_cast<int>(Items.size()),
                              first + static_cast<int>(std::ceil(bounds.Height / RowHeight)) + 1);
    const bool focused = IsFocused();

    for (int row = first; row < last; ++row)
    {
        const Rect rect{ bounds.X, bounds.Y + static_cast<float>(row) * RowHeight - m_Scroll, bounds.Width, RowHeight };
        if (IsSelected(row))
        {
            drawList.FillRect(rect, focused ? theme.Accent : theme.SurfacePressed);
        }
        else if (row % 2 == 1)
        {
            drawList.FillRect(rect, kit::WithAlpha(theme.Surface, 0.45f));
        }

        const Color text = IsSelected(row) && focused ? Color{ 1.0f, 1.0f, 1.0f, 1.0f } : theme.Text;
        drawList.DrawTextInRect(Items[static_cast<size_t>(row)],
                                Rect{ rect.X + 10.0f, rect.Y, rect.Width - 20.0f, rect.Height }, theme.Font, text,
                                TextAlign::Left);
    }
    drawList.PopClip();

    PaintScrollBar(drawList, theme, bounds, m_Scroll, MaximumScroll());
    if (IsFocusVisible())
    {
        drawList.StrokeRect(kit::Inset(bounds, 1.0f), 2.0f, theme.Accent, 0.0f);
    }
}

void ListView::OnEvent(Event& event)
{
    const int count = static_cast<int>(Items.size());

    switch (event.Type)
    {
        case EventType::Wheel:
            m_Scroll = std::clamp(m_Scroll - event.WheelDelta * RowHeight * 3.0f, 0.0f, MaximumScroll());
            event.Handled = true;
            break;

        case EventType::PointerDown:
        {
            const int row = RowAt(event.Position);
            event.Handled = true;
            if (row < 0)
            {
                break;
            }

            App app = GetApp();
            const bool control = app.IsValid() && (app.GetInput().IsKeyDown(Key::LeftControl) ||
                                                   app.GetInput().IsKeyDown(Key::RightControl));
            const bool shift = app.IsValid() && (app.GetInput().IsKeyDown(Key::LeftShift) ||
                                                 app.GetInput().IsKeyDown(Key::RightShift));

            if (event.Button == MouseButton::Right)
            {
                // A right-click on an unselected row selects it first, so the
                // context menu is about what was clicked.
                if (!IsSelected(row))
                {
                    SelectRow(row, false, false);
                }
                if (OnContextMenu)
                {
                    kit::Invoke(OnContextMenu, row, event.Position);
                }
                break;
            }
            if (event.Button != MouseButton::Left)
            {
                break;
            }

            const bool doubled = row == m_LastClickRow && m_Clock - m_LastClick < DoubleClickSeconds;
            SelectRow(row, shift, control);
            if (doubled && OnActivated)
            {
                kit::Invoke(OnActivated, row);
                m_LastClick = -10.0f;
            }
            else
            {
                m_LastClick = m_Clock;
                m_LastClickRow = row;
            }
            break;
        }

        case EventType::KeyDown:
        {
            if (count == 0)
            {
                break;
            }
            const int page = std::max(1, static_cast<int>(GetBounds().Height / RowHeight) - 1);
            int target = -1;
            switch (event.KeyCode)
            {
                case Key::Down: target = std::min(count - 1, Selected + 1); break;
                case Key::Up: target = std::max(0, Selected < 0 ? 0 : Selected - 1); break;
                case Key::PageDown: target = std::min(count - 1, Selected + page); break;
                case Key::PageUp: target = std::max(0, Selected - page); break;
                case Key::Home: target = 0; break;
                case Key::End: target = count - 1; break;
                case Key::Enter:
                    if (!event.Repeat && Selected >= 0 && OnActivated)
                    {
                        kit::Invoke(OnActivated, Selected);
                    }
                    event.Handled = true;
                    return;
                case Key::A:
                    if (event.Control && MultiSelect)
                    {
                        m_Selection.clear();
                        for (int row = 0; row < count; ++row)
                        {
                            m_Selection.push_back(row);
                        }
                        event.Handled = true;
                    }
                    return;
                default:
                    return;
            }
            SelectRow(target, event.Shift, false);
            event.Handled = true;
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// TreeView
// ---------------------------------------------------------------------------

TreeView::TreeView()
{
    Size = Size2::Fill();
    Focusable = true;
    ClipChildren = true;
}

Vec2 TreeView::Measure(Vec2 available)
{
    return Size.Resolve(available);
}

void TreeView::Flatten()
{
    // The rows are rebuilt from the items every frame: items are data the
    // program may replace at any time, and walking them costs far less than
    // drawing them.
    m_Rows.clear();

    struct Frame
    {
        std::vector<Item>* List;
        size_t Index;
        int Depth;
    };
    std::vector<Frame> stack;
    stack.push_back(Frame{ &Items, 0, 0 });

    while (!stack.empty())
    {
        Frame& frame = stack.back();
        if (frame.Index >= frame.List->size())
        {
            stack.pop_back();
            continue;
        }
        Item& item = (*frame.List)[frame.Index++];
        m_Rows.push_back(Row{ &item, frame.Depth });
        if (item.Expanded && !item.Children.empty())
        {
            const int depth = frame.Depth + 1;
            stack.push_back(Frame{ &item.Children, 0, depth });
        }
    }
}

TreeView::Item* TreeView::Find(uint64_t id)
{
    std::vector<std::vector<Item>*> pending{ &Items };
    while (!pending.empty())
    {
        std::vector<Item>* list = pending.back();
        pending.pop_back();
        for (Item& item : *list)
        {
            if (item.Id == id)
            {
                return &item;
            }
            pending.push_back(&item.Children);
        }
    }
    return nullptr;
}

int TreeView::RowIndexOf(uint64_t id) const
{
    for (size_t row = 0; row < m_Rows.size(); ++row)
    {
        if (m_Rows[row].Target->Id == id)
        {
            return static_cast<int>(row);
        }
    }
    return -1;
}

float TreeView::MaximumScroll() const
{
    return std::max(0.0f, static_cast<float>(m_Rows.size()) * RowHeight - GetBounds().Height);
}

int TreeView::RowAt(Vec2 point) const
{
    const Rect bounds = GetBounds();
    if (!kit::Contains(bounds, point) || RowHeight <= 0.0f)
    {
        return -1;
    }
    const int row = static_cast<int>(std::floor((point.Y - bounds.Y + m_Scroll) / RowHeight));
    return row >= 0 && row < static_cast<int>(m_Rows.size()) ? row : -1;
}

void TreeView::ScrollToRow(int row)
{
    if (row < 0)
    {
        return;
    }
    const float top = static_cast<float>(row) * RowHeight;
    const float view = GetBounds().Height;
    if (top < m_Scroll)
    {
        m_Scroll = top;
    }
    else if (top + RowHeight > m_Scroll + view)
    {
        m_Scroll = top + RowHeight - view;
    }
    m_Scroll = std::clamp(m_Scroll, 0.0f, MaximumScroll());
}

void TreeView::SetExpanded(Item& item, bool expanded)
{
    if (item.Children.empty() || item.Expanded == expanded)
    {
        return;
    }
    item.Expanded = expanded;
    if (OnExpanded)
    {
        kit::Invoke(OnExpanded, item, expanded);
    }
    Flatten();
}

void TreeView::Select(uint64_t id)
{
    Item* item = Find(id);
    if (item == nullptr)
    {
        return;
    }
    const bool changed = Selected != id;
    Selected = id;
    Flatten();
    ScrollToRow(RowIndexOf(id));
    if (changed && OnSelected)
    {
        kit::Invoke(OnSelected, *item);
    }
}

void TreeView::ExpandAll(bool expanded)
{
    std::vector<std::vector<Item>*> pending{ &Items };
    while (!pending.empty())
    {
        std::vector<Item>* list = pending.back();
        pending.pop_back();
        for (Item& item : *list)
        {
            if (!item.Children.empty())
            {
                item.Expanded = expanded;
                pending.push_back(&item.Children);
            }
        }
    }
    Flatten();
}

void TreeView::Reveal(uint64_t id)
{
    // The path to the item, found depth first, then every step of it opened.
    std::vector<Item*> path;
    auto Search = [&](auto&& self, std::vector<Item>& list) -> bool {
        for (Item& item : list)
        {
            path.push_back(&item);
            if (item.Id == id || self(self, item.Children))
            {
                return true;
            }
            path.pop_back();
        }
        return false;
    };
    if (!Search(Search, Items))
    {
        return;
    }
    for (size_t step = 0; step + 1 < path.size(); ++step)
    {
        path[step]->Expanded = true;
    }
    Flatten();
    ScrollToRow(RowIndexOf(id));
}

void TreeView::OnUpdate(float deltaSeconds)
{
    m_Clock += deltaSeconds;
    Flatten();
    m_Scroll = std::clamp(m_Scroll, 0.0f, MaximumScroll());
}

const Style* TreeView::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.ListView;
}

void TreeView::Paint(DrawList& drawList)
{
    // Rebuilt here too: the rows point into Items, which the program may have
    // replaced since the update.
    Flatten();

    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    if (!HasAppearance())
    {
        drawList.FillRect(bounds, theme.Background);
    }
    if (RowHeight <= 0.0f)
    {
        return;
    }

    drawList.PushClip(bounds);
    const int first = std::max(0, static_cast<int>(m_Scroll / RowHeight));
    const int last = std::min(static_cast<int>(m_Rows.size()),
                              first + static_cast<int>(std::ceil(bounds.Height / RowHeight)) + 1);
    const bool focused = IsFocused();

    for (int row = first; row < last; ++row)
    {
        const Row& entry = m_Rows[static_cast<size_t>(row)];
        const Item& item = *entry.Target;
        const Rect rect{ bounds.X, bounds.Y + static_cast<float>(row) * RowHeight - m_Scroll, bounds.Width, RowHeight };
        const bool selected = Selected != 0 && item.Id == Selected;

        if (selected)
        {
            drawList.FillRect(rect, focused ? theme.Accent : theme.SurfacePressed);
        }

        const float indent = 8.0f + static_cast<float>(entry.Depth) * Indent;
        const Color text = selected && focused ? Color{ 1.0f, 1.0f, 1.0f, 1.0f } : theme.Text;
        if (!item.Children.empty())
        {
            kit::DrawChevron(drawList, Vec2{ rect.X + indent + 5.0f, rect.Y + rect.Height * 0.5f }, 8.0f,
                             item.Expanded ? kit::Direction::Down : kit::Direction::Right,
                             selected && focused ? text : theme.TextMuted, 1.5f);
        }
        drawList.DrawTextInRect(item.Text,
                                Rect{ rect.X + indent + 16.0f, rect.Y, rect.Width - indent - 24.0f, rect.Height },
                                theme.Font, text, TextAlign::Left);
    }
    drawList.PopClip();

    PaintScrollBar(drawList, theme, bounds, m_Scroll, MaximumScroll());
    if (IsFocusVisible())
    {
        drawList.StrokeRect(kit::Inset(bounds, 1.0f), 2.0f, theme.Accent, 0.0f);
    }
}

void TreeView::OnEvent(Event& event)
{
    // The rows point into Items, and a callback run by an earlier event may
    // have rebuilt them.
    Flatten();

    switch (event.Type)
    {
        case EventType::Wheel:
            m_Scroll = std::clamp(m_Scroll - event.WheelDelta * RowHeight * 3.0f, 0.0f, MaximumScroll());
            event.Handled = true;
            break;

        case EventType::PointerDown:
        {
            event.Handled = true;
            const int row = RowAt(event.Position);
            if (row < 0)
            {
                break;
            }
            Row entry = m_Rows[static_cast<size_t>(row)];
            Item& item = *entry.Target;

            // The disclosure arrow opens and closes without selecting.
            const float indent = GetBounds().X + 8.0f + static_cast<float>(entry.Depth) * Indent;
            if (event.Button == MouseButton::Left && !item.Children.empty() && event.Position.X >= indent - 4.0f &&
                event.Position.X <= indent + 14.0f)
            {
                SetExpanded(item, !item.Expanded);
                break;
            }

            if (event.Button == MouseButton::Right)
            {
                Select(item.Id);
                if (OnContextMenu)
                {
                    if (Item* found = Find(item.Id))
                    {
                        kit::Invoke(OnContextMenu, *found, event.Position);
                    }
                }
                break;
            }
            if (event.Button != MouseButton::Left)
            {
                break;
            }

            const uint64_t id = item.Id;
            const bool doubled = id == m_LastClickId && m_Clock - m_LastClick < DoubleClickSeconds;
            Select(id);
            if (doubled)
            {
                m_LastClick = -10.0f;
                if (Item* found = Find(id))
                {
                    // A double-click opens a branch, and activates a leaf.
                    if (!found->Children.empty())
                    {
                        SetExpanded(*found, !found->Expanded);
                    }
                    else if (OnActivated)
                    {
                        kit::Invoke(OnActivated, *found);
                    }
                }
            }
            else
            {
                m_LastClick = m_Clock;
                m_LastClickId = id;
            }
            break;
        }

        case EventType::KeyDown:
        {
            if (m_Rows.empty())
            {
                break;
            }
            const int current = RowIndexOf(Selected);
            const int count = static_cast<int>(m_Rows.size());
            const int page = std::max(1, static_cast<int>(GetBounds().Height / RowHeight) - 1);

            auto SelectRow = [&](int row) {
                row = std::clamp(row, 0, count - 1);
                Select(m_Rows[static_cast<size_t>(row)].Target->Id);
            };

            switch (event.KeyCode)
            {
                case Key::Down: SelectRow(current < 0 ? 0 : current + 1); break;
                case Key::Up: SelectRow(current < 0 ? 0 : current - 1); break;
                case Key::PageDown: SelectRow(current + page); break;
                case Key::PageUp: SelectRow(current - page); break;
                case Key::Home: SelectRow(0); break;
                case Key::End: SelectRow(count - 1); break;

                case Key::Right:
                {
                    // Opens a branch; on an open branch, moves to its first child.
                    if (current < 0)
                    {
                        break;
                    }
                    Item& item = *m_Rows[static_cast<size_t>(current)].Target;
                    if (!item.Children.empty() && !item.Expanded)
                    {
                        SetExpanded(item, true);
                    }
                    else if (!item.Children.empty())
                    {
                        SelectRow(current + 1);
                    }
                    break;
                }

                case Key::Left:
                {
                    // Closes a branch; on a closed one or a leaf, moves to the parent.
                    if (current < 0)
                    {
                        break;
                    }
                    Item& item = *m_Rows[static_cast<size_t>(current)].Target;
                    if (!item.Children.empty() && item.Expanded)
                    {
                        SetExpanded(item, false);
                        break;
                    }
                    const int depth = m_Rows[static_cast<size_t>(current)].Depth;
                    for (int row = current - 1; row >= 0; --row)
                    {
                        if (m_Rows[static_cast<size_t>(row)].Depth < depth)
                        {
                            SelectRow(row);
                            break;
                        }
                    }
                    break;
                }

                case Key::Enter:
                    if (!event.Repeat && current >= 0 && OnActivated)
                    {
                        kit::Invoke(OnActivated, *m_Rows[static_cast<size_t>(current)].Target);
                    }
                    break;

                default:
                    return;
            }
            event.Handled = true;
            break;
        }

        default:
            break;
    }
}

} // namespace opane
