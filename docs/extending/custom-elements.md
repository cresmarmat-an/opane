# Writing a custom element

To make an element of your own, derive from `opane::Element` and override the
methods you need. Your element takes part in layout, painting, input, focus,
and batching in exactly the same way as the built-in widgets, which are written
the same way.

```cpp
class RadialGauge : public opane::Element
{
public:
    float Value = 0.5f;

    RadialGauge()
    {
        Size = opane::Size2::FromOffset(150.0f, 150.0f);
        Focusable = true;
    }

    opane::Vec2 Measure(opane::Vec2 available) override
    {
        return Size.Resolve(available);
    }

    // A circular hit region, so the corners fall through to what is beneath.
    bool HitTest(opane::Vec2 point) const override
    {
        const opane::Rect bounds = GetBounds();
        const float dx = point.X - (bounds.X + bounds.Width * 0.5f);
        const float dy = point.Y - (bounds.Y + bounds.Height * 0.5f);
        const float radius = std::min(bounds.Width, bounds.Height) * 0.5f;
        return dx * dx + dy * dy <= radius * radius;
    }

    void Paint(opane::DrawList& drawList) override
    {
        const opane::Theme& theme = GetTheme();
        const opane::Rect bounds = GetBounds();
        const opane::Vec2 center{ bounds.X + bounds.Width * 0.5f, bounds.Y + bounds.Height * 0.5f };
        drawList.StrokeCircle(center, bounds.Width * 0.4f, 8.0f, theme.Surface);
        // ... draw the value with the theme's colours
    }

    void OnEvent(opane::Event& event) override
    {
        if (event.Type == opane::EventType::PointerDown)
        {
            event.Handled = true;   // stops the event bubbling to the parent
        }
    }

    void OnUpdate(float deltaSeconds) override { /* animation */ }
};

RadialGauge* gauge = root->Add<RadialGauge>();
```

## Methods you can override

| Method | Purpose |
|---|---|
| `Measure(available)` | The size the element wants, given the space its parent offers. The default resolves `Size`. |
| `Arrange(bounds)` | Receives the final rectangle. The default stores it. |
| `Paint(drawList)` | Draws the element. Children are painted afterwards by the tree. |
| `HitTest(point)` | Whether a point is inside the element. The default is its bounds. |
| `OnEvent(event)` | Pointer, wheel, key, text, and focus events. |
| `OnUpdate(deltaSeconds)` | Called every frame, for animation and polling. |
| `PlaceChild(child, index, content)` | Where a child goes, when `ChildLayout` is `Custom`. See [Layout](../interface/layout.md). |
| `GetContentBounds()` | Where children are laid out. The default is the bounds minus padding. |
| `OnShortcut(keyEvent)` | A key press nothing focused handled. Return true to consume it. |
| `OnChildRemoved(child)` | A child is about to be destroyed; forget any pointer to it. |
| `WantsText()` | Return true to receive typed text while focused. |
| `GetDefaultAppearance(theme)` | A look from the theme to use when the element has no `Appearance`. |
| `IsSelected()` | Whether the element counts as selected for its `Appearance`. |

## Useful calls inside an element

- `GetTheme()`, `GetBounds()`, `GetContentBounds()`
- `IsHovered()`, `IsPressed()`, `IsFocused()`, `IsFocusVisible()`, `IsEnabled()`
- `MeasureText(text)`, `MeasureText(text, font)`, `GetLineHeight()`
- `RequestFocus()`, `BringToFront()`
- `CapturePointer()`, `ReleasePointer()`, `HasPointerCapture()`
- `GetApp()`: the application the element belongs to, for loading an image,
  playing a sound, or animating a material. Valid once the element is in a
  tree.

## Events

An event goes first to the element under the pointer (or the focused element,
for keys and text), then bubbles up to each ancestor until one sets
`Handled`. Pressing an element captures the pointer for it, so it keeps
receiving `PointerMove` and the matching `PointerUp` even when the pointer
leaves its bounds. `CapturePointer` hands the capture to another element (a
window taking over a drag that started on its title, for example), and
`ReleasePointer` gives it up early.

The event types are listed in [Input](../application/input.md#events). Pointer
positions are in interface units, and in the element's own layout
coordinates when it or an ancestor has a `Transform`.

## Holding on to another element

An element that keeps a pointer to one it does not own, such as a popup it
opened in the overlay, should hold an `ElementRef`. It returns null once that
element is gone, however it was removed:

```cpp
class Picker : public opane::Element
{
    opane::ElementRef<opane::Popup> m_List;

    void Open()
    {
        opane::Popup* list = m_List.Get();
        if (list == nullptr)
        {
            list = GetApp().GetOverlay()->Add<opane::Popup>();
            m_List = list;
        }
        list->OpenBelow(*this);
    }

    ~Picker() override
    {
        if (opane::Popup* list = m_List.Get())   // null if it was destroyed first
        {
            list->GetParent()->Remove(list);
        }
    }
};
```

This matters most in destructors. Elements removed together are destroyed one
after another, in no particular order, so the second must not use the first.
Through an `ElementRef` it sees null instead of freed memory. The whole
interface is `Get()`, `->`, `if (ref)`, and `Reset()`.

## Callbacks and removal

A callback may replace itself while it runs (a button whose `OnClick` assigns
a new `OnClick`), and an element may remove itself or anything else from
inside its own handler, because removal waits until the end of the frame.

## Limitations

- Elements are C++ classes. There is no scripting language or markup for
  defining them.
- An element has one rectangle in the layout. Shapes that are not rectangles
  are handled by `HitTest`, but layout always uses the bounding rectangle.
