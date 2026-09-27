# The element tree

An opane interface is a tree of elements. `app.GetRoot()` is an element that
fills the window; everything you add hangs below it. Each element lays out its
children, draws itself, and receives input. The built-in widgets are elements,
and so are the ones you write yourself.

## Adding and removing elements

`Add<T>()` creates a child of any element type, adds it to the tree, and returns
a pointer to it. The tree owns what it creates, so you never `delete` an
element.

```cpp
opane::Element* root = app.GetRoot();

opane::Panel* panel = root->Add<opane::Panel>();
panel->Size = opane::Size2::FromOffset(320.0f, 200.0f);
panel->PlaceCentered();

opane::Label* label = panel->Add<opane::Label>();
label->Text = "Inside the panel";
```

Arguments to `Add<T>` are passed to `T`'s constructor, so elements of your own
can take constructor parameters.

Removing is queued and applied at the end of the frame, so an element can
safely remove itself, or anything else, from inside its own event handler:

```cpp
parent->Remove(child);
parent->RemoveAllChildren();
```

Removing an element also removes everything inside it. If the element or
anything inside it had the keyboard, the pointer, or a tooltip, those are let
go first.

## Holding on to elements

An element pointer stays valid until the element is removed. When you keep a
pointer to an element you did not create, or one that something else may
remove, hold an `ElementRef` instead. It returns null once the element is gone:

```cpp
opane::ElementRef<opane::Popup> menu = app.GetOverlay()->Add<opane::Popup>();

if (menu)                   // false once the popup has been removed
{
    menu->Close();
}
```

## Common properties

Every element has these:

```cpp
element->Visible = true;         // hidden elements skip layout, painting, and input
element->Interactive = true;     // false lets pointer events pass through to what is below
element->Enabled = true;         // false ignores input and draws the element dimmed
element->ClipChildren = false;   // true cuts children off at this element's edges
element->Padding = 12.0f;        // space between the element's edge and its children
element->Spacing = 8.0f;         // space between children in a stack
element->Name = "Sidebar";       // for your own use, such as debugging
```

Layout properties (`Size`, `Position`, `AnchorPoint`, `ChildLayout`, `Flex`,
`MinSize`) are covered in [Layout](layout.md), and appearance properties
(`Appearance`, `Transform`, `Opacity`, `StyleName`, `Painter`) in
[Designing your own look](../styling/your-own-look.md).

## Reading state

```cpp
element->GetBounds();          // the final rectangle, in interface units
element->GetContentBounds();   // where children are laid out (bounds minus padding)
element->GetParent();
element->GetChildren();
element->IsHovered();          // the pointer is over it or over a child
element->IsPressed();
element->IsFocused();          // it has the keyboard
element->IsEnabled();          // it and every ancestor are enabled
element->IsInside(ancestor);
element->GetTheme();
```

`ForEachDescendant` visits an element and everything below it, depth first,
until the callback returns false:

```cpp
root->ForEachDescendant([](opane::Element& element) {
    element.Enabled = false;
    return true;   // keep going
});
```

## Per-frame updates

An element's `OnUpdate(float deltaSeconds)` is called once per frame for every
visible element. Override it in your own elements for animation or polling;
see [Custom elements](../extending/custom-elements.md).

## The overlay

`app.GetOverlay()` is a second tree root, laid out over the whole window,
painted after everything else, and offered the pointer first. Popups, menus,
dialogs, tooltips, and floating windows live there. Wherever nothing in the
overlay is, the pointer passes through to the interface below.

## Limitations

- Interfaces are built in C++ code. There is no markup or layout file format;
  theme files style an interface but do not create elements.
- There is no lookup by name. Keep the pointers `Add` returns, or search with
  `ForEachDescendant`.
- There is no data binding. When your data changes, update the elements
  yourself.
- The tree is not thread-safe. Change it only from the thread that runs the
  frame loop.
