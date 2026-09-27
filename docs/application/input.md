# Input

There are two ways to handle input. You can read the current state of the
keyboard and mouse at any point in a frame, which suits games and per-frame
logic. Or you can let elements receive events, which is how widgets work; see
[Custom elements](../extending/custom-elements.md).

## Reading the state

```cpp
const opane::Input& input = app.GetInput();

input.IsKeyDown(opane::Key::W);          // true while the key is held
input.WasKeyPressed(opane::Key::Space);  // true only in the frame it went down
input.WasKeyReleased(opane::Key::Space); // true only in the frame it came up

input.IsMouseButtonDown(opane::MouseButton::Left);
input.WasMouseButtonPressed(opane::MouseButton::Right);
input.WasMouseButtonReleased(opane::MouseButton::Middle);

input.GetMousePosition();  // in interface units, from the window's top left
input.GetMouseDelta();     // how far the mouse moved this frame
input.GetScrollDelta();    // wheel movement this frame
```

`Key` covers the letters, the digits `Num0` to `Num9`, the arrows, `F1` to
`F12`, `Escape`, `Space`, `Enter`, `Tab`, `Backspace`, `Delete`, `Insert`,
`Home`, `End`, `PageUp`, `PageDown`, the punctuation keys, and the left and
right Shift, Control, and Alt keys. Keys are identified by their position on a
US keyboard layout. `MouseButton` is `Left`, `Middle`, or `Right`.

Mouse positions are in interface units, the same space elements are laid out
in, so they need no conversion at any [interface scale](interface-scale.md).

## Events

Elements receive input as `Event`s:

| Type | When |
|---|---|
| `PointerEnter`, `PointerLeave`, `PointerMove` | The pointer moves over or off the element. |
| `PointerDown`, `PointerUp` | A mouse button is pressed or released. |
| `Wheel` | The wheel turns; `WheelDelta` says how far. |
| `KeyDown`, `KeyUp` | A key is pressed or released while the element has focus. |
| `Text` | Characters were typed, already composed, as UTF-8 in `Text`. |
| `FocusGained`, `FocusLost` | The element gains or loses the keyboard. |

Key and text events carry `Shift`, `Control`, and `Alt`. `Repeat` is true for
key presses repeated because the key is held; editing usually acts on them,
while shortcuts usually ignore them.

Typed text arrives as `Text` events, not key presses, so accented letters and
characters entered through an input method work. The platform's text input is
turned on only while an element that wants text has focus; see
[Keyboard focus](../interface/keyboard-focus.md).

## When the window loses focus

When the pointer leaves the window, hover ends. When the window loses the
keyboard in the middle of a drag, the drag is finished with a release (the
release it was waiting for went to another window), and keys held at that
moment are released.

## Limitations

- Keyboard and mouse only. There is no gamepad or joystick input, and touch
  and pen input arrive only as the mouse events the platform emulates for
  them.
- Only the left, middle, and right mouse buttons are reported. The side
  buttons (back and forward) are ignored.
- The wheel is reported on one axis; horizontal scrolling is not supported.
- Keys are reported by position, so a shortcut such as `Ctrl+Z` is on the same
  physical key on every layout. There is no way to ask for the character a key
  produces on the current layout, except through `Text` events.
