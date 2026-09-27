// Containers that arrange what they hold: a splitter, a tab view, and a
// floating window.
//
// Written against the public API alone. Each uses a Custom layout or its own
// content box, which is the same extension point a program has for layouts of
// its own.

#include <opane/opane.h>

#include "WidgetKit.h"

#include <algorithm>
#include <cmath>

namespace opane
{

// ---------------------------------------------------------------------------
// Splitter
// ---------------------------------------------------------------------------

namespace
{

// The divider is easier to catch than it is to see: this much either side of
// it still grabs it.
constexpr float DividerSlop = 3.0f;

} // namespace

Splitter::Splitter()
{
    ChildLayout = LayoutMode::Custom;
    Size = Size2::Fill();
}

float Splitter::FirstExtent(const Rect& content) const
{
    const float total = std::max(0.0f, (Stacked ? content.Height : content.Width) - DividerThickness);
    const float wanted = std::clamp(Ratio, 0.0f, 1.0f) * total;

    // Neither pane is squeezed below its minimum while there is room for
    // both; when there is not, the ratio decides alone.
    if (total >= MinimumPane * 2.0f)
    {
        return std::clamp(wanted, MinimumPane, total - MinimumPane);
    }
    return wanted;
}

Rect Splitter::GetDividerBounds() const
{
    const Rect content = GetContentBounds();
    const float first = FirstExtent(content);
    if (Stacked)
    {
        return Rect{ content.X, content.Y + first, content.Width, DividerThickness };
    }
    return Rect{ content.X + first, content.Y, DividerThickness, content.Height };
}

void Splitter::Arrange(const Rect& bounds)
{
    Element::Arrange(bounds);
    Cursor = Stacked ? CursorShape::ResizeVertical : CursorShape::ResizeHorizontal;
}

Rect Splitter::PlaceChild(Element&, size_t index, const Rect& content)
{
    const float first = FirstExtent(content);
    if (index == 0)
    {
        return Stacked ? Rect{ content.X, content.Y, content.Width, first }
                       : Rect{ content.X, content.Y, first, content.Height };
    }
    if (index == 1)
    {
        const float offset = first + DividerThickness;
        return Stacked ? Rect{ content.X, content.Y + offset, content.Width, std::max(0.0f, content.Height - offset) }
                       : Rect{ content.X + offset, content.Y, std::max(0.0f, content.Width - offset), content.Height };
    }

    // A third child has nowhere to go. It is given no room rather than laid
    // over a pane.
    return Rect{ content.X, content.Y, 0.0f, 0.0f };
}

bool Splitter::HitTest(Vec2 point) const
{
    // Only the divider belongs to the splitter itself; everywhere else is a
    // pane, and a press there is the pane's.
    return kit::Contains(kit::Inset(GetDividerBounds(), -DividerSlop), point) || m_Dragging;
}

void Splitter::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect divider = GetDividerBounds();

    const bool active = m_Dragging || IsHovered();
    const Color line = active ? theme.Accent : theme.Border;
    if (Stacked)
    {
        drawList.FillRect(Rect{ divider.X, divider.Y + divider.Height * 0.5f - (active ? 1.0f : 0.5f), divider.Width,
                                active ? 2.0f : 1.0f },
                          line);
    }
    else
    {
        drawList.FillRect(Rect{ divider.X + divider.Width * 0.5f - (active ? 1.0f : 0.5f), divider.Y,
                                active ? 2.0f : 1.0f, divider.Height },
                          line);
    }
}

