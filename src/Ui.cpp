#include "Ui.h"

#include "RenderTargets.h"
#include "Text.h"

#include <algorithm>
#include <cmath>

namespace opane
{
namespace
{

bool Contains(const Rect& rect, Vec2 point)
{
    return point.X >= rect.X && point.X <= rect.X + rect.Width && point.Y >= rect.Y &&
           point.Y <= rect.Y + rect.Height;
}

// Shown after the pointer has rested this long on something with a tooltip.
constexpr float TooltipDelay = 0.55f;

// Moving further than this restarts the wait, so a tooltip appears where the
// pointer settled rather than where it passed through.
constexpr float TooltipSlop = 4.0f;

Vec2 ApplyMinimum(const Element& element, Vec2 size)
{
    return Vec2{ std::max(size.X, element.MinSize.X), std::max(size.Y, element.MinSize.Y) };
}

// Visible here and in every ancestor.
bool IsShown(const Element* element)
{
    for (const Element* current = element; current != nullptr; current = current->GetParent())
    {
        if (!current->Visible)
        {
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

Theme Theme::Dark(FontId font)
{
    Theme theme;
    theme.Font = font;
    return theme;
}

Theme Theme::Light(FontId font)
{
    Theme theme;
    theme.Background = Color{ 0.96f, 0.96f, 0.97f, 1.0f };
    theme.Surface = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    theme.SurfaceHovered = Color{ 0.93f, 0.94f, 0.96f, 1.0f };
    theme.SurfacePressed = Color{ 0.88f, 0.89f, 0.92f, 1.0f };
    theme.Accent = Color{ 0.16f, 0.45f, 0.90f, 1.0f };
    theme.AccentHovered = Color{ 0.24f, 0.53f, 0.96f, 1.0f };
    theme.Text = Color{ 0.10f, 0.11f, 0.14f, 1.0f };
    theme.TextMuted = Color{ 0.42f, 0.45f, 0.51f, 1.0f };
    theme.Border = Color{ 0.0f, 0.0f, 0.0f, 0.12f };
    theme.Font = font;
    return theme;
}

// ---------------------------------------------------------------------------
// Element defaults
// ---------------------------------------------------------------------------

Vec2 Element::Measure(Vec2 available)
{
    return Size.Resolve(available);
}

void Element::Arrange(const Rect& bounds)
{
    m_Bounds = bounds;
}

void Element::Paint(DrawList&)
{
}

bool Element::IsEnabled() const
{
    for (const Element* current = this; current != nullptr; current = current->m_Parent)
    {
        if (!current->Enabled)
        {
            return false;
        }
    }
    return true;
}

const Style* Element::GetDefaultAppearance(const Theme&) const
{
    return nullptr;
}

const Style* Element::GetEffectiveAppearance() const
{
    if (!Appearance.IsEmpty())
    {
        return &Appearance;
    }
    const Theme& theme = GetTheme();
    if (!StyleName.empty())
    {
        if (const Style* named = theme.FindStyle(StyleName); named != nullptr && !named->IsEmpty())
        {
            return named;
        }
    }
    const Style* fallback = GetDefaultAppearance(theme);
    return fallback != nullptr && !fallback->IsEmpty() ? fallback : nullptr;
}

bool Element::HitTest(Vec2 point) const
{
    return Contains(m_Bounds, point);
}

void Element::OnEvent(Event&)
{
}

void Element::OnUpdate(float)
{
}

Rect Element::PlaceChild(Element&, size_t, const Rect& content)
{
    return content;
}

bool Element::OnShortcut(const Event&)
{
    return false;
}

void Element::OnChildRemoved(Element&)
{
}

bool Element::WantsText() const
{
    return false;
}

bool Element::IsFocusVisible() const
{
    return m_Tree != nullptr && m_Tree->IsFocusVisible(this);
}

bool Element::IsInside(const Element* ancestor) const
{
    if (ancestor == nullptr)
    {
        return false;
    }
    for (const Element* current = this; current != nullptr; current = current->m_Parent)
    {
        if (current == ancestor)
        {
            return true;
        }
    }
    return false;
}

void Element::ForEachDescendant(const std::function<bool(Element&)>& visit)
{
    // Iterative, so a very deep tree cannot overflow the stack, and in the
    // order the tree paints.
    std::vector<Element*> pending{ this };
    while (!pending.empty())
    {
        Element* current = pending.back();
        pending.pop_back();
        if (!visit(*current))
        {
            return;
        }
        for (auto child = current->m_ChildViews.rbegin(); child != current->m_ChildViews.rend(); ++child)
        {
            pending.push_back(*child);
        }
    }
}

void Element::CapturePointer()
{
    if (m_Tree != nullptr)
    {
        m_Tree->Capture(this);
    }
}

void Element::ReleasePointer()
{
    if (m_Tree != nullptr)
    {
        m_Tree->Release(this);
    }
}

bool Element::HasPointerCapture() const
{
    return m_Tree != nullptr && m_Tree->GetCaptured() == this;
}

bool Element::MoveTo(Element* newParent)
{
    if (m_Tree == nullptr)
    {
        LogMessage(LogLevel::Warning, "ui", "MoveTo on an element that is not in a tree yet; ignoring it.");
        return false;
    }
    return m_Tree->Reparent(this, newParent);
}

void Element::BringToFront()
{
    if (m_Tree != nullptr)
    {
        m_Tree->BringToFront(this);
    }
}

void Element::AnimateUniform(const std::string& name, float x, float y, float z, float w,
                             float seconds, Easing easing)
{
    if (Material.IsValid())
    {
        GetApp().AnimateMaterialUniform(Material, name, x, y, z, w, seconds, easing);
    }
}

void Element::AnimateUniform(const std::string& name, Color color, float seconds, Easing easing)
{
    AnimateUniform(name, color.R, color.G, color.B, color.A, seconds, easing);
}

Rect Element::GetContentBounds() const
{
    return Rect{ m_Bounds.X + Padding, m_Bounds.Y + Padding,
                 std::max(0.0f, m_Bounds.Width - Padding * 2.0f),
                 std::max(0.0f, m_Bounds.Height - Padding * 2.0f) };
}

void Element::AdoptChild(std::unique_ptr<Element> child)
{
    child->m_Parent = this;
    child->m_Tree = m_Tree;

    if (m_Tree != nullptr)
    {
        m_Tree->NotifyElementAdopted(child.get());
    }

    m_ChildViews.push_back(child.get());
    m_Children.push_back(std::move(child));
}

void Element::Remove(Element* child)
{
    if (child == nullptr || m_Tree == nullptr)
    {
        return;
    }
    // Queued so that removing a child from inside its own handler is safe.
    m_Tree->QueueRemoval(this, child);
}

void Element::RemoveAllChildren()
{
    for (Element* child : m_ChildViews)
    {
        Remove(child);
    }
}

const Theme& Element::GetTheme() const
{
    static const Theme fallback;
    return m_Tree != nullptr ? m_Tree->GetTheme() : fallback;
}

void Element::PlaceCentered()
{
    Position = Position2::Center();
    AnchorPoint = Vec2{ 0.5f, 0.5f };
}

void Element::PlaceAtCorner(Corner corner, float margin)
{
    switch (corner)
    {
        case Corner::TopLeft:
            Position = Position2::FromOffset(margin, margin);
            AnchorPoint = Vec2{ 0.0f, 0.0f };
            break;
        case Corner::TopRight:
            Position = Position2{ Dim{ 1.0f, -margin }, Dim{ 0.0f, margin } };
            AnchorPoint = Vec2{ 1.0f, 0.0f };
            break;
        case Corner::BottomLeft:
            Position = Position2{ Dim{ 0.0f, margin }, Dim{ 1.0f, -margin } };
            AnchorPoint = Vec2{ 0.0f, 1.0f };
            break;
        case Corner::BottomRight:
            Position = Position2{ Dim{ 1.0f, -margin }, Dim{ 1.0f, -margin } };
            AnchorPoint = Vec2{ 1.0f, 1.0f };
            break;
    }
}

void Element::RequestFocus()
{
    if (m_Tree != nullptr)
    {
        m_Tree->RequestFocus(this);
    }
}

Vec2 Element::MeasureText(const std::string& text) const
{
    if (m_Tree == nullptr || m_Tree->GetFonts() == nullptr)
    {
        return Vec2{};
    }
    return m_Tree->GetFonts()->Measure(GetTheme().Font, text);
}

App Element::GetApp() const
{
    return detail::MakeAppView(m_Tree != nullptr ? m_Tree->GetApp() : nullptr);
}

std::weak_ptr<const void> Element::GetLifetime() const
{
    if (!m_Lifetime)
    {
        m_Lifetime = std::make_shared<bool>(true);
    }
    return m_Lifetime;
}

Vec2 Element::MeasureText(const std::string& text, FontId font) const
{
    if (m_Tree == nullptr || m_Tree->GetFonts() == nullptr)
    {
        return Vec2{};
    }
    return m_Tree->GetFonts()->Measure(font.IsValid() ? font : GetTheme().Font, text);
}

float Element::GetLineHeight() const
{
    if (m_Tree == nullptr || m_Tree->GetFonts() == nullptr)
    {
        return 0.0f;
    }
    return m_Tree->GetFonts()->GetLineHeight(GetTheme().Font);
}

float Element::GetLineHeight(FontId font) const
{
    if (m_Tree == nullptr || m_Tree->GetFonts() == nullptr)
    {
        return 0.0f;
    }
    return m_Tree->GetFonts()->GetLineHeight(font.IsValid() ? font : GetTheme().Font);
}

void Element::MarkLayoutDirty()
{
    // Layout runs every frame, so there is nothing to mark.
}

namespace detail
{

// ---------------------------------------------------------------------------
// UiTree
// ---------------------------------------------------------------------------

void UiTree::Initialize(const Theme& theme, FontStore* fonts, RenderTargetStore* renderTargets,
                        AppState* app)
{
    m_Theme = theme;
    m_Fonts = fonts;
    m_RenderTargets = renderTargets;
    m_App = app;

    m_Root = std::make_unique<Element>();
    m_Root->Name = "Root";
    m_Root->Size = Size2::Fill();
    m_Root->m_Tree = this;

    // The overlay is never itself the target of anything: a press that lands
    // on no popup and no window falls through to the interface beneath.
    m_Overlay = std::make_unique<Element>();
    m_Overlay->Name = "Overlay";
    m_Overlay->Size = Size2::Fill();
    m_Overlay->Interactive = false;
    m_Overlay->m_Tree = this;
}

void UiTree::Shutdown()
{
    m_Hovered = nullptr;
    m_Focused = nullptr;
    m_Captured = nullptr;
    m_TooltipSource = nullptr;

    // The interface before the overlay: a menu bar or a drop-down owns a
    // popup that lives in the overlay, and lets go of it as it is destroyed,
    // so the popup has to still exist at that moment.
    m_Root.reset();
    m_Overlay.reset();
    m_PendingRemovals.clear();
}

std::vector<Element*>& UiTree::ScratchChildren(size_t depth)
{
    if (m_ChildScratch.size() <= depth)
    {
        m_ChildScratch.resize(depth + 1);
    }
    return m_ChildScratch[depth];
}

std::vector<Vec2>& UiTree::ScratchSizes(size_t depth)
{
    if (m_SizeScratch.size() <= depth)
    {
        m_SizeScratch.resize(depth + 1);
    }
    return m_SizeScratch[depth];
}

void UiTree::NotifyElementAdopted(Element* element)
{
    if (element == nullptr)
    {
        return;
    }

    element->m_Tree = this;
    for (Element* child : element->m_ChildViews)
    {
        NotifyElementAdopted(child);
    }
}

void UiTree::QueueRemoval(Element* parent, Element* child)
{
    m_PendingRemovals.push_back(PendingRemoval{ parent, child });
}

bool UiTree::Contains(const Element* element) const
{
    if (element == nullptr)
    {
        return false;
    }

    // Compared by address only, never dereferenced, so asking about an
    // element that has already been destroyed is safe.
    bool found = false;
    for (Element* layer : { m_Root.get(), m_Overlay.get() })
    {
        if (layer == nullptr || found)
        {
            continue;
        }
        layer->ForEachDescendant([&](Element& candidate) {
            if (&candidate == element)
            {
                found = true;
                return false;
            }
            return true;
        });
    }
    return found;
}

void UiTree::ForgetSubtree(Element* element, std::unordered_set<Element*>& doomed)
{
    element->ForEachDescendant([&](Element& candidate) {
        doomed.insert(&candidate);
        return true;
    });

    // Anything that points into the subtree has to let go before it goes,
    // not only something pointing at its top: a panel removed while the text
    // field inside it has the keyboard must not leave the keyboard with a
    // field that no longer exists.
    if (m_Hovered != nullptr && doomed.count(m_Hovered) != 0)
    {
        m_Hovered = nullptr;
    }
    if (m_Focused != nullptr && doomed.count(m_Focused) != 0)
    {
        m_Focused = nullptr;
        m_FocusVisible = false;
        SetTextInputActive(m_App, false);
    }
    if (m_Captured != nullptr && doomed.count(m_Captured) != 0)
    {
        m_Captured = nullptr;
    }
    if (m_TooltipSource != nullptr && doomed.count(m_TooltipSource) != 0)
    {
        m_TooltipSource = nullptr;
        m_TooltipShown = false;
    }

    // A popup opened from inside the subtree lives on in the overlay, and
    // must not go on naming the element that opened it.
    if (m_Overlay != nullptr)
    {
        for (Element* child : m_Overlay->m_ChildViews)
        {
            auto* popup = dynamic_cast<Popup*>(child);
            if (popup != nullptr && popup->Owner != nullptr && doomed.count(popup->Owner) != 0)
            {
                popup->Owner = nullptr;
            }
        }
    }
}

void UiTree::ApplyPendingRemovals()
{
    if (m_PendingRemovals.empty())
    {
        return;
    }

    // Everything destroyed so far in this batch. A later entry naming one of
    // them (removed twice, or removed along with a parent removed earlier) is
    // skipped without touching its pointer, so RemoveAllChildren followed by
    // a child removing itself is safe.
    std::unordered_set<Element*> doomed;

    // Taken by value: a handler below may queue more.
    std::vector<PendingRemoval> removals;
    removals.swap(m_PendingRemovals);

    for (const PendingRemoval& removal : removals)
    {
        Element* child = removal.Child;
        if (child == nullptr || doomed.count(child) != 0)
        {
            continue;
        }

        // The child's parent now, not when the removal was queued: it may
        // have been moved since.
        Element* parent = child->m_Parent;
        if (parent == nullptr)
        {
            continue;
        }

        ForgetSubtree(child, doomed);
        parent->OnChildRemoved(*child);

        auto& views = parent->m_ChildViews;
        views.erase(std::remove(views.begin(), views.end(), child), views.end());

        auto& owned = parent->m_Children;
        owned.erase(std::remove_if(owned.begin(), owned.end(),
                                   [&](const std::unique_ptr<Element>& candidate) {
                                       return candidate.get() == child;
                                   }),
                    owned.end());
    }

    // A destructor in this batch may have asked for something else to be
    // removed (a menu bar releasing its menu, for example) that this batch
    // then destroyed as well. Those requests name elements that no longer exist and are dropped
    // before anything reads them.
    m_PendingRemovals.erase(std::remove_if(m_PendingRemovals.begin(), m_PendingRemovals.end(),
                                           [&](const PendingRemoval& removal) {
                                               return doomed.count(removal.Child) != 0;
                                           }),
                            m_PendingRemovals.end());
}

bool UiTree::Reparent(Element* element, Element* newParent)
{
    if (element == nullptr || newParent == nullptr)
    {
        return false;
    }
    if (element == m_Root.get() || element == m_Overlay.get())
    {
        LogMessage(LogLevel::Warning, "ui", "The root and the overlay cannot be moved.");
        return false;
    }
    if (newParent->m_Tree != this)
    {
        LogMessage(LogLevel::Warning, "ui", "MoveTo: the new parent belongs to a different tree.");
        return false;
    }
    if (newParent->IsInside(element))
    {
        LogMessage(LogLevel::Warning, "ui", "MoveTo: an element cannot be moved inside itself.");
        return false;
    }

    Element* oldParent = element->m_Parent;
    if (oldParent == nullptr || oldParent == newParent)
    {
        return oldParent == newParent;
    }

    auto owned = std::find_if(oldParent->m_Children.begin(), oldParent->m_Children.end(),
                              [&](const std::unique_ptr<Element>& candidate) {
                                  return candidate.get() == element;
                              });
    if (owned == oldParent->m_Children.end())
    {
        return false;
    }

    std::unique_ptr<Element> held = std::move(*owned);
    oldParent->m_Children.erase(owned);
    auto& views = oldParent->m_ChildViews;
    views.erase(std::remove(views.begin(), views.end(), element), views.end());

    element->m_Parent = newParent;
    newParent->m_ChildViews.push_back(element);
    newParent->m_Children.push_back(std::move(held));
    return true;
}

void UiTree::BringToFront(Element* element)
{
    if (element == nullptr || element->m_Parent == nullptr)
    {
        return;
    }

    Element* parent = element->m_Parent;
    auto& views = parent->m_ChildViews;
    auto view = std::find(views.begin(), views.end(), element);
    if (view == views.end() || view + 1 == views.end())
    {
        return;
    }
    views.erase(view);
    views.push_back(element);

    // Ownership order follows drawing order, so the two lists never disagree
    // about which child is which.
    auto& owned = parent->m_Children;
    auto held = std::find_if(owned.begin(), owned.end(),
                             [&](const std::unique_ptr<Element>& candidate) { return candidate.get() == element; });
    if (held != owned.end())
    {
        std::unique_ptr<Element> moved = std::move(*held);
        owned.erase(held);
        owned.push_back(std::move(moved));
    }
}

void UiTree::LayoutInto(Element* element, const Rect& bounds, size_t depth)
{
    if (element == nullptr || !element->Visible)
    {
        return;
    }

    element->Arrange(bounds);

    const Rect content = element->GetContentBounds();
    const Vec2 available{ content.Width, content.Height };

    if (element->ChildLayout == LayoutMode::Absolute)
    {
        for (Element* child : element->m_ChildViews)
        {
            if (!child->Visible)
            {
                continue;
            }

            const Vec2 size = ApplyMinimum(*child, child->Measure(available));
            const Vec2 position = child->Position.Resolve(available);

            // The anchor decides which point of the child the position names,
            // so centering is a property rather than an arithmetic expression.
            const Rect childBounds{ content.X + position.X - size.X * child->AnchorPoint.X,
                                    content.Y + position.Y - size.Y * child->AnchorPoint.Y, size.X,
                                    size.Y };

            LayoutInto(child, childBounds, depth + 1);
        }
        return;
    }

    if (element->ChildLayout == LayoutMode::Custom)
    {
        const std::vector<Element*>& children = element->m_ChildViews;
        for (size_t index = 0; index < children.size(); ++index)
        {
            Element* child = children[index];
            if (child->Visible)
            {
                LayoutInto(child, element->PlaceChild(*child, index, content), depth + 1);
            }
        }
        return;
    }

    // Stacks lay children out along one axis and give them the full extent of
    // the other, so a column of buttons lines up without each button setting
    // its width. Children with Flex share what is left.
    const bool vertical = (element->ChildLayout == LayoutMode::Vertical);
    const float extent = vertical ? content.Height : content.Width;

    std::vector<Vec2>& sizes = ScratchSizes(depth);
    sizes.clear();

    float fixed = 0.0f;
    float flexTotal = 0.0f;
    int shown = 0;

    for (Element* child : element->m_ChildViews)
    {
        if (!child->Visible)
        {
            sizes.push_back(Vec2{});
            continue;
        }
        ++shown;

        if (child->Flex > 0.0f)
        {
            flexTotal += child->Flex;
            sizes.push_back(child->MinSize);
            fixed += vertical ? child->MinSize.Y : child->MinSize.X;
            continue;
        }

        const Vec2 size = ApplyMinimum(*child, child->Measure(available));
        sizes.push_back(size);
        fixed += vertical ? size.Y : size.X;
    }

    const float spacing = element->Spacing * static_cast<float>(std::max(0, shown - 1));
    const float remaining = std::max(0.0f, extent - fixed - spacing);

    float cursor = vertical ? content.Y : content.X;

    for (size_t index = 0; index < element->m_ChildViews.size(); ++index)
    {
        Element* child = element->m_ChildViews[index];
        if (!child->Visible)
        {
            continue;
        }

        Vec2 size = sizes[index];
        if (child->Flex > 0.0f && flexTotal > 0.0f)
        {
            // The minimum was already counted as fixed, so the share is added
            // on top of it rather than competing with it.
            const float share = remaining * child->Flex / flexTotal;
            if (vertical)
            {
                size.Y += share;
            }
            else
            {
                size.X += share;
            }
        }

        Rect childBounds;
        if (vertical)
        {
            childBounds = Rect{ content.X, cursor, content.Width, size.Y };
            cursor += size.Y + element->Spacing;
        }
        else
        {
            childBounds = Rect{ cursor, content.Y, size.X, content.Height };
            cursor += size.X + element->Spacing;
        }

        // Recursing rather than arranging in place is what lets a stack contain
        // another stack: each element decides its own children's layout.
        LayoutInto(child, childBounds, depth + 1);
    }
}

namespace
{

enum LookState
{
    LookNormal,
    LookHovered,
    LookPressed,
    LookFocused,
    LookSelected,
    LookDisabled
};

const BoxStyle& StyleFor(const Style& style, int state)
{
    switch (state)
    {
    case LookHovered: return style.Hovered ? *style.Hovered : style.Normal;
    case LookPressed: return style.Pressed ? *style.Pressed : style.Normal;
    case LookFocused: return style.Focused ? *style.Focused : style.Normal;
    case LookSelected: return style.Selected ? *style.Selected : style.Normal;
    case LookDisabled: return style.Disabled ? *style.Disabled : style.Normal;
    default: return style.Normal;
    }
}

float Mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

Color MixColor(const Color& a, const Color& b, float t)
{
    return Color{ Mix(a.R, b.R, t), Mix(a.G, b.G, t), Mix(a.B, b.B, t), Mix(a.A, b.A, t) };
}

Vec2 MixVec(const Vec2& a, const Vec2& b, float t)
{
    return Vec2{ Mix(a.X, b.X, t), Mix(a.Y, b.Y, t) };
}

// A fill that is not there, shaped like the other so the two can be eased:
// the same colours, transparent.
Fill Vanished(const Fill& like)
{
    Fill fill = like;
    fill.Color.A = 0.0f;
    for (GradientStop& stop : fill.Stops)
    {
        stop.Color.A = 0.0f;
    }
    return fill;
}

Fill MixFill(const Fill& a, const Fill& b, float t)
{
    if (a.Kind == FillKind::None && b.Kind != FillKind::None)
    {
        return MixFill(Vanished(b), b, t);
    }
    if (b.Kind == FillKind::None && a.Kind != FillKind::None)
    {
        return MixFill(a, Vanished(a), t);
    }

    const bool sameShape = a.Kind == b.Kind && a.Stops.size() == b.Stops.size() &&
                           (a.Kind != FillKind::Image || (a.Image.Index == b.Image.Index &&
                                                          a.Image.Generation == b.Image.Generation));
    if (!sameShape)
    {
        // Nothing in between two unlike fills; it changes halfway.
        return t < 0.5f ? a : b;
    }

    Fill fill = b;
    fill.Color = MixColor(a.Color, b.Color, t);
    for (size_t index = 0; index < fill.Stops.size(); ++index)
    {
        fill.Stops[index].Position = Mix(a.Stops[index].Position, b.Stops[index].Position, t);
        fill.Stops[index].Color = MixColor(a.Stops[index].Color, b.Stops[index].Color, t);
    }
    fill.Angle = Mix(a.Angle, b.Angle, t);
    fill.Center = MixVec(a.Center, b.Center, t);
    fill.Radius = Mix(a.Radius, b.Radius, t);
    fill.TileScale = Mix(a.TileScale, b.TileScale, t);
    return fill;
}

Shadow MixShadow(const Shadow& a, const Shadow& b, float t)
{
    Shadow shadow = b;
    shadow.Color = MixColor(a.Color, b.Color, t);
    shadow.Offset = MixVec(a.Offset, b.Offset, t);
    shadow.Blur = Mix(a.Blur, b.Blur, t);
    shadow.Spread = Mix(a.Spread, b.Spread, t);
    shadow.Inset = t < 0.5f ? a.Inset : b.Inset;
    return shadow;
}

BoxStyle MixLook(const BoxStyle& a, const BoxStyle& b, float t)
{
    BoxStyle look = b;
    look.Background = MixFill(a.Background, b.Background, t);
    look.BorderColor = MixColor(a.BorderColor, b.BorderColor, t);
    look.BorderWidth = Mix(a.BorderWidth, b.BorderWidth, t);
    look.Radius = CornerRadii{ Mix(a.Radius.TopLeft, b.Radius.TopLeft, t),
                               Mix(a.Radius.TopRight, b.Radius.TopRight, t),
                               Mix(a.Radius.BottomRight, b.Radius.BottomRight, t),
                               Mix(a.Radius.BottomLeft, b.Radius.BottomLeft, t) };

    // Shadows pair up in order; one without a partner fades in or out.
    look.Shadows.clear();
    const size_t count = std::max(a.Shadows.size(), b.Shadows.size());
    for (size_t index = 0; index < count; ++index)
    {
        if (index < a.Shadows.size() && index < b.Shadows.size())
        {
            look.Shadows.push_back(MixShadow(a.Shadows[index], b.Shadows[index], t));
        }
        else if (index < a.Shadows.size())
        {
            Shadow gone = a.Shadows[index];
            gone.Color.A = 0.0f;
            look.Shadows.push_back(MixShadow(a.Shadows[index], gone, t));
        }
        else
        {
            Shadow gone = b.Shadows[index];
            gone.Color.A = 0.0f;
            look.Shadows.push_back(MixShadow(gone, b.Shadows[index], t));
        }
    }

    look.BackdropBlur = Mix(a.BackdropBlur, b.BackdropBlur, t);
    look.Opacity = Mix(a.Opacity, b.Opacity, t);
    look.TextColor = MixColor(a.TextColor, b.TextColor, t);
    look.Scale = Mix(a.Scale, b.Scale, t);
    look.Offset = MixVec(a.Offset, b.Offset, t);
    return look;
}

// An element's own transform as it is drawn: its Transform, then the nudge
// its look gives it, both about its own box.
Affine2D OwnTransform(const Element& element)
{
    const Rect bounds = element.GetBounds();
    Affine2D transform;
    if (!element.Transform.IsIdentity())
    {
        const Vec2 pivot{ bounds.X + element.Transform.Pivot.X * bounds.Width,
                          bounds.Y + element.Transform.Pivot.Y * bounds.Height };
        transform = Affine2D::Translation(element.Transform.Translate) *
                    Affine2D::About(pivot, element.Transform.Rotation, element.Transform.Scale);
    }
    if (element.HasAppearance())
    {
        const BoxStyle& look = element.GetCurrentLook();
        if (look.Scale != 1.0f || look.Offset.X != 0.0f || look.Offset.Y != 0.0f)
        {
            const Vec2 center{ bounds.X + bounds.Width * 0.5f, bounds.Y + bounds.Height * 0.5f };
            transform = transform * Affine2D::Translation(look.Offset) *
                        Affine2D::About(center, 0.0f, Vec2{ look.Scale, look.Scale });
        }
    }
    return transform;
}

} // namespace

Vec2 UiTree::LocalPoint(const Element* element, Vec2 windowPoint) const
{
    // Most trees have no transforms at all, and pay nothing for them.
    bool any = false;
    for (const Element* current = element; current != nullptr && !any; current = current->GetParent())
    {
        any = !OwnTransform(*current).IsIdentity();
    }
    if (!any)
    {
        return windowPoint;
    }

    Affine2D chain;
    for (const Element* current = element; current != nullptr; current = current->GetParent())
    {
        chain = OwnTransform(*current) * chain;
    }
    return chain.Inverse().Apply(windowPoint);
}

void UiTree::Deliver(Element* element, Event& event)
{
    const Vec2 window = event.Position;
    event.Position = LocalPoint(element, window);
    element->OnEvent(event);
    event.Position = window;
}

void UiTree::UpdateLook(Element* element, float deltaSeconds)
{
    const Style* effective = element->GetEffectiveAppearance();
    if (effective == nullptr)
    {
        element->m_LookState = -1;
        return;
    }

    const Style& style = *effective;
    const bool pressedWithin = m_Captured != nullptr && m_Captured->IsInside(element);
    const bool hoveredWithin = m_Hovered != nullptr && m_Hovered->IsInside(element);
    const bool focusedWithin = m_Focused != nullptr && m_Focused->IsInside(element);

    int state = LookNormal;
    if (!element->IsEnabled() && style.Disabled)
    {
        state = LookDisabled;
    }
    else if (pressedWithin && style.Pressed)
    {
        state = LookPressed;
    }
    else if (element->IsSelected() && style.Selected)
    {
        state = LookSelected;
    }
    else if (hoveredWithin && style.Hovered)
    {
        state = LookHovered;
    }
    else if (focusedWithin && style.Focused)
    {
        state = LookFocused;
    }

    if (element->m_LookState < 0)
    {
        // A new element starts in its state rather than easing into it.
        element->m_LookState = state;
        element->m_LookProgress = 1.0f;
    }
    else if (state != element->m_LookState)
    {
        element->m_LookFrom = element->m_Look;
        element->m_LookState = state;
        element->m_LookProgress = 0.0f;
    }

    element->m_LookProgress =
        std::min(1.0f, element->m_LookProgress + deltaSeconds / std::max(style.Transition, 1e-4f));

    // The target is read afresh every frame, so a change to the Appearance
    // itself shows at once.
    const BoxStyle& target = StyleFor(style, state);
    element->m_Look = element->m_LookProgress >= 1.0f
                          ? target
                          : MixLook(element->m_LookFrom, target, ApplyEasing(style.Curve, element->m_LookProgress));
}

void UiTree::UpdateElement(Element* element, float deltaSeconds, size_t depth)
{
    if (element == nullptr || !element->Visible)
    {
        return;
    }

    UpdateLook(element, deltaSeconds);
    element->OnUpdate(deltaSeconds);

    // A snapshot, because a handler may move or reorder children; kept in a
    // buffer reused every frame, because a snapshot per element per frame
    // would be the interface's largest source of allocation. Removal is
    // deferred, so every pointer in the snapshot stays valid throughout.
    std::vector<Element*>& children = ScratchChildren(depth);
    children.assign(element->m_ChildViews.begin(), element->m_ChildViews.end());

    for (size_t index = 0; index < children.size(); ++index)
    {
        UpdateElement(children[index], deltaSeconds, depth + 1);
    }
}

void UiTree::Update(float deltaSeconds, Vec2 viewportSize)
{
    if (m_Root == nullptr)
    {
        return;
    }

    ApplyPendingRemovals();

    m_Viewport = viewportSize;

    // The root and the overlay always fill the window, so their bounds are
    // given rather than resolved from a parent.
    const Rect viewport{ 0.0f, 0.0f, viewportSize.X, viewportSize.Y };
    LayoutInto(m_Root.get(), viewport, 0);
    LayoutInto(m_Overlay.get(), viewport, 0);

    UpdateElement(m_Root.get(), deltaSeconds, 0);
    UpdateElement(m_Overlay.get(), deltaSeconds, 0);

    ApplyPendingRemovals();

    // When what is under a still pointer changes (a popup opening beneath it,
    // for example), the cursor should change without waiting for the pointer
    // to move.
    UpdateTooltip(deltaSeconds);
    UpdateCursor();
}

void UiTree::PaintElement(Element* element, DrawList& drawList)
{
    if (element == nullptr || !element->Visible)
    {
        return;
    }

    // Its opacity and transform hold for everything inside it too. A
    // disabled element dims, unless its Appearance says how it looks instead.
    const Style* look = element->GetEffectiveAppearance();
    float opacity = element->Opacity * (look != nullptr ? element->m_Look.Opacity : 1.0f);
    if (!element->Enabled && !(look != nullptr && look->Disabled))
    {
        opacity *= 0.45f;
    }
    if (opacity <= 0.001f)
    {
        return;
    }

    const Affine2D own = OwnTransform(*element);
    const bool transformed = !own.IsIdentity();
    const bool faded = opacity < 1.0f;
    if (transformed)
    {
        drawList.PushTransform(own);
    }
    if (faded)
    {
        drawList.PushOpacity(opacity);
    }

    PaintTransformed(element, drawList);

    if (faded)
    {
        drawList.PopOpacity();
    }
    if (transformed)
    {
        drawList.PopTransform();
    }
}

void UiTree::PaintTransformed(Element* element, DrawList& drawList)
{
    const bool wantsTarget = element->RenderToTexture || !element->PostProcess.empty();

    if (wantsTarget && m_RenderTargets != nullptr)
    {
        RenderTargetRecord* target = m_RenderTargets->Acquire(element, element->GetBounds(), element->PostProcess,
                                                              drawList.GetPixelScale());

        if (target != nullptr)
        {
            // Everything the element would have drawn goes into its own list
            // instead. That list renders before the main pass, and here the
            // main list gets one quad showing the finished result.
            PaintContents(element, target->Contents);
            drawList.DrawPremultipliedTexture(target->UnitBounds, m_RenderTargets->FinalTexture(*target));
            return;
        }
    }

    PaintContents(element, drawList);
}

void UiTree::PaintContents(Element* element, DrawList& drawList)
{
    // An element's material applies to its own painting and is cleared
    // afterwards, so a custom shader is one assignment on any element,
    // built-in or your own, with no drawing code changed.
    // The Appearance goes beneath everything, and outside any material: a
    // material shades what the element paints, not its box.
    if (element->HasAppearance())
    {
        drawList.DrawBoxShape(element->GetBounds(), element->m_Look);
    }

    const bool hasMaterial = element->Material.IsValid();
    const MaterialId previousMaterial = drawList.GetMaterial();

    if (hasMaterial)
    {
        drawList.SetMaterial(element->Material);
    }

    // A copy, so a painter that replaces itself is not destroyed mid-call.
    if (element->Painter)
    {
        const auto painter = element->Painter;
        painter(drawList, *element);
    }
    else
    {
        element->Paint(drawList);
    }

    if (hasMaterial)
    {
        drawList.SetMaterial(previousMaterial);
    }

    if (element->ClipChildren)
    {
        drawList.PushClip(element->GetBounds());
    }

    for (Element* child : element->m_ChildViews)
    {
        PaintElement(child, drawList);
    }

    if (element->ClipChildren)
    {
        drawList.PopClip();
    }
}

void UiTree::PaintOverlay(DrawList& drawList)
{
    if (m_Overlay == nullptr)
    {
        return;
    }

    // A modal popup dims everything beneath it, including earlier overlay
    // children, so the shade is laid down just before it rather than once
    // for the whole overlay.
    for (Element* child : m_Overlay->m_ChildViews)
    {
        if (!child->Visible)
        {
            continue;
        }
        if (const auto* popup = dynamic_cast<const Popup*>(child); popup != nullptr && popup->Modal)
        {
            drawList.FillRect(Rect{ 0.0f, 0.0f, m_Viewport.X, m_Viewport.Y }, popup->ModalShade);
        }
        PaintElement(child, drawList);
    }
}

void UiTree::PaintTooltip(DrawList& drawList)
{
    if (!m_TooltipShown || m_TooltipSource == nullptr || m_Fonts == nullptr)
    {
        return;
    }

    const std::string& text = m_TooltipSource->Tooltip;
    if (text.empty())
    {
        return;
    }

    const FontId font = m_Theme.Font;
    const float padding = 7.0f;
    const float maximumWidth = 320.0f;

    // Wrapped only when it has to be, so a short tip hugs its text.
    const Vec2 natural = m_Fonts->Measure(font, text);
    float width = std::min(natural.X, maximumWidth);
    float height = natural.Y;
    if (natural.X > maximumWidth)
    {
        const std::vector<size_t> breaks = m_Fonts->WrapLines(font, text, maximumWidth);
        height = m_Fonts->GetLineHeight(font) * static_cast<float>(std::max<size_t>(1, breaks.size()));
    }

    Rect box{ m_TooltipAnchor.X + 12.0f, m_TooltipAnchor.Y + 20.0f, width + padding * 2.0f,
              height + padding * 2.0f };

    // Kept on screen: flipped above the pointer at the bottom edge, pulled in
    // at the right.
    if (box.X + box.Width > m_Viewport.X - 4.0f)
    {
        box.X = std::max(4.0f, m_Viewport.X - 4.0f - box.Width);
    }
    if (box.Y + box.Height > m_Viewport.Y - 4.0f)
    {
        box.Y = std::max(4.0f, m_TooltipAnchor.Y - 8.0f - box.Height);
    }

    Color textColor = m_Theme.Text;
    if (!m_Theme.Styles.Tooltip.IsEmpty())
    {
        const BoxStyle& look = m_Theme.Styles.Tooltip.Normal;
        drawList.DrawBox(box, look);
        textColor = Color{ Mix(textColor.R, look.TextColor.R, look.TextColor.A),
                           Mix(textColor.G, look.TextColor.G, look.TextColor.A),
                           Mix(textColor.B, look.TextColor.B, look.TextColor.A), textColor.A };
    }
    else
    {
        const Color fill{ m_Theme.Surface.R * 0.8f, m_Theme.Surface.G * 0.8f, m_Theme.Surface.B * 0.8f, 0.97f };
        drawList.FillRoundedRect(Rect{ box.X, box.Y + 2.0f, box.Width, box.Height }, 6.0f,
                                 Color{ 0.0f, 0.0f, 0.0f, 0.25f });
        drawList.FillRoundedRect(box, 6.0f, fill);
        drawList.StrokeRect(box, 1.0f, m_Theme.Border, 6.0f);
    }
    drawList.DrawTextWrapped(text, Rect{ box.X + padding, box.Y + padding, width + 1.0f, height + 1.0f }, font,
                             textColor, TextAlign::Left);
}

void UiTree::Paint(DrawList& drawList)
{
    PaintElement(m_Root.get(), drawList);
    PaintOverlay(drawList);
    PaintTooltip(drawList);
}

Element* UiTree::PickDeepest(Element* element, Vec2 point)
{
    // A disabled element takes no pointer, and nor does anything in it.
    if (element == nullptr || !element->Visible || !element->Enabled)
    {
        return nullptr;
    }

    // The point, moved into the element's own layout: it is found where it
    // is drawn, however it has been moved, turned, or scaled.
    const Affine2D own = OwnTransform(*element);
    if (!own.IsIdentity())
    {
        point = own.Inverse().Apply(point);
    }

    // A clipping parent that does not contain the point cannot have a child
    // that does, so the whole subtree is skipped.
    if (element->ClipChildren && !opane::Contains(element->GetBounds(), point))
    {
        return nullptr;
    }

    // Later children draw on top, so they are tested first.
    for (auto child = element->m_ChildViews.rbegin(); child != element->m_ChildViews.rend(); ++child)
    {
        if (Element* hit = PickDeepest(*child, point))
        {
            return hit;
        }
    }

    if (element->Interactive && element->HitTest(point))
    {
        return element;
    }

    return nullptr;
}

void UiTree::CollectOpenPopups(std::vector<Popup*>& out) const
{
    out.clear();
    if (m_Overlay == nullptr)
    {
        return;
    }
    for (Element* child : m_Overlay->m_ChildViews)
    {
        if (!child->Visible)
        {
            continue;
        }
        if (auto* popup = dynamic_cast<Popup*>(child))
        {
            out.push_back(popup);
        }
    }
}

Popup* UiTree::TopModal() const
{
    if (m_Overlay == nullptr)
    {
        return nullptr;
    }
    for (auto child = m_Overlay->m_ChildViews.rbegin(); child != m_Overlay->m_ChildViews.rend(); ++child)
    {
        if (!(*child)->Visible)
        {
            continue;
        }
        if (auto* popup = dynamic_cast<Popup*>(*child); popup != nullptr && popup->Modal)
        {
            return popup;
        }
    }
    return nullptr;
}

Element* UiTree::Pick(Vec2 point, bool* blocked)
{
    if (blocked != nullptr)
    {
        *blocked = false;
    }

    Popup* modal = TopModal();
    if (modal != nullptr)
    {
        // Only what is in front of the modal, or in it, can be reached.
        const auto& layer = m_Overlay->m_ChildViews;
        auto at = std::find(layer.begin(), layer.end(), modal);
        for (auto child = layer.rbegin(); child != layer.rend(); ++child)
        {
            if (Element* hit = PickDeepest(*child, point))
            {
                return hit;
            }
            if (child.base() - 1 == at)
            {
                break;
            }
        }
        if (blocked != nullptr)
        {
            *blocked = true;
        }
        return nullptr;
    }

    if (Element* hit = PickDeepest(m_Overlay.get(), point))
    {
        return hit;
    }
    return PickDeepest(m_Root.get(), point);
}

WindowRegion UiTree::RegionAt(Vec2 point)
{
    for (Element* current = Pick(point); current != nullptr; current = current->GetParent())
    {
        if (current->Region != WindowRegion::None)
        {
            return current->Region;
        }
        if (current->Focusable)
        {
            return WindowRegion::None;
        }
    }
    return WindowRegion::None;
}

void UiTree::Dispatch(Element* element, Event& event)
{
    // Bubbling: the deepest element sees the event first, then each ancestor,
    // until one marks it handled.
    for (Element* current = element; current != nullptr; current = current->GetParent())
    {
        Deliver(current, event);
        if (event.Handled)
        {
            return;
        }
    }
}

void UiTree::SetHovered(Element* element, Vec2 position)
{
    if (m_Hovered == element)
    {
        return;
    }

    if (m_Hovered != nullptr)
    {
        m_Hovered->m_Hovered = false;
        Event leave;
        leave.Type = EventType::PointerLeave;
        leave.Position = position;
        Deliver(m_Hovered, leave);
    }

    m_Hovered = element;

    if (m_Hovered != nullptr)
    {
        m_Hovered->m_Hovered = true;
        Event enter;
        enter.Type = EventType::PointerEnter;
        enter.Position = position;
        Deliver(m_Hovered, enter);
    }
}

bool UiTree::HandlePointerMove(Vec2 position)
{
    if (m_Root == nullptr)
    {
        return false;
    }

    const float moved = std::abs(position.X - m_TooltipAnchor.X) + std::abs(position.Y - m_TooltipAnchor.Y);
    m_Pointer = position;
    m_PointerInside = true;

    if (moved > TooltipSlop && !m_TooltipShown)
    {
        m_TooltipTime = 0.0f;
        m_TooltipAnchor = position;
    }

    // While a drag is in progress the captured element keeps receiving moves
    // even when the pointer leaves its bounds.
    if (m_Captured != nullptr)
    {
        Event move;
        move.Type = EventType::PointerMove;
        move.Position = position;
        Deliver(m_Captured, move);
        UpdateCursor();
        return true;
    }

    bool blocked = false;
    Element* hit = Pick(position, &blocked);
    SetHovered(hit, position);

    if (hit == nullptr)
    {
        UpdateCursor();
        return blocked;
    }

    Event move;
    move.Type = EventType::PointerMove;
    move.Position = position;
    Dispatch(hit, move);
    UpdateCursor();
    return true;
}

bool UiTree::DismissPopupsFor(Element* hit)
{
    std::vector<Popup*> open;
    CollectOpenPopups(open);
    if (open.empty())
    {
        return false;
    }

    // The popup the press landed in, if any, and every popup that owns it, so
    // a press in a submenu keeps the menu that opened it.
    std::vector<Popup*> kept;
    for (Element* current = hit; current != nullptr; current = current->GetParent())
    {
        if (auto* popup = dynamic_cast<Popup*>(current))
        {
            for (Popup* owner = popup; owner != nullptr; owner = owner->Owner)
            {
                kept.push_back(owner);
            }
            break;
        }
    }

    bool closed = false;
    // Front to back, so a submenu closes before the menu that owns it.
    for (auto popup = open.rbegin(); popup != open.rend(); ++popup)
    {
        if (!(*popup)->DismissOnOutsideClick || !(*popup)->IsOpen())
        {
            continue;
        }
        if (std::find(kept.begin(), kept.end(), *popup) != kept.end())
        {
            continue;
        }
        (*popup)->Close();
        closed = true;
    }
    return closed;
}

bool UiTree::HandlePointerButton(Vec2 position, MouseButton button, bool down)
{
    if (m_Root == nullptr)
    {
        return false;
    }

    m_Pointer = position;

    // Any press or release ends a tooltip; it was covering what is being
    // pointed at.
    m_TooltipShown = false;
    m_TooltipTime = 0.0f;
    m_TooltipAnchor = position;

    if (!down && m_SwallowRelease)
    {
        m_SwallowRelease = false;
        return true;
    }

    if (!down && m_Captured != nullptr)
    {
        Element* captured = m_Captured;
        m_Captured = nullptr;
        captured->m_Pressed = false;

        Event release;
        release.Type = EventType::PointerUp;
        release.Position = position;
        release.Button = button;
        Deliver(captured, release);
        UpdateCursor();
        return true;
    }

    bool blocked = false;
    Element* hit = Pick(position, &blocked);

    if (down)
    {
        // A press outside an open popup closes it and is spent there, so the
        // click that dismisses a menu does not also press what was under it.
        if (DismissPopupsFor(hit) || blocked)
        {
            m_SwallowRelease = true;
            return true;
        }

        // Focus goes to the nearest thing that takes it, or nowhere: clicking
        // empty space takes the keyboard away from a text field, the way it
        // does everywhere else.
        Element* focusable = hit;
        while (focusable != nullptr && !focusable->Focusable)
        {
            focusable = focusable->GetParent();
        }
        RequestFocus(focusable != nullptr ? focusable : hit, false);
    }

    if (hit == nullptr)
    {
        return blocked;
    }

    if (down)
    {
        m_Captured = hit;
        hit->m_Pressed = true;

        // A press anywhere in a window, including on a button inside it,
        // brings the window to the front. It is done here, before the button
        // can mark the press handled and stop it reaching the window.
        for (Element* current = hit; current != nullptr; current = current->GetParent())
        {
            if (dynamic_cast<Window*>(current) != nullptr)
            {
                BringToFront(current);
            }
        }
    }

    Event event;
    event.Type = down ? EventType::PointerDown : EventType::PointerUp;
    event.Position = position;
    event.Button = button;
    Dispatch(hit, event);
    UpdateCursor();
    return true;
}

bool UiTree::HandleWheel(Vec2 position, float delta)
{
    if (m_Root == nullptr)
    {
        return false;
    }

    bool blocked = false;
    Element* hit = Pick(position, &blocked);
    if (hit == nullptr)
    {
        return blocked;
    }

    Event event;
    event.Type = EventType::Wheel;
    event.Position = position;
    event.WheelDelta = delta;
    Dispatch(hit, event);
    return event.Handled;
}

void UiTree::HandlePointerLeave()
{
    m_PointerInside = false;
    m_TooltipShown = false;
    m_TooltipTime = 0.0f;
    if (m_Captured == nullptr)
    {
        SetHovered(nullptr, m_Pointer);
    }
}

void UiTree::HandleFocusLost()
{
    // The release that would end this drag is going to another window, so it
    // is delivered here instead: a slider must not stay stuck to the pointer
    // after an alt-tab.
    if (m_Captured != nullptr)
    {
        Element* captured = m_Captured;
        m_Captured = nullptr;
        captured->m_Pressed = false;

        Event release;
        release.Type = EventType::PointerUp;
        release.Position = m_Pointer;
        Deliver(captured, release);
    }
    m_TooltipShown = false;
}

bool UiTree::OfferShortcut(Element* element, const Event& event)
{
    if (element == nullptr || !element->Visible)
    {
        return false;
    }
    // Front to back, the same order the pointer is offered things in.
    for (auto child = element->m_ChildViews.rbegin(); child != element->m_ChildViews.rend(); ++child)
    {
        if (OfferShortcut(*child, event))
        {
            return true;
        }
    }
    return element->OnShortcut(event);
}

bool UiTree::CloseTopPopupOnEscape()
{
    std::vector<Popup*> open;
    CollectOpenPopups(open);
    for (auto popup = open.rbegin(); popup != open.rend(); ++popup)
    {
        if ((*popup)->DismissOnEscape)
        {
            (*popup)->Close();
            return true;
        }
    }
    return false;
}

bool UiTree::MoveFocus(bool backward)
{
    // The scope: a modal popup keeps Tab inside itself, as a dialog should.
    Popup* modal = TopModal();

    std::vector<Element*> order;
    auto Gather = [&](Element* scope) {
        if (scope == nullptr)
        {
            return;
        }
        scope->ForEachDescendant([&](Element& candidate) {
            if (!candidate.Visible)
            {
                return true;
            }
            if (candidate.Focusable && candidate.Interactive && candidate.IsEnabled() && IsShown(&candidate))
            {
                order.push_back(&candidate);
            }
            return true;
        });
    };

    if (modal != nullptr)
    {
        Gather(modal);
    }
    else
    {
        Gather(m_Root.get());
        Gather(m_Overlay.get());
    }

    if (order.empty())
    {
        return false;
    }

    auto current = std::find(order.begin(), order.end(), m_Focused);
    size_t next = 0;
    if (current == order.end())
    {
        next = backward ? order.size() - 1 : 0;
    }
    else
    {
        const size_t index = static_cast<size_t>(current - order.begin());
        next = backward ? (index + order.size() - 1) % order.size() : (index + 1) % order.size();
    }

    RequestFocus(order[next], true);
    return true;
}

bool UiTree::HandleKey(Key key, bool down, bool shift, bool control, bool alt, bool repeat)
{
    // Typing hides a tooltip the way pressing does.
    m_TooltipShown = false;
    m_TooltipTime = 0.0f;

    Event event;
    event.Type = down ? EventType::KeyDown : EventType::KeyUp;
    event.KeyCode = key;
    event.Shift = shift;
    event.Control = control;
    event.Alt = alt;
    event.Repeat = repeat;

    if (m_Focused != nullptr)
    {
        Dispatch(m_Focused, event);
        if (event.Handled)
        {
            return true;
        }
    }

    if (!down)
    {
        return false;
    }

    // What nothing focused wanted: Escape closes the front popup, Tab moves
    // the keyboard on, and anything else is offered as a shortcut.
    if (key == Key::Escape && !repeat && CloseTopPopupOnEscape())
    {
        return true;
    }

    if (key == Key::Tab && !control && !alt)
    {
        return MoveFocus(shift);
    }

    if (!repeat)
    {
        if (OfferShortcut(m_Overlay.get(), event) || OfferShortcut(m_Root.get(), event))
        {
            return true;
        }
    }
    return false;
}

bool UiTree::HandleText(const std::string& text)
{
    if (m_Focused == nullptr || text.empty())
    {
        return false;
    }

    Event event;
    event.Type = EventType::Text;
    event.Text = text;
    Dispatch(m_Focused, event);
    return event.Handled;
}

void UiTree::RequestFocus(Element* element, bool fromKeyboard)
{
    // A disabled element cannot take the keyboard.
    if (element != nullptr && !element->IsEnabled())
    {
        return;
    }

    m_FocusVisible = element != nullptr && fromKeyboard;

    if (m_Focused == element)
    {
        // Focus stays, but what it wants may have changed: a number field
        // that starts taking typing while it already has the keyboard needs
        // the platform's text input turned on now.
        SetTextInputActive(m_App, m_Focused != nullptr && m_Focused->WantsText());
        return;
    }

    if (m_Focused != nullptr)
    {
        m_Focused->m_Focused = false;
        Event lost;
        lost.Type = EventType::FocusLost;
        m_Focused->OnEvent(lost);
    }

    m_Focused = element;

    if (m_Focused != nullptr)
    {
        m_Focused->m_Focused = true;
        Event gained;
        gained.Type = EventType::FocusGained;
        m_Focused->OnEvent(gained);
    }

    // The platform's text input follows focus, so a field that accepts typing
    // receives composed characters while it holds focus and nothing else does.
    SetTextInputActive(m_App, m_Focused != nullptr && m_Focused->WantsText());
}

void UiTree::Capture(Element* element)
{
    if (element == nullptr || element == m_Captured)
    {
        return;
    }
    if (m_Captured != nullptr)
    {
        m_Captured->m_Pressed = false;
    }
    m_Captured = element;
    element->m_Pressed = true;
}

void UiTree::Release(Element* element)
{
    if (element != nullptr && element == m_Captured)
    {
        m_Captured->m_Pressed = false;
        m_Captured = nullptr;
    }
}

void UiTree::UpdateCursor()
{
    CursorShape shape = CursorShape::Default;

    if (m_Captured != nullptr)
    {
        shape = m_Captured->Cursor;
    }
    else
    {
        for (Element* current = m_Hovered; current != nullptr; current = current->GetParent())
        {
            if (current->Cursor != CursorShape::Default)
            {
                shape = current->Cursor;
                break;
            }
        }
    }

    if (shape != m_Cursor)
    {
        m_Cursor = shape;
        SetCursorShape(m_App, shape);
    }
}

void UiTree::UpdateTooltip(float deltaSeconds)
{
    // The nearest element under the pointer, or above it, that has something
    // to say.
    Element* source = nullptr;
    if (m_PointerInside && m_Captured == nullptr)
    {
        for (Element* current = m_Hovered; current != nullptr; current = current->GetParent())
        {
            if (!current->Tooltip.empty())
            {
                source = current;
                break;
            }
        }
    }

    if (source != m_TooltipSource)
    {
        m_TooltipSource = source;
        m_TooltipShown = false;
        m_TooltipTime = 0.0f;
        m_TooltipAnchor = m_Pointer;
    }

    if (m_TooltipSource == nullptr)
    {
        m_TooltipShown = false;
        return;
    }

    m_TooltipTime += deltaSeconds;
    if (!m_TooltipShown && m_TooltipTime >= TooltipDelay)
    {
        m_TooltipShown = true;
        m_TooltipAnchor = m_Pointer;
    }
}

} // namespace detail
} // namespace opane
