# Layout

Each element's size and position are given as a **fraction of its parent plus
an offset**. That keeps a layout correct at any window size without doing any
arithmetic yourself. Offsets are in interface units (pixels at a scale of 1);
see [Interface scale](../application/interface-scale.md).

## Sizes

```cpp
opane::Size2::FromOffset(200.0f, 60.0f);   // 200 by 60 units
opane::Size2::FromScale(0.5f, 1.0f);       // half the parent's width, all of its height
opane::Size2::Fill();                      // all of the parent (the default)

// Half the parent's width minus ten units, and all of its height:
opane::Size2{ opane::Dim{ 0.5f, -10.0f }, opane::Dim{ 1.0f, 0.0f } };
```

A `Dim` is a `Scale` (the fraction of the parent) and an `Offset` (units
added to it). `Dim::FromScale` and `Dim::FromOffset` build one from either
part.

## Positions and anchors

`Position` works the same way, measured from the parent's content box.
`AnchorPoint` says which point of the element the position refers to: `{0, 0}`
is its top left, `{0.5, 0.5}` its centre, `{1, 1}` its bottom right.

```cpp
element->Position = opane::Position2::Center();
element->AnchorPoint = { 0.5f, 0.5f };

// The same thing in one call:
element->PlaceCentered();

// Pinned to a corner, 16 units in from both edges:
element->PlaceAtCorner(opane::Corner::TopRight, 16.0f);
```

## Stacks

`ChildLayout` decides how an element arranges its children:

| Mode | Children are |
|---|---|
| `Absolute` (default) | placed by their own `Position` and `Size`. |
| `Vertical` | stacked downward, each as wide as the content box. |
| `Horizontal` | stacked rightward, each as tall as the content box. |
| `Custom` | placed by the element itself; see below. |

In a stack, each child sets its size along the stack and is stretched across
it, so a column of buttons lines up without each button repeating its width.
`Padding` insets the children from the element's edges and `Spacing` separates
them:

```cpp
panel->ChildLayout = opane::LayoutMode::Vertical;
panel->Padding = 20.0f;
panel->Spacing = 12.0f;
```

Each child's size along the stack comes from its `Measure` method. For a plain
element or panel, that is its `Size` resolved against the parent, and since the
default `Size` fills the parent, give stacked panels a height (or a `Flex`).
Labels, buttons, and the other widgets measure to fit their content unless you
give them a size.

## Filling the space that is left

`Flex` gives a child in a stack a share of the space the other children did
not use. A sidebar and a view can share a row like this:

```cpp
root->ChildLayout = opane::LayoutMode::Horizontal;

opane::Panel* sidebar = root->Add<opane::Panel>();
sidebar->Size = opane::Size2::FromOffset(280.0f, 0.0f);

opane::Viewport* view = root->Add<opane::Viewport>();
view->Flex = 1.0f;   // everything the sidebar leaves
```

Two children with `Flex` 1 split the remainder evenly; `Flex` 2 and 1 split it
two to one. Spacing is taken out of the remainder first. `MinSize` sets a
minimum on either axis for any child, flexible or not:

```cpp
view->MinSize = { 320.0f, 200.0f };
```

## Layouts of your own

`LayoutMode::Custom` lets an element place each child itself, through its
`PlaceChild` method. The tree calls it once per visible child with the content
box and uses the rectangle it returns. A grid, for example:

```cpp
class Grid : public opane::Element
{
public:
    int Columns = 3;
    float CellHeight = 90.0f;

    Grid() { ChildLayout = opane::LayoutMode::Custom; }

    opane::Rect PlaceChild(opane::Element&, size_t index, const opane::Rect& content) override
    {
        const float width = content.Width / static_cast<float>(Columns);
        return opane::Rect{ content.X + width * static_cast<float>(index % Columns),
                            content.Y + CellHeight * static_cast<float>(index / Columns),
                            width, CellHeight };
    }
};
```

`index` is the child's position among all of the element's children, visible
or not. The built-in splitter, tab view, and dock space are written this way.

## Moving elements around the tree

```cpp
panel->MoveTo(otherParent);   // keeps its state, its children, and its focus
window->BringToFront();       // drawn over its siblings and given the pointer first
```

`MoveTo` happens at once, while removal is queued to the end of the frame.
`MoveTo` is refused, with a warning, when the new parent is inside the element
or belongs to another tree.

When a child is about to be destroyed, its parent's `OnChildRemoved` is called
first, so an element that keeps track of its children can forget it.

## When layout runs

Layout runs every frame, before painting: each element measures its children
and then arranges them, from the root down. A property you change takes effect
in the next layout, which is the same frame if you change it in your frame
function.

## Limitations

- Stacks always stretch children across the stack axis. To centre a narrower
  child, wrap it in an element with `Absolute` layout and centre it there.
- There is no built-in grid, wrapping (flow) layout, or table layout. Write
  them with `LayoutMode::Custom`, as above.
- Layout is not animated. Changing a size or position moves the element at
  once; use `Transform` to animate how it is drawn.
- There is no right-to-left mirroring of layouts.
