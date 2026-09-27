# Pointer shapes and tooltips

Any element can set the pointer's shape while the pointer is over it, and a
tooltip that appears when the pointer rests on it.

```cpp
button->Tooltip = "Saves the scene";
divider->Cursor = opane::CursorShape::ResizeHorizontal;
```

## Tooltips

A tooltip appears beside the pointer after it has rested on the element for
about half a second. It stays inside the window, wraps long text, and
disappears when a button or key is pressed or the pointer moves away. An
element without a tooltip shows its nearest ancestor's, so a tooltip on a card
also covers everything inside it. The tooltip's look comes from the theme's
`Tooltip` style; see [Skins and theme files](../styling/skins-and-theme-files.md).

## Pointer shapes

| `CursorShape` | Typical use |
|---|---|
| `Default` | The normal arrow. |
| `Pointer` | A hand, for something that acts when clicked. |
| `Text` | An I-beam, for somewhere to type. |
| `ResizeHorizontal` | A left-right arrow, for a vertical divider. |
| `ResizeVertical` | An up-down arrow, for a horizontal divider. |
| `ResizeDiagonal` | A diagonal arrow, for a window's corner. |
| `Move` | For something being dragged around. |
| `NotAllowed` | For something that cannot be used right now. |

The shape follows the element under the pointer, or the element that is
capturing a drag. An element can change its `Cursor` from `OnEvent` while the
pointer is over it, and the change shows at once. The dock space uses this to
show the resize arrow only over its dividers.

## Limitations

- Tooltips show plain text only. For richer content, open a
  [popup](popups-menus-dialogs.md) yourself.
- The tooltip delay cannot be changed.
- Only the shapes above are available. Custom cursor images are not
  supported.
