# Docking

A dock space holds panels that the user can rearrange by dragging: tabbed
together, split beside one another, or torn off into floating windows. The
arrangement can be saved as text and restored the next time the program runs.
This is the layout style of most editors and tools.

```cpp
opane::DockSpace* dock = root->Add<opane::DockSpace>();
dock->Flex = 1.0f;

opane::DockPanel* scene = dock->AddPanel("Scene");
opane::DockPanel* tree  = dock->AddPanel("Hierarchy", opane::DockSide::Left, nullptr, 0.2f);
opane::DockPanel* props = dock->AddPanel("Inspector", opane::DockSide::Right, nullptr, 0.25f);
opane::DockPanel* log   = dock->AddPanel("Console", opane::DockSide::Bottom, scene, 0.3f);
opane::DockPanel* notes = dock->AddPanel("Notes", opane::DockSide::Center, log);  // a tab beside Console

scene->Add<opane::Viewport>()->SetWorld(world);
```

`AddPanel(title, side, relativeTo, share)` creates a panel and docks it:

- `side` is `Center` (a tab in the same group), `Left`, `Right`, `Top`, or
  `Bottom`.
- With no `relativeTo`, the side is relative to the whole dock space; with a
  panel, relative to that panel's group.
- `share` is the new panel's part of the split, from 0 to 1.

`AddPanel<T>` creates a panel of your own `DockPanel` subclass. A panel is
filled like any other element.

## What the user can do

- **Drag a tab.** Guides appear: a compass over the group under the pointer
  (the centre to add a tab, the four arms to split beside it) and a guide at
  each edge of the whole space. The shaded area shows where the panel will
  land.
- **Drop a tab away from every guide** to tear it off into a floating window.
  Drag the window by its title back onto a guide to dock it again.
- **Drag a tab along its own tab strip** to reorder it.
- **Drag a divider** to resize the groups on either side.
- **Close a panel** with its tab's close button or a middle click. A closed
  panel is hidden, not destroyed.

A panel is never recreated when it moves, so its contents (a half-typed field,
a scrolled list, a running world) are kept.

## Controlling panels from code

```cpp
dock->Dock(panel, opane::DockSide::Right, scene, 0.3f);   // from wherever it is now
dock->Float(panel, { 200.0f, 150.0f, 400.0f, 300.0f });
dock->ClosePanel(panel);
dock->Show(panel);            // reopens a closed panel where it last was
dock->Activate(panel);        // brings its tab, or its window, to the front

dock->IsDocked(panel);
dock->IsFloating(panel);
dock->IsOpen(panel);
dock->FindPanel("Console");
dock->GetPanels();
```

`DockPanel` members:

| Member | Meaning |
|---|---|
| `Title` | The tab's text. |
| `Id` | The name used in saved layouts. Defaults to the title; set it when the title can change. |
| `Closable` | Whether the tab has a close button. |
| `OnClosed` | Called when the user closes the panel. |

`DockSpace` settings:

| Member | Meaning |
|---|---|
| `AllowFloating` | When false, a tab dropped away from every guide goes back where it was. |
| `TabHeight` | The height of each group's tab strip. |
| `DividerThickness` | The width of the dividers between groups. |
| `MinimumPane` | The smallest a group can be dragged. |
| `OnLayoutChanged` | Called whenever the arrangement changes. |

## Saving the arrangement

```cpp
settings.Write("layout", dock->SaveLayout());

// The next time the program starts:
dock->LoadLayout(settings.Read("layout"));
```

The layout is plain, versioned text that names panels by their `Id`. Loading is
forgiving: panels the text names but the space does not have are skipped, and
panels the space has but the text does not mention are added as tabs to the
largest group. So a layout saved by an older version of your program still
loads. Text that is not a layout is refused, returns false, and changes
nothing.

## Limitations

- Floating panels are windows inside the application's window. They cannot be
  dragged outside it to become separate operating system windows.
- Panels move only within their own dock space; they cannot be dragged into
  another dock space.
- Panels cannot be pinned to an edge as auto-hiding side bars.