void Splitter::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerDown:
        {
            if (event.Button != MouseButton::Left)
            {
                break;
            }
            const Rect divider = GetDividerBounds();
            m_Dragging = true;
            m_GrabOffset = Stacked ? event.Position.Y - divider.Y : event.Position.X - divider.X;
            event.Handled = true;
            break;
        }

        case EventType::PointerMove:
        {
            if (!m_Dragging)
            {
                break;
            }
            const Rect content = GetContentBounds();
            const float total = std::max(1.0f, (Stacked ? content.Height : content.Width) - DividerThickness);
            const float start = Stacked ? content.Y : content.X;
            const float position = (Stacked ? event.Position.Y : event.Position.X) - m_GrabOffset - start;

            float ratio = std::clamp(position / total, 0.0f, 1.0f);
            if (total >= MinimumPane * 2.0f)
            {
                ratio = std::clamp(ratio, MinimumPane / total, 1.0f - MinimumPane / total);
            }
            if (ratio != Ratio)
            {
                Ratio = ratio;
                if (OnRatioChanged)
                {
                    kit::Invoke(OnRatioChanged, Ratio);
                }
            }
            event.Handled = true;
            break;
        }

        case EventType::PointerUp:
            if (m_Dragging)
            {
                m_Dragging = false;
                event.Handled = true;
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// TabView
// ---------------------------------------------------------------------------

namespace
{

constexpr float TabMinimumWidth = 44.0f;
constexpr float TabMaximumWidth = 220.0f;
constexpr float TabCloseSize = 16.0f;

} // namespace

TabView::TabView()
{
    ChildLayout = LayoutMode::Custom;
    Size = Size2::Fill();
}

Element* TabView::AddPage(const std::string& title)
{
    Element* page = Add<Element>();
    page->Name = title;
    SetTitle(page, title);
    page->Visible = GetPageCount() - 1 == Active;
    return page;
}

void TabView::RemovePage(int index)
{
    if (Element* page = GetPage(index))
    {
        Remove(page);
    }
}

int TabView::GetPageCount() const
{
    return static_cast<int>(GetChildren().size());
}

Element* TabView::GetPage(int index) const
{
    const auto& children = GetChildren();
    if (index < 0 || index >= static_cast<int>(children.size()))
    {
        return nullptr;
    }
    return children[static_cast<size_t>(index)];
}

int TabView::IndexOf(const Element* page) const
{
    const auto& children = GetChildren();
    const auto at = std::find(children.begin(), children.end(), page);
    return at == children.end() ? -1 : static_cast<int>(at - children.begin());
}

std::string TabView::GetTitle(int index) const
{
    const Element* page = GetPage(index);
    if (page == nullptr)
    {
        return {};
    }
    for (const auto& entry : m_Titles)
    {
        if (entry.first == page)
        {
            return entry.second;
        }
    }
    // A child added without AddPage is titled by its name.
    return page->Name;
}

void TabView::SetTitle(const Element* page, const std::string& title)
{
    for (auto& entry : m_Titles)
    {
        if (entry.first == page)
        {
            entry.second = title;
            return;
        }
    }
    m_Titles.emplace_back(page, title);
}

void TabView::Activate(int index)
{
    const int count = GetPageCount();
    if (count == 0)
    {
        Active = 0;
        return;
    }
    index = std::clamp(index, 0, count - 1);

    // Visibility is set at once rather than at the next update, so the page
    // a click chose is the one the same frame draws.
    for (int page = 0; page < count; ++page)
    {
        GetPage(page)->Visible = page == index;
    }

    if (index != Active)
    {
        Active = index;
        if (OnActiveChanged)
        {
            kit::Invoke(OnActiveChanged, Active);
        }
    }
}

Rect TabView::GetContentBounds() const
{
    const Rect bounds = GetBounds();
    return Rect{ bounds.X + Padding, bounds.Y + TabHeight + Padding, std::max(0.0f, bounds.Width - Padding * 2.0f),
                 std::max(0.0f, bounds.Height - TabHeight - Padding * 2.0f) };
}

Rect TabView::PlaceChild(Element&, size_t, const Rect& content)
{
    return content;
}

Rect TabView::GetTabBounds(int index) const
{
    const int count = GetPageCount();
    if (index < 0 || index >= count)
    {
        return Rect{};
    }

    const Rect bounds = GetBounds();
    const float extra = Closable ? TabCloseSize + 8.0f : 0.0f;

    // Each tab as wide as its title wants, then all of them squeezed alike if
    // together they overflow the strip.
    float total = 0.0f;
    for (int tab = 0; tab < count; ++tab)
    {
        total += std::clamp(MeasureText(GetTitle(tab)).X + 24.0f + extra, TabMinimumWidth, TabMaximumWidth);
    }
    const float available = std::max(1.0f, bounds.Width - 4.0f);
    const float squeeze = total > available ? available / total : 1.0f;

    float x = bounds.X + 2.0f;
    for (int tab = 0; tab < count; ++tab)
    {
        const float width =
            std::clamp(MeasureText(GetTitle(tab)).X + 24.0f + extra, TabMinimumWidth, TabMaximumWidth) * squeeze;
        if (tab == index)
        {
            return Rect{ x, bounds.Y + 3.0f, width, TabHeight - 3.0f };
        }
        x += width;
    }
    return Rect{};
}

Rect TabView::CloseBounds(int index) const
{
    const Rect tab = GetTabBounds(index);
    return Rect{ tab.X + tab.Width - TabCloseSize - 6.0f, tab.Y + (tab.Height - TabCloseSize) * 0.5f, TabCloseSize,
                 TabCloseSize };
}

void TabView::OnUpdate(float)
{
    const int count = GetPageCount();
    if (count == 0)
    {
        return;
    }
    const int clamped = std::clamp(Active, 0, count - 1);
    for (int page = 0; page < count; ++page)
    {
        GetPage(page)->Visible = page == clamped;
    }
    if (clamped != Active)
    {
        Activate(clamped);
    }
}

void TabView::OnChildRemoved(Element& child)
{
    const int index = IndexOf(&child);
    m_Titles.erase(std::remove_if(m_Titles.begin(), m_Titles.end(),
                                  [&](const auto& entry) { return entry.first == &child; }),
                   m_Titles.end());

    // The tab after a closed one moves into its place, so the page shown
    // after closing the active tab is its right-hand neighbour, or the last
    // one when there is none.
    if (index >= 0 && index < Active)
    {
        --Active;
    }
    const int remaining = GetPageCount() - 1;
    if (remaining > 0)
    {
        Active = std::clamp(Active, 0, remaining - 1);
    }
    else
    {
        Active = 0;
    }
    m_HoverTab = -1;
    m_HoverClose = -1;
}

void TabView::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();

    drawList.FillRect(Rect{ bounds.X, bounds.Y, bounds.Width, TabHeight }, theme.Surface);
    drawList.FillRect(Rect{ bounds.X, bounds.Y + TabHeight - 1.0f, bounds.Width, 1.0f }, theme.Border);

    const int count = GetPageCount();
    for (int tab = 0; tab < count; ++tab)
    {
        const Rect rect = GetTabBounds(tab);
        const bool active = tab == Active;

        if (active)
        {
            drawList.FillRect(rect, theme.Background);
            drawList.FillRect(Rect{ rect.X, rect.Y, rect.Width, 2.0f }, theme.Accent);
        }
        else if (tab == m_HoverTab)
        {
            drawList.FillRect(rect, theme.SurfaceHovered);
        }

        const bool showClose = Closable && (active || tab == m_HoverTab);
        const float closeRoom = Closable ? TabCloseSize + 8.0f : 0.0f;
        const Rect textRect{ rect.X + 10.0f, rect.Y, std::max(0.0f, rect.Width - 20.0f - closeRoom), rect.Height };
        const std::string title =
            kit::Ellipsize(GetTitle(tab), textRect.Width, [this](const std::string& text) { return MeasureText(text).X; });

        drawList.PushClip(rect);
        drawList.DrawTextInRect(title, textRect, theme.Font, active ? theme.Text : theme.TextMuted, TextAlign::Left);
        drawList.PopClip();

        if (showClose)
        {
            const Rect close = CloseBounds(tab);
            if (tab == m_HoverClose)
            {
                drawList.FillRoundedRect(close, 4.0f, theme.SurfacePressed);
            }
            kit::DrawCross(drawList, kit::Center(close), 7.0f, theme.TextMuted, 1.4f);
        }

        // A hairline between inactive neighbours keeps a long row legible.
        if (!active && tab + 1 != Active && tab + 1 < count)
        {
            drawList.FillRect(Rect{ rect.X + rect.Width - 0.5f, rect.Y + 7.0f, 1.0f, rect.Height - 14.0f }, theme.Border);
        }
    }
}

