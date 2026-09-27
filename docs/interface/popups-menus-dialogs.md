# Popups, menus, and dialogs

Popups, menus, drop-down lists, and dialogs float above the interface in the
**overlay**, `app.GetOverlay()`. The overlay is laid out over the whole window,
painted after everything else, and gets the pointer first. Wherever nothing in
it is, the pointer passes through to the interface below.

## Popups

A `Popup` is a floating element that closes by itself when the user clicks
elsewhere or presses Escape. Fill it like any other element:

```cpp
opane::Popup* popup = app.GetOverlay()->Add<opane::Popup>();
popup->Size = opane::Size2::FromOffset(260.0f, 120.0f);
popup->ChildLayout = opane::LayoutMode::Vertical;
popup->Add<opane::Label>()->Text = "Anything can go in here";

popup->OpenBelow(*button);
popup->IsOpen();
popup->Close();
popup->OnClosed = [] { /* ... */ };
```

Ways to open it:

| Call | Opens |
|---|---|
| `OpenAt(point)` | at a window position, down and to the right |
| `OpenBelow(element)` | below an element, lined up with its left edge |
| `OpenBeside(element)` | to the right of an element, as a submenu does |
| `OpenCentered()` | in the middle of the window |
| `OpenBelowRect(rect)`, `OpenBesideRect(rect)` | the same, next to a rectangle |

A popup always stays on screen. Opened near an edge, it moves inward; opened
below something near the bottom of the window, it opens above it instead.

Closing:

- A press outside the popup closes it and is not passed on, so the click that
  closes a menu does not also press whatever was under it.
  `DismissOnOutsideClick = false` turns this off.
- Escape closes it. `DismissOnEscape = false` turns this off.
- When it closes, the keyboard returns to the element that had it, if that
  element still exists.

`Modal = true` dims everything beneath the popup (with `ModalShade`) and blocks
input to it until the popup closes. `Owner` links a popup to the popup that
opened it: a press inside the child does not close the owner, and closing the
owner closes the child.

## Menu bars

```cpp
opane::MenuBar* bar = root->Add<opane::MenuBar>();
bar->Menus = {
    { "File", {
        opane::MenuItem::Action("Open...", [] { /* ... */ }, "Ctrl+O"),
        opane::MenuItem::Action("Save", [] { /* ... */ }, "Ctrl+S"),
        opane::MenuItem::Divider(),
        opane::MenuItem::Submenu("Recent", { /* more items */ }),
    } },
    { "View", {
        opane::MenuItem::Check("Show grid", true, [] { /* ... */ }),
    } },
};
```

Clicking a title opens its menu, and while one is open, moving the pointer
along the bar opens the others. A submenu opens when the pointer rests on its
row. In an open menu, the up and down arrows move the highlight, Right opens a
submenu, Left and Escape close one, and Enter chooses. A checkable item keeps
its state between openings.

An item's shortcut is shown on the right and works anywhere in the window while
the bar is in the tree, not only while the menu is open. A focused text field
still gets its own shortcuts (such as Ctrl+C) first; see
[Keyboard focus](keyboard-focus.md).

`MenuItem` fields, for building items by hand:

| Field | Meaning |
|---|---|
| `Text` | The row's text. |
| `OnSelected` | Called when the item is chosen. |
| `Keys` | A `Shortcut`: key plus Control, Shift, and Alt. |
| `Enabled` | A disabled item is drawn dimmed and cannot be chosen. |
| `Checkable`, `Checked` | A check mark that flips when chosen. |
| `Separator` | A dividing line instead of an item. |
| `Children` | Makes the item a submenu. |

`Shortcut::Parse("Ctrl+Shift+Z")` reads the usual spellings, case-insensitively,
and returns an invalid shortcut for anything it does not understand.
`Shortcut::ToString()` writes one back out.

## Context menus

```cpp
list->OnContextMenu = [&](int row, opane::Vec2 at) {
    app.ShowMenu({
        opane::MenuItem::Action("Rename", [row] { /* ... */ }),
        opane::MenuItem::Action("Delete", [row] { /* ... */ }),
    }, at);
};
```

Only one context menu is open at a time; showing another replaces it. An
item's callback runs after the menu has closed, so it can open a dialog or
another menu.

## Drop-down lists

```cpp
opane::Dropdown* quality = root->Add<opane::Dropdown>();
quality->Options = { "Low", "Medium", "High", "Ultra" };
quality->Selected = 2;             // -1 shows the placeholder
quality->Placeholder = "Choose...";
quality->VisibleRows = 8;          // more options than this scroll
quality->OnChanged = [](int index) { /* ... */ };

quality->Select(0);
quality->Open();
quality->IsListOpen();
```

A drop-down opens on a click, Enter, Space, or Alt+Down. While it is closed,
the up and down arrows change the selection directly.

## Dialogs

```cpp
app.ShowDialog("Quit?", "Unsaved changes will be lost.", { "Quit", "Cancel" },
               [&](int button) {
                   if (button == 0) { app.Close(); }
               });
```

The callback receives the index of the button pressed, or -1 when the dialog
was dismissed with Escape. Enter presses the default button, the arrow keys
and Tab move between buttons, and nothing beneath the dialog can be used until
it is answered.

A `Dialog` can also be created and kept like any popup, to set more options:

```cpp
opane::Dialog* dialog = app.GetOverlay()->Add<opane::Dialog>();
dialog->Title = "Replace file?";
dialog->Message = "A file with that name already exists.";
dialog->Buttons = { "Replace", "Keep both", "Cancel" };
dialog->DefaultButton = 0;   // what Enter presses
dialog->CancelButton = 2;    // what Escape means; -1 reports "dismissed"
dialog->OnResult = [](int button) { /* ... */ };
dialog->OpenCentered();
```

## Limitations

- Menus show text, check marks, and shortcuts only; there are no icons in menu
  items.
- There are no Alt-key mnemonics (underlined letters) in menu bars.
- Menus and dialogs are drawn by opane inside the window. There are no native
  operating system menus, and no native file, folder, colour, or font dialogs.
- A dialog shows a title, plain text, and a row of buttons. For anything more,
  build a modal `Popup` with your own contents.
