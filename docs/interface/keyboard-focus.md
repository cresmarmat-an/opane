# Keyboard focus

One element at a time has the keyboard. Key presses and typed text go to it
first.

## Moving focus

Tab moves focus to the next focusable element, in the order the elements
appear in the tree; Shift+Tab moves back. The built-in controls are
focusable. Set `Focusable` on an element of your own that reacts to keys:

```cpp
myElement->Focusable = true;
button->Focusable = false;      // skipped by Tab, and a click does not focus it

element->RequestFocus();
app.GetFocusedElement();        // the element with the keyboard, or null
element->IsFocused();
```

A click gives focus to the nearest focusable element under the pointer, or
removes focus when there is none. So clicking empty space stops typing in a
text field.

## Focus rings

`IsFocusVisible()` is true when an element has focus and got it from the
keyboard rather than a click. The built-in controls draw a focus ring only
then, and your own elements can do the same.

## Where key presses go

1. The focused element's `OnEvent` receives the key.
2. If it does not set `Handled`, the event bubbles up to its parent, and so on
   to the root.
3. If nothing handled it, every element's `OnShortcut` is asked, front to back,
   until one returns true.

The last step is how a [menu bar](popups-menus-dialogs.md)'s shortcuts work
wherever focus is, while a focused text field still gets Ctrl+C before any
menu does. Escape closes the frontmost popup.

## Text input

An element that returns true from `WantsText()` receives typed characters as
`EventType::Text` events while it has focus. The platform's text input (and the
on-screen keyboard, on devices that have one) is turned on only while such an
element is focused. `TextInput`, `TextArea`, and `NumberField` (while editing)
do this.

## Limitations

- Tab order is always tree order. There is no way to set a custom order other
  than rearranging the tree.
- Arrow keys move within a control (a list, a menu, a slider) but do not move
  focus between controls.
- There is no screen reader or other accessibility support.
