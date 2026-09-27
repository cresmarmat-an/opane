// The element tree: layout, painting, and input routing.
// Not installed and not part of the public API.

#pragma once

#include "Internal.h"

#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>

namespace opane::detail
{

class UiTree
{
public:
    void Initialize(const Theme& theme, FontStore* fonts, RenderTargetStore* renderTargets,
                    AppState* app);
    void Shutdown();

    FontStore* GetFonts() const { return m_Fonts; }
    AppState* GetApp() const { return m_App; }

    Element* GetRoot() { return m_Root.get(); }
    Element* GetOverlay() { return m_Overlay.get(); }

    const Theme& GetTheme() const { return m_Theme; }
    void SetTheme(const Theme& theme) { m_Theme = theme; }

    void Update(float deltaSeconds, Vec2 viewportSize);
    void Paint(DrawList& drawList);

    // Returns true when the interface consumed the event, so a world viewport
    // can ignore input that belongs to a button on top of it.
    bool HandlePointerMove(Vec2 position);
    bool HandlePointerButton(Vec2 position, MouseButton button, bool down);
    bool HandleWheel(Vec2 position, float delta);
    bool HandleKey(Key key, bool down, bool shift, bool control, bool alt, bool repeat);
    bool HandleText(const std::string& text);

    // The pointer left the window, or the window lost the keyboard. Hover
    // ends; a drag in progress is finished rather than left hanging, because
    // the release it is waiting for may never arrive.
    void HandlePointerLeave();
    void HandleFocusLost();

    void RequestFocus(Element* element, bool fromKeyboard = false);
    Element* GetFocused() const { return m_Focused; }
    bool IsFocusVisible(const Element* element) const { return element == m_Focused && m_FocusVisible; }

    void Capture(Element* element);
    void Release(Element* element);
    Element* GetCaptured() const { return m_Captured; }

    bool Reparent(Element* element, Element* newParent);
    void BringToFront(Element* element);
    bool Contains(const Element* element) const;

    void NotifyElementAdopted(Element* element);
    void QueueRemoval(Element* parent, Element* child);

private:
    // Arranges an element into bounds already decided by its parent, then lays
    // out its own children according to its own ChildLayout. Splitting "decide
    // the bounds" from "lay out the children" is what lets stacks nest.
    void LayoutInto(Element* element, const Rect& bounds, size_t depth);
    void PaintElement(Element* element, DrawList& drawList);

    // PaintElement's work once the element's transform and opacity are on the
    // draw list.
    void PaintTransformed(Element* element, DrawList& drawList);

    // The element's own painting and its children's, without the render-target
    // redirection. Split out so a render-target element can paint the same
    // content into its own list.
    void PaintContents(Element* element, DrawList& drawList);
    void PaintOverlay(DrawList& drawList);
    void PaintTooltip(DrawList& drawList);

    void UpdateElement(Element* element, float deltaSeconds, size_t depth);
    Element* PickDeepest(Element* element, Vec2 point);

    // Where a window position falls in an element's own layout, through every
    // transform above it and its own.
    Vec2 LocalPoint(const Element* element, Vec2 windowPoint) const;

    // Hands a pointer event to one element, with its position moved into
    // that element's layout and put back afterwards.
    void Deliver(Element* element, Event& event);

    // Works out which of its Appearance's states an element is in and eases
    // its look toward that one.
    void UpdateLook(Element* element, float deltaSeconds);

    // Where the pointer lands, overlay first. Null when nothing is there, or
    // when a modal popup is open and the point is beneath it.
    Element* Pick(Vec2 point, bool* blocked = nullptr);

public:
    // What pressing at this point does to a frameless window: the Region of
    // the element there, or of the nearest element above it that has one. A
    // control (anything focusable) stays a control inside a drag region, so a
    // title bar's buttons still work.
    WindowRegion RegionAt(Vec2 point);

private:

    void Dispatch(Element* element, Event& event);
    void ApplyPendingRemovals();
    void SetHovered(Element* element, Vec2 position);

    // Popups open in the overlay, front last.
    void CollectOpenPopups(std::vector<Popup*>& out) const;
    Popup* TopModal() const;

    // Closes every open popup a press at this element would dismiss. True
    // when any closed, in which case the press is spent.
    bool DismissPopupsFor(Element* hit);

    bool CloseTopPopupOnEscape();
    bool MoveFocus(bool backward);
    bool OfferShortcut(Element* element, const Event& event);
    void UpdateCursor();
    void UpdateTooltip(float deltaSeconds);

    // Lets go of every pointer the tree holds into a subtree about to be
    // destroyed.
    void ForgetSubtree(Element* element, std::unordered_set<Element*>& doomed);

    std::vector<Element*>& ScratchChildren(size_t depth);
    std::vector<Vec2>& ScratchSizes(size_t depth);

    std::unique_ptr<Element> m_Root;
    std::unique_ptr<Element> m_Overlay;
    Theme m_Theme;
    FontStore* m_Fonts = nullptr;
    RenderTargetStore* m_RenderTargets = nullptr;
    AppState* m_App = nullptr;
    Vec2 m_Viewport;

    Element* m_Hovered = nullptr;
    Element* m_Focused = nullptr;
    Element* m_Captured = nullptr;
    bool m_FocusVisible = false;

    // Set when a press was spent closing popups, so its release is spent too:
    // a button clicks on release, and the click that dismissed a menu must not
    // click what was beneath it.
    bool m_SwallowRelease = false;
    Vec2 m_Pointer;
    bool m_PointerInside = false;

    CursorShape m_Cursor = CursorShape::Default;

    Element* m_TooltipSource = nullptr;
    float m_TooltipTime = 0.0f;
    bool m_TooltipShown = false;
    Vec2 m_TooltipAnchor;

    struct PendingRemoval
    {
        Element* Parent = nullptr;
        Element* Child = nullptr;
    };
    std::vector<PendingRemoval> m_PendingRemovals;

    // One reusable buffer per depth, so walking the tree allocates nothing
    // once it has reached its deepest. Deques, because growing one at the end
    // keeps references to earlier buffers valid; a vector would move them
    // while an outer level still used one.
    std::deque<std::vector<Element*>> m_ChildScratch;
    std::deque<std::vector<Vec2>> m_SizeScratch;
};

} // namespace opane::detail
