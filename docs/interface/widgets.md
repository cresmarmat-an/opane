# Built-in widgets

The widgets below are ordinary elements written against the same public API
you use for your own, so everything on this page is also something your own
elements can do. Their colours and sizes come from the
[theme](../styling/theming.md), and each can be restyled with an `Appearance`
or a [skin](../styling/skins-and-theme-files.md).

Menus, drop-downs, dialogs, and popups are on
[Popups, menus, and dialogs](popups-menus-dialogs.md); splitters, tab views,
and windows on [Containers](containers.md); lists and trees on
[Lists and trees](lists-and-trees.md); and the dock space on
[Docking](docking.md).

## Panel

A rectangle of the theme's surface colour, for grouping other elements.

```cpp
opane::Panel* panel = root->Add<opane::Panel>();
panel->BackgroundColor = opane::Color::FromBytes(20, 22, 28);  // transparent uses the theme
panel->CornerRadius = 12.0f;                                    // negative uses the theme
panel->DrawBorder = true;
panel->UseThemeSurface = true;    // false draws nothing unless BackgroundColor is set
```

## Label

One line of text.

```cpp
opane::Label* label = root->Add<opane::Label>();
label->Text = "Some text";
label->Align = opane::TextAlign::Center;   // Left (default), Center, or Right
label->Muted = true;                       // the theme's muted text colour
label->TextColor = opane::Color::FromBytes(255, 200, 80);   // transparent uses the theme
label->Font = app.LoadFont("fonts/Inter-Bold.ttf", 22.0f); // invalid uses the theme's font
```

A label measures to fit its text and centres it vertically in its bounds.

## Button

```cpp
opane::Button* button = root->Add<opane::Button>();
button->Text = "Confirm";
button->Accent = true;            // the theme's accent colour
button->OnClick      = [] { /* ... */ };
button->OnHoverStart = [] { /* ... */ };
button->OnHoverEnd   = [] { /* ... */ };
```

`OnClick` fires for a left click whose release lands inside the button, so
dragging off the button cancels the press, and right clicks do not click. A
focused button also responds to Space and Enter, once per press.

## Checkbox, toggle, and radio buttons

```cpp
opane::Checkbox* checkbox = root->Add<opane::Checkbox>();
checkbox->Text = "Enabled";
checkbox->Checked = true;
checkbox->OnChanged = [](bool checked) { /* ... */ };

opane::Toggle* sound = root->Add<opane::Toggle>();
sound->Text = "Sound";
sound->On = true;
sound->OnChanged = [](bool on) { /* ... */ };

// Radio buttons with the same Group under the same parent form one set.
for (const char* size : { "Small", "Medium", "Large" })
{
    opane::RadioButton* choice = root->Add<opane::RadioButton>();
    choice->Text = size;
    choice->Group = "size";
    choice->OnSelected = [size] { /* ... */ };
}
```

A checkbox and a toggle flip on Space. `RadioButton::Select()` checks one from
code and unchecks the rest of its set. All three report themselves as
selected while they are on, so an `Appearance` can style the "on" state with
its `Selected` look.

## Slider

```cpp
opane::Slider* slider = root->Add<opane::Slider>();
slider->Minimum = 0.0f;
slider->Maximum = 100.0f;
slider->Value = 50.0f;
slider->Step = 5.0f;              // what an arrow key moves; values snap to it. 0: a hundredth of the range
slider->OnChanged = [](float value) { /* ... */ };

slider->SetValue(75.0f);          // clamped, snapped, and reported if it changed
```

A slider can be dragged (the drag continues outside its bounds), stepped with
the arrow keys and Page Up and Page Down, and sent to its ends with Home and
End.

## Progress bar

```cpp
opane::ProgressBar* progress = root->Add<opane::ProgressBar>();
progress->Value = 0.4f;            // 0 to 1
progress->Text = "Loading {}%";    // {} becomes the percentage
progress->Indeterminate = false;   // true sweeps back and forth, for work of unknown length
```

The bar eases toward its value, so progress that jumps in large steps still
moves smoothly.

## Number field

A number that is dragged sideways to change, or typed exactly.

```cpp
opane::NumberField* width = root->Add<opane::NumberField>();
width->Prefix = "Width";
width->Suffix = "px";
width->Minimum = 0.0;
width->Maximum = 4096.0;
width->Step = 1.0;                 // per arrow press, and per unit dragged
width->Precision = 0;              // decimal places shown; the value keeps full precision
width->OnChanged = [](double value) { /* ... */ };

width->SetValue(1280.0);
width->Format(1280.0);             // the value as the field would show it
```

