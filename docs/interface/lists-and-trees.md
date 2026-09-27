# Lists and trees

`ListView` shows a column of selectable rows, and `TreeView` a hierarchy of
rows that open and close. Both draw only the rows in view, so very long lists
stay fast.

## List view

```cpp
opane::ListView* list = root->Add<opane::ListView>();
list->Items = { "One", "Two", "Three" };
list->RowHeight = 26.0f;
list->MultiSelect = true;                                   // Ctrl and Shift clicks add to the selection
list->OnSelected    = [](int index) { /* ... */ };
list->OnActivated   = [](int index) { /* double-click or Enter */ };
list->OnContextMenu = [](int index, opane::Vec2 at) { /* right-click, for a context menu */ };

list->Selected;                      // the last selected row, or -1
list->GetSelection();                // every selected row
list->SetSelection({ 0, 2 });
list->IsSelected(1);
list->ScrollTo(42);                  // brings a row into view
list->RowAt(windowPoint);            // the row under a point, or -1
```

The up and down arrows, Page Up, Page Down, Home, and End move the selection,
and the wheel scrolls. With `MultiSelect`, Ctrl-click adds or removes one row
and Shift-click selects a range.

## Tree view

```cpp
opane::TreeView* tree = root->Add<opane::TreeView>();
tree->Items = {
    { "Scene", 1, { { "Camera", 2, {} }, { "Lights", 3, { { "Sun", 4, {} } } } }, true },
};
tree->RowHeight = 24.0f;
tree->Indent = 18.0f;
tree->OnSelected  = [](const opane::TreeView::Item& item) { /* ... */ };
tree->OnActivated = [](const opane::TreeView::Item& item) { /* ... */ };
tree->OnExpanded  = [](const opane::TreeView::Item& item, bool expanded) { /* ... */ };
tree->OnContextMenu = [](const opane::TreeView::Item& item, opane::Vec2 at) { /* ... */ };

tree->Select(4);
tree->Reveal(4);          // opens its ancestors and scrolls it into view
tree->ExpandAll(true);
tree->Find(3);            // the item with that Id, or null
```

Each `Item` has `Text`, an `Id`, its `Children`, and whether it is `Expanded`.
The items are plain data: rebuild them when what they describe changes, and
the selection and open state carry over by `Id`. Give every item its own
non-zero `Id`; `Selected` is 0 when nothing is selected.

The arrow keys move through the tree. Right opens a branch, then moves into
it; Left closes a branch, then moves to its parent. A double-click opens a
branch or activates a leaf.

## Limitations

- Rows show a single line of text. There are no icons, checkboxes, or columns
  inside rows, and rows cannot contain other elements. For richer rows, use a
  `ScrollView` with elements of your own.
- All rows in a list or tree have the same height.
- Neither view supports dragging rows to reorder them or drag and drop between
  views.
- A tree view selects one item at a time.
- There is no built-in sorting or filtering; change `Items` yourself.
