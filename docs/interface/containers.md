# Containers

Three built-in elements divide space among other elements: a splitter, a tab
view, and a window that can be moved and resized.

## Splitter

Two panes with a divider between them that can be dragged. The splitter's
first two children are the panes.

```cpp
opane::Splitter* split = root->Add<opane::Splitter>();
split->Stacked = false;          // side by side; true puts one above the other
split->Ratio = 0.3f;             // the first pane's share, 0 to 1
split->MinimumPane = 80.0f;      // neither pane is dragged smaller than this
split->DividerThickness = 6.0f;
split->OnRatioChanged = [](float ratio) { /* save it, for example */ };

opane::Panel* left = split->Add<opane::Panel>();
opane::Panel* right = split->Add<opane::Panel>();
```

The divider shows a resize pointer and can be dragged. `GetDividerBounds()`
returns where it is.

## Tab view

Pages behind a row of tabs, one visible at a time.

```cpp
opane::TabView* tabs = root->Add<opane::TabView>();
tabs->Closable = true;           // a close button on each tab
tabs->TabHeight = 30.0f;

opane::Element* general = tabs->AddPage("General");
general->ChildLayout = opane::LayoutMode::Vertical;
general->Add<opane::Checkbox>()->Text = "Start with the system";

MyEditor* editor = tabs->AddPageOf<MyEditor>("Untitled");   // any element can be a page

tabs->OnActiveChanged = [](int index) { /* ... */ };
tabs->OnCloseRequested = [](int index) { return true; };    // false keeps the page
```

Pages can be managed from code:

```cpp
tabs->Activate(1);
tabs->Active;                    // the index of the page shown
tabs->GetPageCount();
tabs->GetPage(0);
tabs->IndexOf(editor);
tabs->SetTitle(editor, "notes.txt");
tabs->GetTitle(0);
tabs->RemovePage(0);
tabs->GetTabBounds(0);
```

A tab closes from its close button or with a middle click. Without an
`OnCloseRequested`, a closed page is removed. Ctrl+Tab and Ctrl+Shift+Tab move
between tabs while the keyboard is anywhere inside the pages. Tabs are as wide
as their titles, and are all narrowed together when they do not fit.

## Window

A frame with a title bar that can be moved, resized, and closed. Children go in
the body below the title. Windows usually live in the overlay so they float
above everything.

```cpp
opane::Window* window = app.GetOverlay()->Add<opane::Window>();
window->Title = "Colour";
window->SetFrame({ 100.0f, 100.0f, 320.0f, 240.0f });   // in the parent's space
window->ChildLayout = opane::LayoutMode::Vertical;
window->Closable = true;
window->Movable = true;
window->Resizable = true;
window->MinimumSize = { 160.0f, 100.0f };
window->OnCloseRequested = [] { return true; };   // false keeps it open
window->OnClosed = [] { /* ... */ };

window->GetFrame();
```

A window moves by its title and resizes from its sides and lower corners.
Part of its title always stays inside the parent, so it cannot be lost off
screen. A press anywhere in a window brings it to the front of its siblings.

## Limitations

- A splitter holds exactly two panes. Nest splitters for more.
- Tabs cannot be reordered by dragging in a `TabView`. The
  [dock space](docking.md) supports dragging tabs between groups.
- When there are many tabs, they are narrowed to fit; the tab strip does not
  scroll.
- A `Window` lives inside the application's window. It cannot be dragged
  outside it or become a separate operating system window.