Holding Shift while dragging changes the value at a tenth of the speed.
Double-clicking the field, or pressing Enter, lets you type a value: Enter
keeps it, Escape discards it, and clicking elsewhere keeps it.

## Text input

A single line of editable text.

```cpp
opane::TextInput* field = root->Add<opane::TextInput>();
field->Placeholder = "Your name";
field->MaxLength = 32;             // in characters, not bytes; 0 is no limit
field->Masked = false;             // true draws dots, for a password
field->ReadOnly = false;
field->OnChanged   = [](const std::string& text) { /* ... */ };
field->OnSubmitted = [](const std::string& text) { /* Enter was pressed */ };

field->Text = "Ada";               // can be assigned at any time
field->SelectAll();
field->SetCaret(0);                // a byte offset into Text
field->Undo();
field->Redo();
```

Editing works as in any desktop program:

- Click to focus and place the caret. Drag, or hold Shift with the arrow keys,
  to select. Double-click selects a word.
- Home and End go to the ends of the line. Ctrl+Left and Ctrl+Right move by
  word; Ctrl+Backspace and Ctrl+Delete delete by word.
- Ctrl+A selects all. Ctrl+X, Ctrl+C, and Ctrl+V cut, copy, and paste. A masked
  field never copies its contents to the clipboard, and pasted line breaks
  and tabs become spaces.
- Ctrl+Z undoes, and Ctrl+Y or Ctrl+Shift+Z redoes. A burst of typing undoes
  as one step.

Typed text arrives already composed by the platform, so accented letters and
characters from an input method count as one character each. The field
scrolls sideways to keep the caret in view.

## Text area

Many lines of editable text, with everything a `TextInput` does:

```cpp
opane::TextArea* notes = root->Add<opane::TextArea>();
notes->Placeholder = "Notes";
notes->WordWrap = true;           // break long lines at spaces; false scrolls sideways
notes->TabSpaces = 4;             // spaces Tab inserts; 0 lets Tab move focus
notes->ReadOnly = false;
notes->OnChanged = [](const std::string& text) { /* ... */ };

notes->InsertText("Hello\n");     // replaces the selection, like typing, and can be undone
notes->GetSelectedText();
notes->CanUndo();
notes->CanRedo();
```

Enter starts a new line. The up and down arrows keep their horizontal place
across lines of different lengths, Page Up and Page Down move by a screenful,
Home and End go to the ends of the line on screen, and Ctrl+Home and Ctrl+End
to the ends of the text. Ctrl+Enter is left for your program, for example to
send a message.

## Scroll view

A view onto a column of children taller than the view.

```cpp
opane::ScrollView* list = root->Add<opane::ScrollView>();
list->ChildLayout = opane::LayoutMode::Vertical;
list->Spacing = 8.0f;
list->WheelStep = 60.0f;          // units per wheel notch
list->ShowBar = true;
for (const std::string& line : lines)
{
    list->Add<opane::Label>()->Text = line;
}

list->ScrollTo(0.0f);
list->ScrollBy(120.0f);
list->GetContentHeight();         // known after layout
list->GetMaximumOffset();
```

Children are laid out exactly as in a panel and then shifted by how far the
view is scrolled, so anything that works in a panel works in a scroll view.
The wheel scrolls it and its bar can be dragged. `Offset` is clamped to the
content.

## Image

Shows a texture.

```cpp
opane::Image* picture = root->Add<opane::Image>();
picture->Texture = app.LoadTexture("portrait.png");
picture->Fit = opane::ImageFit::Cover;       // Contain (default), Cover, Stretch, or Center
picture->Tint = opane::Color{ 1, 1, 1, 0.8f };
```

With no `Size`, an image lays out at its texture's size. With only one axis
given, the other keeps the image's proportions. For tiling and nine-slice
images, use a [fill](../styling/your-own-look.md).

## Viewport

Shows a world, or any texture another library rendered, inside the layout. See
[Hosting a world](../application/hosting-a-world.md).

```cpp
opane::Viewport* viewport = root->Add<opane::Viewport>();
viewport->SetWorld(world);
viewport->OnViewEvent = [](const opane::Event& viewEvent) { /* Position is 0..1 */ };
```

## Limitations

- `Label` draws one line. For wrapped text, draw it with
  `DrawTextWrapped` in a `Painter`, or use a read-only `TextArea`.
- `Slider` and `ProgressBar` are horizontal only.
- `ScrollView` scrolls vertically only.
- Buttons show text only. For an icon button, give a button a `Painter` or
  write a small custom element.
- There is no built-in colour picker, date or time picker, table or grid view,
  or rich text editor.
- `TextArea` has no syntax highlighting, search, or spell checking.
