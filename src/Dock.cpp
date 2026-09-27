// Docking: panels arranged by dragging, into tabs, splits, and floating
// windows, with the arrangement saved to text and restored.
//
// The arrangement is a binary tree kept beside the element tree. Its leaves
// are groups of tabs, its branches are splits with a ratio, and the panels
// themselves stay ordinary elements: children of the dock space while docked
// or closed, children of a floating window while floating. Moving a panel
// between those never recreates it, so its contents (a half-typed field, a
// scrolled list, a running world) are kept.

#include <opane/opane.h>

#include "WidgetKit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace opane
{

// ---------------------------------------------------------------------------
// The arrangement
// ---------------------------------------------------------------------------

struct DockSpace::Node
{
    enum class Kind
    {
        Split,
        Tabs
    };

    Kind Type = Kind::Tabs;
    Node* Parent = nullptr;

    // A split: two children side by side, or stacked, and the first one's
    // share of the space.
    bool Stacked = false;
    float Ratio = 0.5f;
    std::unique_ptr<Node> First;
    std::unique_ptr<Node> Second;

    // A group: its tabs, in order, and the one showing.
    std::vector<DockPanel*> Panels;
    int Active = 0;

    // Worked out at layout.
    Rect Bounds;
    Rect Strip;
    Rect Content;
    Rect Divider;

    DockPanel* ActivePanel() const
    {
        if (Panels.empty())
        {
            return nullptr;
        }
        return Panels[static_cast<size_t>(std::clamp(Active, 0, static_cast<int>(Panels.size()) - 1))];
    }
};

namespace
{

using Node = DockSpace::Node;

constexpr float TabPadding = 12.0f;
constexpr float TabCloseSize = 15.0f;
constexpr float TabMinimum = 48.0f;
constexpr float TabMaximum = 200.0f;

// A tab has to be dragged this far before it tears away; less is a click.
constexpr float TearDistance = 7.0f;

// Dragged tabs stay in their strip for reordering until the pointer is this
// far above or below it.
constexpr float StripGrace = 14.0f;

constexpr float GuideSize = 30.0f;
constexpr float GuideGap = 5.0f;

template <typename Visit>
void ForEachGroup(Node* node, Visit&& visit)
{
    if (node == nullptr)
    {
        return;
    }
    if (node->Type == Node::Kind::Tabs)
    {
        visit(node);
        return;
    }
    ForEachGroup(node->First.get(), visit);
    ForEachGroup(node->Second.get(), visit);
}

// The five places around a group's centre a panel can be dropped.
Rect GuideRect(const Rect& area, DockSide side)
{
    const Vec2 center = kit::Center(area);
    const float step = GuideSize + GuideGap;
    Vec2 at = center;
    switch (side)
    {
        case DockSide::Center: break;
        case DockSide::Left:   at.X -= step; break;
        case DockSide::Right:  at.X += step; break;
        case DockSide::Top:    at.Y -= step; break;
        case DockSide::Bottom: at.Y += step; break;
    }
    return Rect{ at.X - GuideSize * 0.5f, at.Y - GuideSize * 0.5f, GuideSize, GuideSize };
}

// The four at the edges of the whole space, for docking beside everything.
Rect EdgeGuideRect(const Rect& area, DockSide side)
{
    const Vec2 center = kit::Center(area);
    const float inset = 10.0f;
    switch (side)
    {
        case DockSide::Left:
            return Rect{ area.X + inset, center.Y - GuideSize * 0.5f, GuideSize, GuideSize };
        case DockSide::Right:
            return Rect{ area.X + area.Width - inset - GuideSize, center.Y - GuideSize * 0.5f, GuideSize, GuideSize };
        case DockSide::Top:
            return Rect{ center.X - GuideSize * 0.5f, area.Y + inset, GuideSize, GuideSize };
        case DockSide::Bottom:
            return Rect{ center.X - GuideSize * 0.5f, area.Y + area.Height - inset - GuideSize, GuideSize, GuideSize };
        default:
            return Rect{};
    }
}

Rect SideOf(const Rect& area, DockSide side, float share)
{
    switch (side)
    {
        case DockSide::Left:   return Rect{ area.X, area.Y, area.Width * share, area.Height };
        case DockSide::Right:  return Rect{ area.X + area.Width * (1.0f - share), area.Y, area.Width * share, area.Height };
        case DockSide::Top:    return Rect{ area.X, area.Y, area.Width, area.Height * share };
        case DockSide::Bottom: return Rect{ area.X, area.Y + area.Height * (1.0f - share), area.Width, area.Height * share };
        default:               return area;
    }
}

// Panel ids go into a line of text separated by spaces, so anything that is
// not plainly printable is escaped.
std::string Escape(const std::string& text)
{
    std::string out;
    for (unsigned char c : text)
    {
        if (c <= ' ' || c == '%' || c >= 0x7F)
        {
            char code[4];
            std::snprintf(code, sizeof(code), "%%%02X", c);
            out += code;
        }
        else
        {
            out.push_back(static_cast<char>(c));
        }
    }
    return out.empty() ? "%00" : out;
}

std::string Unescape(const std::string& text)
{
    if (text == "%00")
    {
        return {};
    }
    std::string out;
    for (size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] == '%' && index + 2 < text.size())
        {
            const std::string hex = text.substr(index + 1, 2);
            out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            index += 2;
        }
        else
        {
            out.push_back(text[index]);
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// DockPanel
// ---------------------------------------------------------------------------

DockPanel::DockPanel()
{
    Size = Size2::Fill();
    ClipChildren = true;
}

void DockPanel::Paint(DrawList& drawList)
{
    drawList.FillRect(GetBounds(), GetTheme().Background);
}

// ---------------------------------------------------------------------------
// The floating window a panel is torn off into
// ---------------------------------------------------------------------------

class DockSpace::FloatWindow : public Window
{
public:
    DockSpace* Owner = nullptr;

    explicit FloatWindow(DockSpace* owner) : Owner(owner)
    {
        // Just enough that the panel's square corners sit inside the
        // window's rounded ones.
        Padding = 3.0f;
        MinimumSize = Vec2{ 180.0f, 120.0f };
    }

    ~FloatWindow() override
    {
        if (Owner != nullptr)
        {
            Owner->ForgetWindow(this);
        }
    }

    DockPanel* Panel() const
    {
        for (Element* child : GetChildren())
        {
            if (auto* panel = dynamic_cast<DockPanel*>(child))
            {
                return panel;
            }
        }
        return nullptr;
    }

    void OnUpdate(float) override
    {
        // The title follows the panel's, so renaming a floating document
        // renames its window.
        if (DockPanel* panel = Panel())
        {
            Title = panel->Title;
            Closable = panel->Closable;
        }
    }

    void OnChildRemoved(Element& child) override
    {
        // The program removed the panel itself: the window has nothing left
        // to show and goes too.
        if (dynamic_cast<DockPanel*>(&child) != nullptr)
        {
            if (Owner != nullptr)
            {
                Owner->ForgetWindow(this);
                Owner = nullptr;
            }
            if (Element* parent = GetParent())
            {
                parent->Remove(this);
            }
        }
    }

protected:
    void OnMoved(Vec2 pointer) override
    {
        if (Owner == nullptr)
        {
            return;
        }
        if (Owner->m_DragWindow != this)
        {
            Owner->BeginWindowDrag(this, pointer);
        }
        Owner->UpdateWindowDrag(pointer);
    }

    void OnMoveEnded(Vec2 pointer) override
    {
        if (Owner != nullptr && Owner->m_DragWindow == this)
        {
            Owner->EndWindowDrag(pointer);
        }
    }
};

// ---------------------------------------------------------------------------
// The drop guides and the ghost of a dragged tab
// ---------------------------------------------------------------------------

class DockSpace::Guides : public Element
{
public:
    DockSpace* Owner = nullptr;

    explicit Guides(DockSpace* owner) : Owner(owner)
    {
        Interactive = false;
        Visible = false;
        Size = Size2::Fill();
    }

    ~Guides() override
    {
        if (Owner != nullptr && Owner->m_Guides == this)
        {
            Owner->m_Guides = nullptr;
        }
    }

    void Paint(DrawList& drawList) override
    {
        if (Owner == nullptr)
        {
            return;
        }
        DockSpace& space = *Owner;
        const Theme& theme = GetTheme();
        const Color accent = theme.Accent;

        // Where the panel would land.
        if (space.m_Target.Valid)
        {
            drawList.FillRoundedRect(kit::Inset(space.m_Target.Preview, 3.0f), 6.0f, kit::WithAlpha(accent, 0.22f));
            drawList.StrokeRect(kit::Inset(space.m_Target.Preview, 3.0f), 2.0f, kit::WithAlpha(accent, 0.8f), 6.0f);
        }

        auto DrawGuide = [&](const Rect& rect, DockSide side, bool lit) {
            drawList.FillRoundedRect(rect, 6.0f, lit ? accent : kit::WithAlpha(theme.Surface, 0.95f));
            drawList.StrokeRect(rect, 1.0f, lit ? accent : theme.Border, 6.0f);
            const Color mark = lit ? Color{ 1.0f, 1.0f, 1.0f, 1.0f } : kit::WithAlpha(accent, 0.9f);
            const Rect inner = kit::Inset(rect, 7.0f);
            switch (side)
            {
                case DockSide::Center: drawList.StrokeRect(inner, 1.5f, mark, 2.0f); break;
                case DockSide::Left:   drawList.FillRect(Rect{ inner.X, inner.Y, inner.Width * 0.4f, inner.Height }, mark); break;
                case DockSide::Right:  drawList.FillRect(Rect{ inner.X + inner.Width * 0.6f, inner.Y, inner.Width * 0.4f, inner.Height }, mark); break;
                case DockSide::Top:    drawList.FillRect(Rect{ inner.X, inner.Y, inner.Width, inner.Height * 0.4f }, mark); break;
                case DockSide::Bottom: drawList.FillRect(Rect{ inner.X, inner.Y + inner.Height * 0.6f, inner.Width, inner.Height * 0.4f }, mark); break;
            }
        };

        const Rect area = space.GetContentBounds();
        if (!space.m_Reordering)
        {
            // The compass over the group under the pointer.
            if (Node* group = space.GroupAt(space.m_Pointer))
            {
                for (DockSide side : { DockSide::Center, DockSide::Left, DockSide::Right, DockSide::Top, DockSide::Bottom })
                {
                    const bool lit = space.m_Target.Valid && space.m_Target.Group == group && space.m_Target.Side == side;
                    DrawGuide(GuideRect(group->Content, side), side, lit);
                }
            }
            else if (space.m_Root == nullptr)
            {
                const bool lit = space.m_Target.Valid;
                DrawGuide(GuideRect(area, DockSide::Center), DockSide::Center, lit);
            }

            // And the edges of the whole space.
            if (space.m_Root != nullptr)
            {
                for (DockSide side : { DockSide::Left, DockSide::Right, DockSide::Top, DockSide::Bottom })
                {
                    const bool lit = space.m_Target.Valid && space.m_Target.Group == nullptr && space.m_Target.Side == side;
                    DrawGuide(EdgeGuideRect(area, side), side, lit);
                }
            }
        }

        // The ghost of a dragged tab follows the pointer; a dragged window is
        // its own ghost.
        if (space.m_DragPanel != nullptr && space.m_DragLive && !space.m_Reordering)
        {
            const std::string& title = space.m_DragPanel->Title;
            const float width = std::min(220.0f, MeasureText(title).X + 28.0f);
            // Beside the pointer rather than under it, so the guide being
            // pointed at is never hidden by the thing being dropped on it.
            const Rect ghost{ space.m_Pointer.X + 16.0f, space.m_Pointer.Y + 18.0f, width, 26.0f };
            drawList.FillRoundedRect(ghost, 5.0f, kit::WithAlpha(theme.Surface, 0.92f));
            drawList.StrokeRect(ghost, 1.0f, accent, 5.0f);
            drawList.DrawTextInRect(title, kit::Inset(ghost, 6.0f), theme.Font, theme.Text, TextAlign::Left);
        }
    }
};

// ---------------------------------------------------------------------------
// DockSpace
// ---------------------------------------------------------------------------

DockSpace::DockSpace()
{
    ChildLayout = LayoutMode::Custom;
    Size = Size2::Fill();
    ClipChildren = true;
}

DockSpace::~DockSpace()
{
    // The floating windows and the guides live in the overlay, not in this
    // element, so they are let go of explicitly: told to forget this space
    // first, then queued for removal. Their panels go with them.
    for (FloatWindow* window : m_Floating)
    {
        window->Owner = nullptr;
        if (Element* parent = window->GetParent())
        {
            parent->Remove(window);
        }
    }
    m_Floating.clear();

    if (m_Guides != nullptr)
    {
        m_Guides->Owner = nullptr;
        if (Element* parent = m_Guides->GetParent())
        {
            parent->Remove(m_Guides);
        }
        m_Guides = nullptr;
    }
}

void DockSpace::Changed()
{
    if (OnLayoutChanged)
    {
        kit::Invoke(OnLayoutChanged);
    }
}

void DockSpace::CollectPanels(std::vector<DockPanel*>& out) const
{
    out.clear();
    for (Element* child : GetChildren())
    {
        if (auto* panel = dynamic_cast<DockPanel*>(child))
        {
            out.push_back(panel);
        }
    }
    for (const FloatWindow* window : m_Floating)
    {
        if (DockPanel* panel = window->Panel())
        {
            out.push_back(panel);
        }
    }
}

std::vector<DockPanel*> DockSpace::GetPanels() const
{
    std::vector<DockPanel*> panels;
    CollectPanels(panels);
    return panels;
}

DockPanel* DockSpace::FindPanel(const std::string& id) const
{
    std::vector<DockPanel*> panels;
    CollectPanels(panels);
    for (DockPanel* panel : panels)
    {
        if (panel->GetId() == id)
        {
            return panel;
        }
    }
    return nullptr;
}

DockSpace::Node* DockSpace::FindGroup(const DockPanel* panel) const
{
    Node* found = nullptr;
    ForEachGroup(m_Root.get(), [&](Node* group) {
        if (found == nullptr && std::find(group->Panels.begin(), group->Panels.end(), panel) != group->Panels.end())
        {
            found = group;
        }
    });
    return found;
}

DockSpace::FloatWindow* DockSpace::FindWindow(const DockPanel* panel) const
{
    for (FloatWindow* window : m_Floating)
    {
        if (window->Panel() == panel)
        {
            return window;
        }
    }
    return nullptr;
}

DockSpace::Node* DockSpace::GroupAt(Vec2 point) const
{
    Node* found = nullptr;
    ForEachGroup(m_Root.get(), [&](Node* group) {
        if (found == nullptr && kit::Contains(group->Bounds, point))
        {
            found = group;
        }
    });
    return found;
}

DockSpace::Node* DockSpace::LargestGroup() const
{
    Node* largest = nullptr;
    float area = -1.0f;
    ForEachGroup(m_Root.get(), [&](Node* group) {
        const float size = group->Bounds.Width * group->Bounds.Height;
        if (size > area)
        {
            area = size;
            largest = group;
        }
    });
    return largest;
}

bool DockSpace::IsDocked(const DockPanel* panel) const
{
    return FindGroup(panel) != nullptr;
}

bool DockSpace::IsFloating(const DockPanel* panel) const
{
    return FindWindow(panel) != nullptr;
}

bool DockSpace::IsOpen(const DockPanel* panel) const
{
    return IsDocked(panel) || IsFloating(panel);
}

void DockSpace::Prune()
{
    // Groups left empty go, and a split left with one child is replaced by
    // that child, until the tree has neither. One group at a time, because
    // removing one reshapes the tree the others were found in.
    for (;;)
    {
        Node* group = nullptr;
        ForEachGroup(m_Root.get(), [&](Node* candidate) {
            if (group == nullptr && candidate->Panels.empty())
            {
                group = candidate;
            }
        });
        if (group == nullptr)
        {
            return;
        }

        Node* parent = group->Parent;
        if (parent == nullptr)
        {
            if (m_HoverGroup == group)
            {
                m_HoverGroup = nullptr;
            }
            if (m_DragGroup == group)
            {
                m_DragGroup = nullptr;
            }
            m_Root.reset();
            return;
        }

        // The sibling takes the parent's place.
        std::unique_ptr<Node> survivor =
            parent->First.get() == group ? std::move(parent->Second) : std::move(parent->First);
        Node* grandparent = parent->Parent;
        survivor->Parent = grandparent;

        if (m_HoverGroup == group || m_HoverGroup == parent)
        {
            m_HoverGroup = nullptr;
        }
        if (m_DragGroup == group)
        {
            m_DragGroup = nullptr;
        }
        if (m_ResizeNode == parent)
        {
            m_ResizeNode = nullptr;
        }

        if (grandparent == nullptr)
        {
            m_Root = std::move(survivor);
        }
        else if (grandparent->First.get() == parent)
        {
            grandparent->First = std::move(survivor);
        }
        else
        {
            grandparent->Second = std::move(survivor);
        }
    }
}

void DockSpace::Detach(DockPanel* panel)
{
    Node* group = FindGroup(panel);
    if (group == nullptr)
    {
        return;
    }

    const auto at = std::find(group->Panels.begin(), group->Panels.end(), panel);
    const int index = static_cast<int>(at - group->Panels.begin());
    group->Panels.erase(at);

    // The neighbour to the right takes the place of a closed active tab.
    if (index < group->Active || group->Active >= static_cast<int>(group->Panels.size()))
    {
        group->Active = std::max(0, group->Active - 1);
    }
    group->Active = std::clamp(group->Active, 0, std::max(0, static_cast<int>(group->Panels.size()) - 1));

    Prune();
}

void DockSpace::Unfloat(DockPanel* panel)
{
    FloatWindow* window = FindWindow(panel);
    if (window == nullptr)
    {
        return;
    }
    if (m_DragWindow == window)
    {
        m_DragWindow = nullptr;
    }

    // The panel comes home before its window goes, so it is never destroyed
    // with it.
    panel->MoveTo(this);
    m_Floating.erase(std::remove(m_Floating.begin(), m_Floating.end(), window), m_Floating.end());
    window->Owner = nullptr;
    if (Element* parent = window->GetParent())
    {
        parent->Remove(window);
    }
}

void DockSpace::Insert(DockPanel* panel, DockSide side, Node* group, float share)
{
    share = std::clamp(share, 0.05f, 0.95f);

    auto NewGroup = [&]() {
        auto node = std::make_unique<Node>();
        node->Type = Node::Kind::Tabs;
        node->Panels.push_back(panel);
        node->Active = 0;
        return node;
    };

    if (m_Root == nullptr)
    {
        m_Root = NewGroup();
        return;
    }

    if (side == DockSide::Center)
    {
        Node* target = group != nullptr ? group : LargestGroup();
        if (target == nullptr)
        {
            ForEachGroup(m_Root.get(), [&](Node* candidate) {
                if (target == nullptr)
                {
                    target = candidate;
                }
            });
        }
        target->Panels.push_back(panel);
        target->Active = static_cast<int>(target->Panels.size()) - 1;
        return;
    }

    // Beside a node: the node and a new group become the two halves of a new
    // split, which takes the node's place.
    Node* beside = group != nullptr ? group : m_Root.get();
    Node* parent = beside->Parent;

    std::unique_ptr<Node> held;
    if (parent == nullptr)
    {
        held = std::move(m_Root);
    }
    else if (parent->First.get() == beside)
    {
        held = std::move(parent->First);
    }
    else
    {
        held = std::move(parent->Second);
    }

    auto split = std::make_unique<Node>();
    split->Type = Node::Kind::Split;
    split->Stacked = side == DockSide::Top || side == DockSide::Bottom;
    split->Parent = parent;

    std::unique_ptr<Node> fresh = NewGroup();
    const bool freshFirst = side == DockSide::Left || side == DockSide::Top;
    split->Ratio = freshFirst ? share : 1.0f - share;

    fresh->Parent = split.get();
    held->Parent = split.get();
    if (freshFirst)
    {
        split->First = std::move(fresh);
        split->Second = std::move(held);
    }
    else
    {
        split->First = std::move(held);
        split->Second = std::move(fresh);
    }

    if (parent == nullptr)
    {
        m_Root = std::move(split);
    }
    else if (parent->First == nullptr)
    {
        parent->First = std::move(split);
    }
    else
    {
        parent->Second = std::move(split);
    }
}

void DockSpace::Dock(DockPanel* panel, DockSide side, DockPanel* relativeTo, float share)
{
    if (panel == nullptr)
    {
        return;
    }

    // A panel from elsewhere in the interface is adopted.
    const bool ours = panel->GetParent() == this || FindWindow(panel) != nullptr;
    if (!ours && !panel->MoveTo(this))
    {
        return;
    }

    if (relativeTo == panel)
    {
        relativeTo = nullptr;
    }

    Unfloat(panel);
    m_Closed.erase(std::remove_if(m_Closed.begin(), m_Closed.end(),
                                  [&](const ClosedPanel& closed) { return closed.Panel == panel; }),
                   m_Closed.end());
    Detach(panel);

    Node* group = relativeTo != nullptr ? FindGroup(relativeTo) : nullptr;
    Insert(panel, side, group, share);
    panel->Visible = true;
    Changed();
}

void DockSpace::Float(DockPanel* panel, const Rect& frame)
{
    if (panel == nullptr)
    {
        return;
    }
    App app = GetApp();
    if (!app.IsValid())
    {
        return;
    }

    if (FloatWindow* window = FindWindow(panel))
    {
        window->SetFrame(frame);
        window->Visible = true;
        window->BringToFront();
        return;
    }

    const bool ours = panel->GetParent() == this;
    if (!ours && !panel->MoveTo(this))
    {
        return;
    }

    m_Closed.erase(std::remove_if(m_Closed.begin(), m_Closed.end(),
                                  [&](const ClosedPanel& closed) { return closed.Panel == panel; }),
                   m_Closed.end());
    Detach(panel);

    auto* window = app.GetOverlay()->Add<FloatWindow>(this);
    window->Name = "opane.DockWindow";
    window->Title = panel->Title;
    window->Closable = panel->Closable;
    window->SetFrame(frame);
    window->OnCloseRequested = [this, window]() {
        // A window whose dock space is gone (it is waiting to be removed)
        // just closes.
        if (window->Owner == nullptr)
        {
            return true;
        }
        // Otherwise closing the window closes the panel: the panel waits in
        // this space to be shown again, and the window itself goes.
        if (DockPanel* inside = window->Panel())
        {
            ClosePanel(inside);
        }
        window->Visible = false;
        return false;
    };

    panel->MoveTo(window);
    panel->Visible = true;
    panel->Position = Position2{};
    panel->AnchorPoint = Vec2{};
    panel->Size = Size2::Fill();
    m_Floating.push_back(window);
    Changed();
}

void DockSpace::ClosePanel(DockPanel* panel)
{
    if (panel == nullptr || !IsOpen(panel))
    {
        return;
    }

    ClosedPanel closed;
    closed.Panel = panel;
    if (Node* group = FindGroup(panel))
    {
        for (DockPanel* other : group->Panels)
        {
            if (other != panel)
            {
                closed.Neighbour = other;
                break;
            }
        }
    }

    Unfloat(panel);
    Detach(panel);
    panel->Visible = false;
    m_Closed.push_back(closed);

    if (m_DragPanel == panel)
    {
        m_DragPanel = nullptr;
        m_DragLive = false;
        ShowGuides(false);
    }

    if (panel->OnClosed)
    {
        kit::Invoke(panel->OnClosed);
    }
    Changed();
}

void DockSpace::Show(DockPanel* panel)
{
    if (panel == nullptr)
    {
        return;
    }
    if (IsOpen(panel))
    {
        Activate(panel);
        return;
    }

    DockPanel* neighbour = nullptr;
    for (const ClosedPanel& closed : m_Closed)
    {
        if (closed.Panel == panel && closed.Neighbour != nullptr && IsDocked(closed.Neighbour))
        {
            neighbour = closed.Neighbour;
        }
    }
    Dock(panel, DockSide::Center, neighbour);
}

void DockSpace::Activate(DockPanel* panel)
{
    if (Node* group = FindGroup(panel))
    {
        const auto at = std::find(group->Panels.begin(), group->Panels.end(), panel);
        group->Active = static_cast<int>(at - group->Panels.begin());
        return;
    }
    if (FloatWindow* window = FindWindow(panel))
    {
        window->Visible = true;
        window->BringToFront();
        return;
    }
    Show(panel);
}

void DockSpace::ForgetWindow(FloatWindow* window)
{
    m_Floating.erase(std::remove(m_Floating.begin(), m_Floating.end(), window), m_Floating.end());
    if (m_DragWindow == window)
    {
        m_DragWindow = nullptr;
        m_Target = Target{};
        ShowGuides(false);
    }
    if (m_DragPanel != nullptr && m_DragPanel->IsInside(window))
    {
        m_DragPanel = nullptr;
        m_DragLive = false;
    }
}

void DockSpace::OnChildRemoved(Element& child)
{
    auto* panel = dynamic_cast<DockPanel*>(&child);
    if (panel == nullptr)
    {
        return;
    }

    // Removed by the program rather than closed: forgotten entirely.
    Detach(panel);
    m_Closed.erase(std::remove_if(m_Closed.begin(), m_Closed.end(),
                                  [&](const ClosedPanel& closed) {
                                      return closed.Panel == panel || closed.Neighbour == panel;
                                  }),
                   m_Closed.end());
    if (m_DragPanel == panel)
    {
        m_DragPanel = nullptr;
        m_DragLive = false;
        ShowGuides(false);
    }
    if (m_PressClose == panel)
    {
        m_PressClose = nullptr;
    }
}

// --- layout ------------------------------------------------------------------

void DockSpace::LayoutNodes(Node* node, const Rect& bounds)
{
    if (node == nullptr)
    {
        return;
    }
    node->Bounds = bounds;

    if (node->Type == Node::Kind::Tabs)
    {
        node->Strip = Rect{ bounds.X, bounds.Y, bounds.Width, std::min(TabHeight, bounds.Height) };
        node->Content = Rect{ bounds.X, bounds.Y + node->Strip.Height, bounds.Width,
                              std::max(0.0f, bounds.Height - node->Strip.Height) };
        node->Active = std::clamp(node->Active, 0, std::max(0, static_cast<int>(node->Panels.size()) - 1));
        return;
    }

    const float extent = node->Stacked ? bounds.Height : bounds.Width;
    const float total = std::max(0.0f, extent - DividerThickness);
    float first = std::clamp(node->Ratio, 0.0f, 1.0f) * total;
    if (total >= MinimumPane * 2.0f)
    {
        first = std::clamp(first, MinimumPane, total - MinimumPane);
    }
    first = std::round(first);

    if (node->Stacked)
    {
        node->Divider = Rect{ bounds.X, bounds.Y + first, bounds.Width, DividerThickness };
        LayoutNodes(node->First.get(), Rect{ bounds.X, bounds.Y, bounds.Width, first });
        LayoutNodes(node->Second.get(), Rect{ bounds.X, bounds.Y + first + DividerThickness, bounds.Width,
                                              std::max(0.0f, bounds.Height - first - DividerThickness) });
    }
    else
    {
        node->Divider = Rect{ bounds.X + first, bounds.Y, DividerThickness, bounds.Height };
        LayoutNodes(node->First.get(), Rect{ bounds.X, bounds.Y, first, bounds.Height });
        LayoutNodes(node->Second.get(), Rect{ bounds.X + first + DividerThickness, bounds.Y,
                                              std::max(0.0f, bounds.Width - first - DividerThickness), bounds.Height });
    }
}

void DockSpace::Arrange(const Rect& bounds)
{
    Element::Arrange(bounds);
    LayoutNodes(m_Root.get(), GetContentBounds());

    // Only the tab showing in each group is laid out and drawn; the rest, and
    // every closed panel, wait hidden. Set here, before the children are
    // placed, so the frame that changed a tab lays out the right page.
    for (Element* child : GetChildren())
    {
        if (auto* panel = dynamic_cast<DockPanel*>(child))
        {
            const Node* group = FindGroup(panel);
            panel->Visible = group != nullptr && group->ActivePanel() == panel;
        }
    }
}

Rect DockSpace::PlaceChild(Element& child, size_t, const Rect& content)
{
    if (auto* panel = dynamic_cast<DockPanel*>(&child))
    {
        if (const Node* group = FindGroup(panel))
        {
            return group->Content;
        }
    }
    return Rect{ content.X, content.Y, 0.0f, 0.0f };
}

Rect DockSpace::TabRect(const Node* group, int index) const
{
    if (group == nullptr || index < 0 || index >= static_cast<int>(group->Panels.size()))
    {
        return Rect{};
    }

    auto Wanted = [&](const DockPanel* panel) {
        const float close = panel->Closable ? TabCloseSize + 6.0f : 0.0f;
        return std::clamp(MeasureText(panel->Title).X + TabPadding * 2.0f + close, TabMinimum, TabMaximum);
    };

    float total = 0.0f;
    for (const DockPanel* panel : group->Panels)
    {
        total += Wanted(panel);
    }
    const float available = std::max(1.0f, group->Strip.Width - 4.0f);
    const float squeeze = total > available ? available / total : 1.0f;

    float x = group->Strip.X + 2.0f;
    for (int tab = 0; tab < static_cast<int>(group->Panels.size()); ++tab)
    {
        const float width = Wanted(group->Panels[static_cast<size_t>(tab)]) * squeeze;
        if (tab == index)
        {
            return Rect{ x, group->Strip.Y + 3.0f, width, group->Strip.Height - 3.0f };
        }
        x += width;
    }
    return Rect{};
}

Rect DockSpace::CloseRect(const Node* group, int index) const
{
    const Rect tab = TabRect(group, index);
    return Rect{ tab.X + tab.Width - TabCloseSize - 6.0f, tab.Y + (tab.Height - TabCloseSize) * 0.5f, TabCloseSize,
                 TabCloseSize };
}

int DockSpace::TabAt(const Node* group, Vec2 point) const
{
    if (group == nullptr || !kit::Contains(group->Strip, point))
    {
        return -1;
    }
    for (int tab = 0; tab < static_cast<int>(group->Panels.size()); ++tab)
    {
        if (kit::Contains(TabRect(group, tab), point))
        {
            return tab;
        }
    }
    return -1;
}

DockSpace::Node* DockSpace::DividerAt(Node* node, Vec2 point) const
{
    if (node == nullptr || node->Type != Node::Kind::Split)
    {
        return nullptr;
    }
    const Rect grab = kit::Inset(node->Divider, -2.0f);
    if (kit::Contains(grab, point))
    {
        return node;
    }
    if (Node* found = DividerAt(node->First.get(), point))
    {
        return found;
    }
    return DividerAt(node->Second.get(), point);
}

// --- painting ----------------------------------------------------------------

void DockSpace::PaintNode(Node* node, DrawList& drawList)
{
    if (node == nullptr)
    {
        return;
    }
    const Theme& theme = GetTheme();

    if (node->Type == Node::Kind::Split)
    {
        PaintNode(node->First.get(), drawList);
        PaintNode(node->Second.get(), drawList);

        const bool active = node == m_ResizeNode;
        const Rect divider = node->Divider;
        if (node->Stacked)
        {
            drawList.FillRect(Rect{ divider.X, divider.Y + divider.Height * 0.5f - 0.5f, divider.Width, active ? 2.0f : 1.0f },
                              active ? theme.Accent : theme.Border);
        }
        else
        {
            drawList.FillRect(Rect{ divider.X + divider.Width * 0.5f - 0.5f, divider.Y, active ? 2.0f : 1.0f, divider.Height },
                              active ? theme.Accent : theme.Border);
        }
        return;
    }

    // The group holding the keyboard is marked, so it is clear where typing
    // goes.
    bool focusedGroup = false;
    if (DockPanel* shown = node->ActivePanel())
    {
        App app = GetApp();
        const Element* focused = app.IsValid() ? app.GetFocusedElement() : nullptr;
        focusedGroup = focused != nullptr && focused->IsInside(shown);
    }

    drawList.FillRect(node->Strip, theme.Surface);
    drawList.FillRect(Rect{ node->Strip.X, node->Strip.Y + node->Strip.Height - 1.0f, node->Strip.Width, 1.0f },
                      theme.Border);

    drawList.PushClip(node->Strip);
    for (int tab = 0; tab < static_cast<int>(node->Panels.size()); ++tab)
    {
        const DockPanel* panel = node->Panels[static_cast<size_t>(tab)];
        const Rect rect = TabRect(node, tab);
        const bool active = tab == node->Active;
        const bool hovered = node == m_HoverGroup && tab == m_HoverTab;
        const bool dragged = panel == m_DragPanel && m_DragLive;

        if (active)
        {
            drawList.FillRect(rect, theme.Background);
            drawList.FillRect(Rect{ rect.X, rect.Y, rect.Width, 2.0f },
                              focusedGroup ? theme.Accent : kit::WithAlpha(theme.TextMuted, 0.6f));
        }
        else if (hovered)
        {
            drawList.FillRect(rect, theme.SurfaceHovered);
        }
        if (dragged && !m_Reordering)
        {
            drawList.FillRect(rect, kit::WithAlpha(theme.Accent, 0.15f));
        }

        const float close = panel->Closable ? TabCloseSize + 6.0f : 0.0f;
        const Rect textRect{ rect.X + TabPadding, rect.Y, std::max(0.0f, rect.Width - TabPadding * 2.0f - close), rect.Height };
        const std::string title =
            kit::Ellipsize(panel->Title, textRect.Width, [this](const std::string& text) { return MeasureText(text).X; });
        drawList.DrawTextInRect(title, textRect, theme.Font, active ? theme.Text : theme.TextMuted, TextAlign::Left);

        if (panel->Closable && (active || hovered))
        {
            const Rect closeRect = CloseRect(node, tab);
            if (hovered && m_HoverClose)
            {
                drawList.FillRoundedRect(closeRect, 4.0f, theme.SurfacePressed);
            }
            kit::DrawCross(drawList, kit::Center(closeRect), 7.0f, theme.TextMuted, 1.4f);
        }
    }
    drawList.PopClip();
}

void DockSpace::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    drawList.FillRect(bounds, theme.Surface);

    if (m_Root == nullptr)
    {
        drawList.DrawTextInRect("Drag a panel here", GetContentBounds(), theme.Font, theme.TextMuted,
                                TextAlign::Center);
        return;
    }
    PaintNode(m_Root.get(), drawList);
}

// --- dropping ----------------------------------------------------------------

DockSpace::Target DockSpace::TargetAt(Vec2 point, const DockPanel* moving) const
{
    Target target;
    const Rect area = GetContentBounds();

    if (m_Root == nullptr)
    {
        if (kit::Contains(area, point))
        {
            target.Valid = true;
            target.Side = DockSide::Center;
            target.Preview = area;
        }
        return target;
    }

    // The edges of the whole space first: they sit over groups and must win.
    for (DockSide side : { DockSide::Left, DockSide::Right, DockSide::Top, DockSide::Bottom })
    {
        if (kit::Contains(EdgeGuideRect(area, side), point))
        {
            target.Valid = true;
            target.Side = side;
            target.Group = nullptr;
            target.Preview = SideOf(area, side, 0.28f);
            return target;
        }
    }

    Node* group = GroupAt(point);
    if (group == nullptr)
    {
        return target;
    }

    const Node* home = moving != nullptr ? FindGroup(moving) : nullptr;
    const bool alone = home == group && group->Panels.size() == 1;

    for (DockSide side : { DockSide::Center, DockSide::Left, DockSide::Right, DockSide::Top, DockSide::Bottom })
    {
        if (kit::Contains(GuideRect(group->Content, side), point))
        {
            // A panel alone in its group has nowhere to go relative to itself.
            if (alone || (side == DockSide::Center && home == group))
            {
                return target;
            }
            target.Valid = true;
            target.Side = side;
            target.Group = group;
            target.Preview = side == DockSide::Center ? group->Bounds : SideOf(group->Bounds, side, 0.5f);
            return target;
        }
    }

    // Anywhere on another group's tab strip tabs the panel in.
    if (kit::Contains(group->Strip, point) && home != group)
    {
        target.Valid = true;
        target.Side = DockSide::Center;
        target.Group = group;
        target.Preview = group->Bounds;
    }
    return target;
}

void DockSpace::EnsureGuides()
{
    App app = GetApp();
    if (!app.IsValid())
    {
        return;
    }
    if (m_Guides != nullptr && !app.ContainsElement(m_Guides))
    {
        m_Guides = nullptr;
    }
    if (m_Guides == nullptr)
    {
        m_Guides = app.GetOverlay()->Add<Guides>(this);
        m_Guides->Name = "opane.DockGuides";
    }
}

void DockSpace::ShowGuides(bool shown)
{
    if (shown)
    {
        EnsureGuides();
        if (m_Guides != nullptr)
        {
            m_Guides->Visible = true;
            m_Guides->BringToFront();
        }
    }
    else if (m_Guides != nullptr)
    {
        m_Guides->Visible = false;
    }
}

void DockSpace::BeginWindowDrag(FloatWindow* window, Vec2 pointer)
{
    m_DragWindow = window;
    m_Pointer = pointer;
    ShowGuides(true);
}

void DockSpace::UpdateWindowDrag(Vec2 pointer)
{
    m_Pointer = pointer;
    DockPanel* panel = m_DragWindow != nullptr ? m_DragWindow->Panel() : nullptr;
    m_Target = panel != nullptr ? TargetAt(pointer, panel) : Target{};
}

void DockSpace::EndWindowDrag(Vec2 pointer)
{
    FloatWindow* window = m_DragWindow;
    m_DragWindow = nullptr;
    ShowGuides(false);

    const Target target = window != nullptr ? TargetAt(pointer, window->Panel()) : Target{};
    m_Target = Target{};
    if (window == nullptr || !target.Valid)
    {
        return;
    }

    DockPanel* panel = window->Panel();
    if (panel == nullptr)
    {
        return;
    }
    DockPanel* relative = target.Group != nullptr ? target.Group->ActivePanel() : nullptr;
    Dock(panel, target.Side, relative, 0.5f);
    if (target.Group == nullptr && target.Side != DockSide::Center)
    {
        // Docked to an edge of the whole space: a sensible side panel rather
        // than half of everything.
        if (Node* group = FindGroup(panel); group != nullptr && group->Parent != nullptr)
        {
            const bool first = group->Parent->First.get() == group;
            group->Parent->Ratio = first ? 0.28f : 0.72f;
        }
    }
}

// --- input -------------------------------------------------------------------

void DockSpace::OnUpdate(float)
{
    // Guides left showing by a drag that ended without a release reaching
    // here (when the window lost focus mid-drag, for example) are hidden.
    if (m_DragPanel == nullptr && m_DragWindow == nullptr && m_Guides != nullptr && m_Guides->Visible)
    {
        ShowGuides(false);
    }
}

void DockSpace::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerMove:
        {
            m_Pointer = event.Position;

            if (m_ResizeNode != nullptr)
            {
                Node* node = m_ResizeNode;
                const Rect bounds = node->Bounds;
                const float extent = node->Stacked ? bounds.Height : bounds.Width;
                const float total = std::max(1.0f, extent - DividerThickness);
                const float offset = (node->Stacked ? event.Position.Y - bounds.Y : event.Position.X - bounds.X) -
                                     DividerThickness * 0.5f;
                float ratio = std::clamp(offset / total, 0.0f, 1.0f);
                if (total >= MinimumPane * 2.0f)
                {
                    ratio = std::clamp(ratio, MinimumPane / total, 1.0f - MinimumPane / total);
                }
                node->Ratio = ratio;
                event.Handled = true;
                break;
            }

            if (m_DragPanel != nullptr)
            {
                const float dx = event.Position.X - m_DragStart.X;
                const float dy = event.Position.Y - m_DragStart.Y;
                if (!m_DragLive && dx * dx + dy * dy >= TearDistance * TearDistance)
                {
                    m_DragLive = true;
                }
                if (!m_DragLive)
                {
                    event.Handled = true;
                    break;
                }

                Node* home = FindGroup(m_DragPanel);
                const bool inStrip =
                    home != nullptr && event.Position.X >= home->Strip.X &&
                    event.Position.X <= home->Strip.X + home->Strip.Width &&
                    event.Position.Y >= home->Strip.Y - StripGrace &&
                    event.Position.Y <= home->Strip.Y + home->Strip.Height + StripGrace;

                m_Reordering = inStrip;
                if (inStrip)
                {
                    // Reordering: the tab swaps with a neighbour once the
                    // pointer passes that neighbour's middle.
                    auto& panels = home->Panels;
                    int from = static_cast<int>(std::find(panels.begin(), panels.end(), m_DragPanel) - panels.begin());
                    for (int tab = 0; tab < static_cast<int>(panels.size()); ++tab)
                    {
                        const Rect rect = TabRect(home, tab);
                        const float middle = rect.X + rect.Width * 0.5f;
                        if ((tab < from && event.Position.X < middle) || (tab > from && event.Position.X > middle))
                        {
                            std::swap(panels[static_cast<size_t>(from)], panels[static_cast<size_t>(tab)]);
                            from = tab;
                            home->Active = tab;
                            break;
                        }
                    }
                    m_Target = Target{};
                    ShowGuides(false);
                }
                else
                {
                    m_Target = TargetAt(event.Position, m_DragPanel);
                    ShowGuides(true);
                }
                event.Handled = true;
                break;
            }

            // Hover.
            m_HoverGroup = GroupAt(event.Position);
            m_HoverTab = TabAt(m_HoverGroup, event.Position);
            m_HoverClose = false;
            if (m_HoverTab >= 0)
            {
                const DockPanel* panel = m_HoverGroup->Panels[static_cast<size_t>(m_HoverTab)];
                m_HoverClose = panel->Closable && kit::Contains(CloseRect(m_HoverGroup, m_HoverTab), event.Position);
            }
            if (Node* divider = DividerAt(m_Root.get(), event.Position))
            {
                Cursor = divider->Stacked ? CursorShape::ResizeVertical : CursorShape::ResizeHorizontal;
            }
            else
            {
                Cursor = CursorShape::Default;
            }
            break;
        }

        case EventType::PointerLeave:
            m_HoverGroup = nullptr;
            m_HoverTab = -1;
            m_HoverClose = false;
            break;

        case EventType::PointerDown:
        {
            m_Pointer = event.Position;
            event.Handled = true;

            if (Node* divider = DividerAt(m_Root.get(), event.Position); divider != nullptr &&
                                                                       event.Button == MouseButton::Left)
            {
                m_ResizeNode = divider;
                break;
            }

            Node* group = GroupAt(event.Position);
            const int tab = TabAt(group, event.Position);
            if (tab < 0)
            {
                break;
            }
            DockPanel* panel = group->Panels[static_cast<size_t>(tab)];

            if (event.Button == MouseButton::Middle)
            {
                m_PressClose = panel->Closable ? panel : nullptr;
                break;
            }
            if (event.Button != MouseButton::Left)
            {
                break;
            }

            if (panel->Closable && kit::Contains(CloseRect(group, tab), event.Position))
            {
                m_PressClose = panel;
                break;
            }

            group->Active = tab;
            m_DragPanel = panel;
            m_DragGroup = group;
            m_DragStart = event.Position;
            m_DragLive = false;
            m_Reordering = false;
            break;
        }

        case EventType::PointerUp:
        {
            m_Pointer = event.Position;
            event.Handled = true;

            if (m_ResizeNode != nullptr)
            {
                m_ResizeNode = nullptr;
                Changed();
                break;
            }

            if (m_PressClose != nullptr)
            {
                DockPanel* pressed = m_PressClose;
                m_PressClose = nullptr;
                Node* group = GroupAt(event.Position);
                const int tab = TabAt(group, event.Position);
                const bool onTab = tab >= 0 && group->Panels[static_cast<size_t>(tab)] == pressed;
                const bool onClose = onTab && kit::Contains(CloseRect(group, tab), event.Position);
                if ((event.Button == MouseButton::Middle && onTab) || (event.Button == MouseButton::Left && onClose))
                {
                    ClosePanel(pressed);
                }
                break;
            }

            if (m_DragPanel == nullptr)
            {
                break;
            }

            DockPanel* panel = m_DragPanel;
            const bool live = m_DragLive;
            const bool reordering = m_Reordering;
            const Target target = live && !reordering ? TargetAt(event.Position, panel) : Target{};

            m_DragPanel = nullptr;
            m_DragGroup = nullptr;
            m_DragLive = false;
            m_Reordering = false;
            m_Target = Target{};
            ShowGuides(false);

            if (!live)
            {
                break;
            }
            if (reordering)
            {
                Changed();
                break;
            }

            if (target.Valid)
            {
                DockPanel* relative = target.Group != nullptr ? target.Group->ActivePanel() : nullptr;
                if (relative == panel)
                {
                    // Splitting its own group: the panel leaves, and the side
                    // is taken relative to whatever remains there.
                    Node* group = target.Group;
                    for (DockPanel* other : group->Panels)
                    {
                        if (other != panel)
                        {
                            relative = other;
                            break;
                        }
                    }
                }
                const float share = target.Group == nullptr ? 0.28f : 0.5f;
                Dock(panel, target.Side, relative, share);
            }
            else if (AllowFloating)
            {
                const Node* group = FindGroup(panel);
                const float width = group != nullptr ? std::max(320.0f, group->Bounds.Width * 0.6f) : 360.0f;
                const float height = group != nullptr ? std::max(220.0f, group->Bounds.Height * 0.6f) : 260.0f;
                Float(panel, Rect{ event.Position.X - 60.0f, event.Position.Y - 14.0f, width, height });
            }
            break;
        }

        default:
            break;
    }
}