void TabView::OnEvent(Event& event)
{
    const int count = GetPageCount();

    auto TabAt = [&](Vec2 point) {
        for (int tab = 0; tab < count; ++tab)
        {
            if (kit::Contains(GetTabBounds(tab), point))
            {
                return tab;
            }
        }
        return -1;
    };

    auto RequestClose = [&](int tab) {
        if (tab < 0 || tab >= count)
        {
            return;
        }
        if (!OnCloseRequested || kit::Invoke(OnCloseRequested, tab))
        {
            RemovePage(tab);
        }
    };

    switch (event.Type)
    {
        case EventType::PointerMove:
        {
            m_HoverTab = TabAt(event.Position);
            m_HoverClose = Closable && m_HoverTab >= 0 && kit::Contains(CloseBounds(m_HoverTab), event.Position)
                               ? m_HoverTab
                               : -1;
            break;
        }

        case EventType::PointerLeave:
            m_HoverTab = -1;
            m_HoverClose = -1;
            break;

        case EventType::PointerDown:
        {
            const int tab = TabAt(event.Position);
            if (tab < 0)
            {
                break;
            }
            if (event.Button == MouseButton::Left &&
                !(Closable && kit::Contains(CloseBounds(tab), event.Position)))
            {
                Activate(tab);
            }
            event.Handled = true;
            break;
        }

        case EventType::PointerUp:
        {
            const int tab = TabAt(event.Position);
            if (tab < 0)
            {
                break;
            }
            // The close button, or a middle click anywhere on the tab.
            if (Closable && ((event.Button == MouseButton::Left && kit::Contains(CloseBounds(tab), event.Position)) ||
                             event.Button == MouseButton::Middle))
            {
                RequestClose(tab);
            }
            event.Handled = true;
            break;
        }

        case EventType::KeyDown:
        {
            // Ctrl+Tab and Ctrl+Shift+Tab from anywhere inside the pages.
            if (event.Control && event.KeyCode == Key::Tab && count > 0)
            {
                Activate((Active + (event.Shift ? count - 1 : 1)) % count);
                event.Handled = true;
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

namespace
{

constexpr float GripSize = 6.0f;
constexpr float WindowCloseSize = 18.0f;

// However a window is moved, this much of its title stays on screen, so it
// can always be caught and moved back.
constexpr float KeepVisible = 48.0f;

} // namespace

Window::Window()
{
    Size = Size2::FromOffset(420.0f, 300.0f);
    Padding = 8.0f;
    ClipChildren = true;
}

void Window::SetFrame(const Rect& frame)
{
    AnchorPoint = Vec2{};
    Position = Position2::FromOffset(frame.X, frame.Y);
    Size = Size2::FromOffset(std::max(frame.Width, MinimumSize.X), std::max(frame.Height, MinimumSize.Y));
}

Rect Window::GetFrame() const
{
    return Rect{ Position.X.Offset, Position.Y.Offset, Size.X.Offset, Size.Y.Offset };
}

Rect Window::GetTitleBounds() const
{
    const Rect bounds = GetBounds();
    return Rect{ bounds.X, bounds.Y, bounds.Width, TitleHeight };
}

Rect Window::GetContentBounds() const
{
    const Rect bounds = GetBounds();
    return Rect{ bounds.X + Padding, bounds.Y + TitleHeight + Padding, std::max(0.0f, bounds.Width - Padding * 2.0f),
                 std::max(0.0f, bounds.Height - TitleHeight - Padding * 2.0f) };
}

Rect Window::CloseBounds() const
{
    const Rect title = GetTitleBounds();
    return Rect{ title.X + title.Width - WindowCloseSize - 7.0f, title.Y + (title.Height - WindowCloseSize) * 0.5f,
                 WindowCloseSize, WindowCloseSize };
}

Window::Grip Window::GripAt(Vec2 point) const
{
    const Rect bounds = GetBounds();
    if (!kit::Contains(kit::Inset(bounds, -1.0f), point))
    {
        return Grip::None;
    }

    if (Resizable)
    {
        const bool left = point.X < bounds.X + GripSize;
        const bool right = point.X > bounds.X + bounds.Width - GripSize;
        const bool bottom = point.Y > bounds.Y + bounds.Height - GripSize;
        if (bottom && left)
        {
            return Grip::BottomLeft;
        }
        if (bottom && right)
        {
            return Grip::BottomRight;
        }
        if (bottom)
        {
            return Grip::Bottom;
        }
        if (left && point.Y > bounds.Y + TitleHeight)
        {
            return Grip::Left;
        }
        if (right && point.Y > bounds.Y + TitleHeight)
        {
            return Grip::Right;
        }
    }

    if (Movable && kit::Contains(GetTitleBounds(), point) && !(Closable && kit::Contains(CloseBounds(), point)))
    {
        return Grip::Move;
    }
    return Grip::None;
}

void Window::OnMoved(Vec2)
{
}

void Window::OnMoveEnded(Vec2)
{
}

void Window::BeginMove(Vec2 windowPoint)
{
    m_Grip = Grip::Move;
    m_GrabPoint = windowPoint;
    m_GrabFrame = GetFrame();
    CapturePointer();
}

const Style* Window::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.Window;
}

void Window::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    const float radius = theme.CornerRadius;
    const Rect title = GetTitleBounds();

    // With a look, the look is the whole window; the title is text on it.
    if (!HasAppearance())
    {
        kit::DrawRaised(drawList, theme, bounds, radius);

        // The title bar: a shade darker than the body, rounded only at the top.
        drawList.PushClip(title);
        drawList.FillRoundedRect(Rect{ title.X, title.Y, title.Width, title.Height + radius }, radius,
                                 theme.SurfacePressed);
        drawList.PopClip();
        drawList.FillRect(Rect{ title.X, title.Y + title.Height - 1.0f, title.Width, 1.0f }, theme.Border);
    }

    const float closeRoom = Closable ? WindowCloseSize + 14.0f : 0.0f;
    const Rect titleText{ title.X + 12.0f, title.Y, std::max(0.0f, title.Width - 24.0f - closeRoom), title.Height };
    drawList.PushClip(titleText);
    drawList.DrawTextInRect(
        kit::Ellipsize(Title, titleText.Width, [this](const std::string& text) { return MeasureText(text).X; }),
        titleText, theme.Font, kit::LookText(*this, theme.Text), TextAlign::Left);
    drawList.PopClip();

    if (Closable)
    {
        const Rect close = CloseBounds();
        if (m_HoverClose)
        {
            drawList.FillRoundedRect(close, 5.0f, Color{ 0.85f, 0.25f, 0.25f, 0.9f });
        }
        kit::DrawCross(drawList, kit::Center(close), 8.0f, m_HoverClose ? Color{ 1, 1, 1, 1 } : theme.TextMuted, 1.5f);
    }
}

void Window::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerMove:
        {
            if (m_Grip == Grip::None)
            {
                m_HoverClose = Closable && kit::Contains(CloseBounds(), event.Position);
                switch (GripAt(event.Position))
                {
                    case Grip::Left:
                    case Grip::Right: Cursor = CursorShape::ResizeHorizontal; break;
                    case Grip::Bottom: Cursor = CursorShape::ResizeVertical; break;
                    case Grip::BottomLeft:
                    case Grip::BottomRight: Cursor = CursorShape::ResizeDiagonal; break;
                    default: Cursor = CursorShape::Default; break;
                }
                break;
            }

            const float dx = event.Position.X - m_GrabPoint.X;
            const float dy = event.Position.Y - m_GrabPoint.Y;
            Rect frame = m_GrabFrame;

            switch (m_Grip)
            {
                case Grip::Move:
                    frame.X += dx;
                    frame.Y += dy;
                    break;
                case Grip::Right:
                    frame.Width = std::max(MinimumSize.X, m_GrabFrame.Width + dx);
                    break;
                case Grip::Bottom:
                    frame.Height = std::max(MinimumSize.Y, m_GrabFrame.Height + dy);
                    break;
                case Grip::BottomRight:
                    frame.Width = std::max(MinimumSize.X, m_GrabFrame.Width + dx);
                    frame.Height = std::max(MinimumSize.Y, m_GrabFrame.Height + dy);
                    break;
                case Grip::Left:
                case Grip::BottomLeft:
                {
                    // Dragging the left edge moves it and keeps the right edge
                    // where it was.
                    const float width = std::max(MinimumSize.X, m_GrabFrame.Width - dx);
                    frame.X = m_GrabFrame.X + (m_GrabFrame.Width - width);
                    frame.Width = width;
                    if (m_Grip == Grip::BottomLeft)
                    {
                        frame.Height = std::max(MinimumSize.Y, m_GrabFrame.Height + dy);
                    }
                    break;
                }
                default:
                    break;
            }

            // Some of the title always stays on screen. The frame is in the
            // parent's content box, which is also what its size is measured
            // against.
            if (const Element* parent = GetParent())
            {
                const Rect area = parent->GetContentBounds();
                frame.X = std::clamp(frame.X, -frame.Width + KeepVisible, std::max(0.0f, area.Width - KeepVisible));
                frame.Y = std::clamp(frame.Y, 0.0f, std::max(0.0f, area.Height - TitleHeight));
            }

            SetFrame(frame);
            if (m_Grip == Grip::Move)
            {
                OnMoved(event.Position);
            }
            event.Handled = true;
            break;
        }

        case EventType::PointerLeave:
            m_HoverClose = false;
            break;

        case EventType::PointerDown:
        {
            if (event.Button != MouseButton::Left)
            {
                event.Handled = true;
                break;
            }
            if (Closable && kit::Contains(CloseBounds(), event.Position))
            {
                event.Handled = true;
                break;
            }
            m_Grip = GripAt(event.Position);
            if (m_Grip != Grip::None)
            {
                m_GrabPoint = event.Position;
                m_GrabFrame = GetFrame();
                if (m_Grip == Grip::Move)
                {
                    Cursor = CursorShape::Move;
                }
            }
            event.Handled = true;
            break;
        }

        case EventType::PointerUp:
        {
            const Grip grip = m_Grip;
            m_Grip = Grip::None;
            if (grip == Grip::Move)
            {
                Cursor = CursorShape::Default;
                OnMoveEnded(event.Position);
            }
            else if (grip == Grip::None && event.Button == MouseButton::Left && Closable &&
                     kit::Contains(CloseBounds(), event.Position))
            {
                if (!OnCloseRequested || OnCloseRequested())
                {
                    Visible = false;
                    if (OnClosed)
                    {
                        kit::Invoke(OnClosed);
                    }
                }
            }
            event.Handled = true;
            break;
        }

        default:
            break;
    }
}

} // namespace opane