// --- saving and restoring ----------------------------------------------------

namespace
{

constexpr const char* LayoutMagic = "opane-dock";
constexpr int LayoutVersion = 1;

void WriteNode(std::ostringstream& out, const Node* node)
{
    if (node->Type == Node::Kind::Split)
    {
        char ratio[32];
        std::snprintf(ratio, sizeof(ratio), "%.4f", static_cast<double>(node->Ratio));
        out << "split " << (node->Stacked ? "v " : "h ") << ratio << ' ';
        WriteNode(out, node->First.get());
        out << ' ';
        WriteNode(out, node->Second.get());
        return;
    }
    out << "tabs " << node->Active << ' ' << node->Panels.size();
    for (const DockPanel* panel : node->Panels)
    {
        out << ' ' << Escape(panel->GetId());
    }
}

// Reads a node, resolving ids through the lookup. Returns null on malformed
// text; groups whose panels are all unknown come back empty, to be pruned.
std::unique_ptr<Node> ReadNode(std::istringstream& in, const std::function<DockPanel*(const std::string&)>& lookup,
                               std::vector<DockPanel*>& used, int depth)
{
    if (depth > 64)
    {
        return nullptr;
    }
    std::string kind;
    if (!(in >> kind))
    {
        return nullptr;
    }

    auto node = std::make_unique<Node>();
    if (kind == "split")
    {
        std::string direction;
        float ratio = 0.5f;
        if (!(in >> direction >> ratio) || (direction != "h" && direction != "v") || !std::isfinite(ratio))
        {
            return nullptr;
        }
        node->Type = Node::Kind::Split;
        node->Stacked = direction == "v";
        node->Ratio = std::clamp(ratio, 0.0f, 1.0f);
        node->First = ReadNode(in, lookup, used, depth + 1);
        node->Second = ReadNode(in, lookup, used, depth + 1);
        if (node->First == nullptr || node->Second == nullptr)
        {
            return nullptr;
        }
        node->First->Parent = node.get();
        node->Second->Parent = node.get();
        return node;
    }

    if (kind == "tabs")
    {
        int active = 0;
        int count = 0;
        if (!(in >> active >> count) || count < 0 || count > 4096)
        {
            return nullptr;
        }
        node->Type = Node::Kind::Tabs;
        int kept = 0;
        int activeKept = 0;
        for (int index = 0; index < count; ++index)
        {
            std::string id;
            if (!(in >> id))
            {
                return nullptr;
            }
            DockPanel* panel = lookup(Unescape(id));
            if (panel == nullptr || std::find(used.begin(), used.end(), panel) != used.end())
            {
                continue;
            }
            if (index == active)
            {
                activeKept = kept;
            }
            used.push_back(panel);
            node->Panels.push_back(panel);
            ++kept;
        }
        node->Active = activeKept;
        return node;
    }
    return nullptr;
}

} // namespace

std::string DockSpace::SaveLayout() const
{
    std::ostringstream out;
    out << LayoutMagic << ' ' << LayoutVersion << '\n';
    if (m_Root != nullptr)
    {
        out << "root ";
        WriteNode(out, m_Root.get());
        out << '\n';
    }
    for (const FloatWindow* window : m_Floating)
    {
        const DockPanel* panel = window->Panel();
        if (panel == nullptr)
        {
            continue;
        }
        const Rect frame = window->GetFrame();
        out << "float " << Escape(panel->GetId()) << ' ' << static_cast<int>(frame.X) << ' '
            << static_cast<int>(frame.Y) << ' ' << static_cast<int>(frame.Width) << ' '
            << static_cast<int>(frame.Height) << '\n';
    }
    for (const ClosedPanel& closed : m_Closed)
    {
        out << "closed " << Escape(closed.Panel->GetId()) << '\n';
    }
    return out.str();
}

bool DockSpace::LoadLayout(const std::string& layout)
{
    std::istringstream in(layout);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != LayoutMagic || version != LayoutVersion)
    {
        LogMessage(LogLevel::Warning, "ui", "DockSpace::LoadLayout: not a layout this build can read.");
        return false;
    }

    std::vector<DockPanel*> panels;
    CollectPanels(panels);
    auto Lookup = [&](const std::string& id) -> DockPanel* {
        for (DockPanel* panel : panels)
        {
            if (panel->GetId() == id)
            {
                return panel;
            }
        }
        return nullptr;
    };

    // Everything is parsed before anything changes, so a layout that turns
    // out to be malformed half way leaves the arrangement as it was.
    std::unique_ptr<Node> root;
    struct Floating
    {
        DockPanel* Panel;
        Rect Frame;
    };
    std::vector<Floating> floating;
    std::vector<DockPanel*> closed;
    std::vector<DockPanel*> used;

    std::string keyword;
    while (in >> keyword)
    {
        if (keyword == "root")
        {
            if (root != nullptr)
            {
                return false;
            }
            root = ReadNode(in, Lookup, used, 0);
            if (root == nullptr)
            {
                LogMessage(LogLevel::Warning, "ui", "DockSpace::LoadLayout: the arrangement is malformed.");
                return false;
            }
        }
        else if (keyword == "float")
        {
            std::string id;
            float x = 0, y = 0, width = 0, height = 0;
            if (!(in >> id >> x >> y >> width >> height))
            {
                return false;
            }
            DockPanel* panel = Lookup(Unescape(id));
            if (panel != nullptr && std::find(used.begin(), used.end(), panel) == used.end())
            {
                used.push_back(panel);
                floating.push_back(Floating{ panel, Rect{ x, y, std::max(width, 120.0f), std::max(height, 80.0f) } });
            }
        }
        else if (keyword == "closed")
        {
            std::string id;
            if (!(in >> id))
            {
                return false;
            }
            DockPanel* panel = Lookup(Unescape(id));
            if (panel != nullptr && std::find(used.begin(), used.end(), panel) == used.end())
            {
                used.push_back(panel);
                closed.push_back(panel);
            }
        }
        else
        {
            LogMessage(LogLevel::Warning, "ui", "DockSpace::LoadLayout: unknown entry \"%s\".", keyword.c_str());
            return false;
        }
    }

    // Apply. Every panel comes home first, so each starts from the same place.
    m_DragPanel = nullptr;
    m_DragWindow = nullptr;
    m_ResizeNode = nullptr;
    m_HoverGroup = nullptr;
    m_Target = Target{};
    ShowGuides(false);

    for (DockPanel* panel : panels)
    {
        Unfloat(panel);
    }
    m_Root.reset();
    m_Closed.clear();

    m_Root = std::move(root);
    Prune();

    for (const Floating& entry : floating)
    {
        Float(entry.Panel, entry.Frame);
    }
    for (DockPanel* panel : closed)
    {
        panel->Visible = false;
        m_Closed.push_back(ClosedPanel{ panel, nullptr });
    }

    // Panels the layout did not mention go somewhere visible rather than
    // vanishing.
    for (DockPanel* panel : panels)
    {
        if (std::find(used.begin(), used.end(), panel) == used.end())
        {
            Insert(panel, DockSide::Center, nullptr, 0.3f);
        }
    }

    Changed();
    return true;
}

} // namespace opane
