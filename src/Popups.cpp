// Popups and what is built from them: menus, the menu bar, drop-downs, and
// dialogs.
//
// Written against the public API alone. A popup is an element in the overlay
// that places itself on screen, closes when it should, and hands the keyboard
// back to whoever had it.

#include <opane/opane.h>

#include "WidgetKit.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace opane
{
namespace
{

constexpr float EdgeMargin = 4.0f;

Rect ScreenOf(const Element& popup)
{
    if (const Element* parent = popup.GetParent())
    {
        return parent->GetBounds();
    }
    return popup.GetBounds();
}

// Keeps a rectangle inside another, moving it rather than shrinking it where
// it fits, and shrinking it only where it cannot.
Rect KeepInside(Rect rect, const Rect& screen)
{
    rect.Width = std::min(rect.Width, std::max(0.0f, screen.Width - EdgeMargin * 2.0f));
    rect.Height = std::min(rect.Height, std::max(0.0f, screen.Height - EdgeMargin * 2.0f));
    rect.X = std::clamp(rect.X, screen.X + EdgeMargin, screen.X + screen.Width - EdgeMargin - rect.Width);
    rect.Y = std::clamp(rect.Y, screen.Y + EdgeMargin, screen.Y + screen.Height - EdgeMargin - rect.Height);
    return rect;
}

} // namespace

// ---------------------------------------------------------------------------
// Popup
// ---------------------------------------------------------------------------

Popup::Popup()
{
    Visible = false;
    Size = Size2::FromOffset(240.0f, 160.0f);
    Padding = 8.0f;
}

void Popup::OnOpened()
{
}

void Popup::OpenAt(Vec2 windowPosition)
{
    m_Placement = Placement::AtPoint;
    m_Anchor = Rect{ windowPosition.X, windowPosition.Y, 0.0f, 0.0f };

    App app = GetApp();
    m_PreviousFocus = app.IsValid() ? app.GetFocusedElement() : nullptr;
    Visible = true;
    BringToFront();
    OnOpened();
    RequestFocus();
}

void Popup::OpenBelowRect(const Rect& anchor)
{
    OpenAt(Vec2{ anchor.X, anchor.Y + anchor.Height });
    m_Placement = Placement::Below;
    m_Anchor = anchor;
}

void Popup::OpenBesideRect(const Rect& anchor)
{
    OpenAt(Vec2{ anchor.X + anchor.Width, anchor.Y });
    m_Placement = Placement::Beside;
    m_Anchor = anchor;
}

void Popup::OpenBelow(const Element& anchor)
{
    OpenBelowRect(anchor.GetBounds());
}

void Popup::OpenBeside(const Element& anchor)
{
    OpenBesideRect(anchor.GetBounds());
}

void Popup::OpenCentered()
{
    OpenAt(Vec2{});
    m_Placement = Placement::Centered;
}

void Popup::Close()
{
    if (!Visible)
    {
        return;
    }
    Visible = false;

    App app = GetApp();

    // Whatever this popup opened goes with it: a menu's submenu, a picker's
    // drop-down.
    if (Element* parent = GetParent())
    {
        for (Element* sibling : parent->GetChildren())
        {
            if (auto* owned = dynamic_cast<Popup*>(sibling); owned != nullptr && owned->Owner == this)
            {
                owned->Close();
            }
        }
    }

    // Focus goes back where it was, but only if that element still exists and
    // nothing else has taken focus since.
    if (app.IsValid())
    {
        Element* focused = app.GetFocusedElement();
        const bool focusWasHere = focused == nullptr || focused->IsInside(this);
        Element* previous = m_PreviousFocus.Get();
        if (focusWasHere && previous != nullptr && app.ContainsElement(previous))
        {
            previous->RequestFocus();
        }
    }
    m_PreviousFocus.Reset();

    if (OnClosed)
    {
        kit::Invoke(OnClosed);
    }
}

void Popup::Arrange(const Rect& bounds)
{
    const Rect screen = ScreenOf(*this);
    Rect placed{ bounds.X, bounds.Y, bounds.Width, bounds.Height };

    switch (m_Placement)
    {
        case Placement::AtPoint:
        {
            // Opens down and to the right of the point; where there is no
            // room, it opens the other way rather than being pushed over the
            // point it was opened at.
            placed.X = m_Anchor.X;
            placed.Y = m_Anchor.Y;
            if (placed.X + placed.Width > screen.X + screen.Width - EdgeMargin && m_Anchor.X - placed.Width >= screen.X)
            {
                placed.X = m_Anchor.X - placed.Width;
            }
            if (placed.Y + placed.Height > screen.Y + screen.Height - EdgeMargin &&
                m_Anchor.Y - placed.Height >= screen.Y)
            {
                placed.Y = m_Anchor.Y - placed.Height;
            }
            break;
        }

        case Placement::Below:
        {
            placed.X = m_Anchor.X;
            placed.Y = m_Anchor.Y + m_Anchor.Height;
            const bool roomBelow = placed.Y + placed.Height <= screen.Y + screen.Height - EdgeMargin;
            const bool roomAbove = m_Anchor.Y - placed.Height >= screen.Y + EdgeMargin;
            if (!roomBelow && roomAbove)
            {
                placed.Y = m_Anchor.Y - placed.Height;
            }
            break;
        }

        case Placement::Beside:
        {
            placed.X = m_Anchor.X + m_Anchor.Width;
            placed.Y = m_Anchor.Y;
            if (placed.X + placed.Width > screen.X + screen.Width - EdgeMargin)
            {
                placed.X = m_Anchor.X - placed.Width;
            }
            break;
        }

        case Placement::Centered:
        {
            placed.X = screen.X + (screen.Width - placed.Width) * 0.5f;
            placed.Y = screen.Y + (screen.Height - placed.Height) * 0.5f;
            break;
        }
    }

    Element::Arrange(KeepInside(placed, screen));
}

const Style* Popup::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.Popup;
}

void Popup::Paint(DrawList& drawList)
{
    if (!HasAppearance())
    {
        kit::DrawRaised(drawList, GetTheme(), GetBounds(), GetTheme().CornerRadius);
    }
}

void Popup::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::KeyDown:
            if (event.KeyCode == Key::Escape && DismissOnEscape && !event.Repeat)
            {
                Close();
                event.Handled = true;
            }
            break;

        // The pointer stops here: a press inside a popup is never also a
        // press on whatever is behind it.
        case EventType::PointerDown:
        case EventType::PointerUp:
        case EventType::Wheel:
            event.Handled = true;
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Shortcut
// ---------------------------------------------------------------------------

namespace
{

struct KeyName
{
    Key Code;
    const char* Name;
};

const KeyName KeyNames[] = {
    { Key::Escape, "Esc" },       { Key::Space, "Space" },     { Key::Enter, "Enter" },
    { Key::Tab, "Tab" },          { Key::Backspace, "Backspace" }, { Key::Delete, "Delete" },
    { Key::Insert, "Insert" },    { Key::Left, "Left" },       { Key::Right, "Right" },
    { Key::Up, "Up" },            { Key::Down, "Down" },       { Key::Home, "Home" },
    { Key::End, "End" },          { Key::PageUp, "PageUp" },   { Key::PageDown, "PageDown" },
    { Key::F1, "F1" },            { Key::F2, "F2" },           { Key::F3, "F3" },
    { Key::F4, "F4" },            { Key::F5, "F5" },           { Key::F6, "F6" },
    { Key::F7, "F7" },            { Key::F8, "F8" },           { Key::F9, "F9" },
    { Key::F10, "F10" },          { Key::F11, "F11" },         { Key::F12, "F12" },
    { Key::Minus, "-" },          { Key::Equals, "=" },        { Key::LeftBracket, "[" },
    { Key::RightBracket, "]" },   { Key::Backslash, "\\" },    { Key::Semicolon, ";" },
    { Key::Apostrophe, "'" },     { Key::Comma, "," },         { Key::Period, "." },
    { Key::Slash, "/" },          { Key::Grave, "`" },
};

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

Key ParseKey(const std::string& name)
{
    if (name.size() == 1)
    {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if (c >= 'A' && c <= 'Z')
        {
            return static_cast<Key>(static_cast<int>(Key::A) + (c - 'A'));
        }
        if (c >= '0' && c <= '9')
        {
            return static_cast<Key>(static_cast<int>(Key::Num0) + (c - '0'));
        }
    }
    const std::string lowered = Lower(name);
    for (const KeyName& entry : KeyNames)
    {
        if (Lower(entry.Name) == lowered)
        {
            return entry.Code;
        }
    }
    if (lowered == "escape")
    {
        return Key::Escape;
    }
    if (lowered == "return")
    {
        return Key::Enter;
    }
    if (lowered == "del")
    {
        return Key::Delete;
    }
    return Key::Unknown;
}

std::string KeyToString(Key key)
{
    const int value = static_cast<int>(key);
    if (value >= static_cast<int>(Key::A) && value <= static_cast<int>(Key::Z))
    {
        return std::string(1, static_cast<char>('A' + (value - static_cast<int>(Key::A))));
    }
    if (value >= static_cast<int>(Key::Num0) && value <= static_cast<int>(Key::Num9))
    {
        return std::string(1, static_cast<char>('0' + (value - static_cast<int>(Key::Num0))));
    }
    for (const KeyName& entry : KeyNames)
    {
        if (entry.Code == key)
        {
            return entry.Name;
        }
    }
    return {};
}

} // namespace

bool Shortcut::Matches(const Event& keyEvent) const
{
    return IsValid() && keyEvent.Type == EventType::KeyDown && keyEvent.KeyCode == KeyCode &&
           keyEvent.Control == Control && keyEvent.Shift == Shift && keyEvent.Alt == Alt;
}

std::string Shortcut::ToString() const
{
    if (!IsValid())
    {
        return {};
    }
    std::string text;
    if (Control)
    {
        text += "Ctrl+";
    }
    if (Shift)
    {
        text += "Shift+";
    }
    if (Alt)
    {
        text += "Alt+";
    }
    return text + KeyToString(KeyCode);
}

Shortcut Shortcut::Parse(const std::string& text)
{
    Shortcut shortcut;
    if (text.empty())
    {
        return shortcut;
    }

    // Split on '+', keeping a trailing "+" as the key itself so "Ctrl++"
    // could be written, although "Ctrl+=" is the usual spelling.
    std::vector<std::string> parts;
    std::string current;
    for (size_t index = 0; index < text.size(); ++index)
    {
        const char c = text[index];
        if (c == '+' && !current.empty())
        {
            parts.push_back(current);
            current.clear();
        }
        else if (c != ' ')
        {
            current.push_back(c);
        }
    }
    if (!current.empty())
    {
        parts.push_back(current);
    }
    if (parts.empty())
    {
        return shortcut;
    }

    for (size_t index = 0; index + 1 < parts.size(); ++index)
    {
        const std::string modifier = Lower(parts[index]);
        if (modifier == "ctrl" || modifier == "control" || modifier == "cmd")
        {
            shortcut.Control = true;
        }
        else if (modifier == "shift")
        {
            shortcut.Shift = true;
        }
        else if (modifier == "alt" || modifier == "option")
        {
            shortcut.Alt = true;
        }
        else
        {
            return Shortcut{};
        }
    }

    shortcut.KeyCode = ParseKey(parts.back());
    if (!shortcut.IsValid())
    {
        return Shortcut{};
    }
    return shortcut;
}

// ---------------------------------------------------------------------------
// MenuItem
// ---------------------------------------------------------------------------

MenuItem MenuItem::Action(const std::string& text, std::function<void()> onSelected, const std::string& shortcut)
{
    MenuItem item;
    item.Text = text;
    item.OnSelected = std::move(onSelected);
    item.Keys = Shortcut::Parse(shortcut);
    if (!shortcut.empty() && !item.Keys.IsValid())
    {
        LogMessage(LogLevel::Warning, "ui", "Menu item \"%s\": \"%s\" is not a shortcut this build understands.",
                   text.c_str(), shortcut.c_str());
    }
    return item;
}

MenuItem MenuItem::Check(const std::string& text, bool checked, std::function<void()> onSelected)
{
    MenuItem item;
    item.Text = text;
    item.Checkable = true;
    item.Checked = checked;
    item.OnSelected = std::move(onSelected);
    return item;
}

MenuItem MenuItem::Submenu(const std::string& text, std::vector<MenuItem> children)
{
    MenuItem item;
    item.Text = text;
    item.Children = std::move(children);
    return item;
}

MenuItem MenuItem::Divider()
{
    MenuItem item;
    item.Separator = true;
    item.Enabled = false;
    return item;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

namespace
{

constexpr float MenuRowHeight = 28.0f;
constexpr float MenuSeparatorHeight = 9.0f;
constexpr float MenuPadding = 5.0f;
constexpr float MenuCheckColumn = 26.0f;
constexpr float MenuArrowColumn = 22.0f;

// How long the pointer rests on a submenu's row before it opens, so a
// diagonal move toward a submenu does not open every row it crosses.
constexpr float SubmenuDelay = 0.18f;

bool IsChoosable(const MenuItem& item)
{
    return !item.Separator && item.Enabled;
}

} // namespace

Menu::Menu()
{
    Padding = 0.0f;
    Size = Size2{};
}

Menu::~Menu()
{
    // The submenu is a sibling in the overlay; it outlives this menu, so it
    // is hidden and told to forget its owner rather than left pointing here.
    if (Menu* submenu = m_Submenu.Get())
    {
        submenu->Owner = nullptr;
        submenu->OnClosed = nullptr;
        submenu->Visible = false;
        if (Element* parent = submenu->GetParent())
        {
            parent->Remove(submenu);
        }
    }
}

Vec2 Menu::Measure(Vec2)
{
    float textWidth = 0.0f;
    float keysWidth = 0.0f;
    float height = MenuPadding * 2.0f;
    bool anySubmenu = false;

    for (const MenuItem& item : Items)
    {
        if (item.Separator)
        {
            height += MenuSeparatorHeight;
            continue;
        }
        height += MenuRowHeight;
        textWidth = std::max(textWidth, MeasureText(item.Text).X);
        if (item.Keys.IsValid())
        {
            keysWidth = std::max(keysWidth, MeasureText(item.Keys.ToString()).X);
        }
        anySubmenu = anySubmenu || !item.Children.empty();
    }

    const float width = MenuCheckColumn + textWidth + (keysWidth > 0.0f ? keysWidth + 32.0f : 0.0f) +
                        (anySubmenu ? MenuArrowColumn : 12.0f) + MenuPadding * 2.0f;
    return Vec2{ std::max(width, 160.0f), height };
}

Rect Menu::RowBounds(int index) const
{
    const Rect bounds = GetBounds();
    float y = bounds.Y + MenuPadding;
    for (int row = 0; row < static_cast<int>(Items.size()); ++row)
    {
        const float height = Items[static_cast<size_t>(row)].Separator ? MenuSeparatorHeight : MenuRowHeight;
        if (row == index)
        {
            return Rect{ bounds.X + MenuPadding, y, bounds.Width - MenuPadding * 2.0f, height };
        }
        y += height;
    }
    return Rect{};
}

int Menu::RowAt(Vec2 windowPoint) const
{
    for (int row = 0; row < static_cast<int>(Items.size()); ++row)
    {
        if (kit::Contains(RowBounds(row), windowPoint))
        {
            return row;
        }
    }
    return -1;
}

void Menu::OnOpened()
{
    m_Highlight = -1;
    m_HoverRow = -1;
    m_HoverTime = 0.0f;
    CloseSubmenu();
}

void Menu::Close()
{
    CloseSubmenu();
    Popup::Close();
}

void Menu::CloseSubmenu()
{
    if (Menu* submenu = m_Submenu.Get(); submenu != nullptr && submenu->IsOpen())
    {
        submenu->Close();
    }
    m_SubmenuIndex = -1;
}

void Menu::OpenSubmenu(int index)
{
    if (index < 0 || index >= static_cast<int>(Items.size()) || Items[static_cast<size_t>(index)].Children.empty())
    {
        return;
    }
    if (Menu* open = m_Submenu.Get(); m_SubmenuIndex == index && open != nullptr && open->IsOpen())
    {
        return;
    }

    CloseSubmenu();

    Menu* submenu = m_Submenu.Get();
    if (submenu == nullptr)
    {
        Element* parent = GetParent();
        if (parent == nullptr)
        {
            return;
        }
        submenu = parent->Add<Menu>();
        submenu->Name = "opane.Submenu";
        m_Submenu = submenu;

        // What the submenu changes, such as a checked option, is written back
        // here as it closes, so it shows the current state when it opens again.
        submenu->OnClosed = [this]() {
            Menu* closing = m_Submenu.Get();
            if (m_SubmenuIndex >= 0 && m_SubmenuIndex < static_cast<int>(Items.size()) && closing != nullptr)
            {
                Items[static_cast<size_t>(m_SubmenuIndex)].Children = closing->Items;
            }
        };
    }

    submenu->Owner = this;
    submenu->Items = Items[static_cast<size_t>(index)].Children;
    m_SubmenuIndex = index;
    submenu->OpenBesideRect(RowBounds(index));

    // Opened by the pointer, the keyboard stays with this menu so the hover
    // can move on; the submenu takes it only when Right opened it.
    RequestFocus();
}

void Menu::Choose(int index)
{
    if (index < 0 || index >= static_cast<int>(Items.size()))
    {
        return;
    }
    MenuItem& item = Items[static_cast<size_t>(index)];
    if (!IsChoosable(item))
    {
        return;
    }

    if (!item.Children.empty())
    {
        OpenSubmenu(index);
        if (Menu* submenu = m_Submenu.Get())
        {
            submenu->RequestFocus();
            submenu->MoveHighlight(1);
        }
        return;
    }

    if (item.Checkable)
    {
        item.Checked = !item.Checked;
    }

    // The callback runs after every menu in the chain has closed, so it can
    // open a dialog or another menu without this one in the way.
    const std::function<void()> action = item.OnSelected;

    Popup* top = this;
    while (top->Owner != nullptr)
    {
        top = top->Owner;
    }
    // A submenu writes its changes back as it closes; closing from the top
    // closes it first.
    top->Close();

    if (action)
    {
        action();
    }
}

void Menu::MoveHighlight(int direction)
{
    const int count = static_cast<int>(Items.size());
    if (count == 0)
    {
        return;
    }
    int row = m_Highlight;
    for (int step = 0; step < count; ++step)
    {
        row = row < 0 ? (direction > 0 ? 0 : count - 1) : (row + direction + count) % count;
        if (IsChoosable(Items[static_cast<size_t>(row)]))
        {
            m_Highlight = row;
            return;
        }
    }
}

void Menu::OnUpdate(float deltaSeconds)
{
    // A submenu opens once the pointer has settled on its row.
    if (m_HoverRow >= 0 && m_HoverRow < static_cast<int>(Items.size()))
    {
        m_HoverTime += deltaSeconds;
        const MenuItem& item = Items[static_cast<size_t>(m_HoverRow)];
        if (m_HoverTime >= SubmenuDelay)
        {
            if (!item.Children.empty() && item.Enabled)
            {
                OpenSubmenu(m_HoverRow);
            }
            else if (m_SubmenuIndex >= 0 && m_SubmenuIndex != m_HoverRow)
            {
                CloseSubmenu();
            }
        }
    }
}

void Menu::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    if (!HasAppearance())
    {
        kit::DrawRaised(drawList, theme, GetBounds(), 7.0f);
    }

    for (int row = 0; row < static_cast<int>(Items.size()); ++row)
    {
        const MenuItem& item = Items[static_cast<size_t>(row)];
        const Rect bounds = RowBounds(row);

        if (item.Separator)
        {
            drawList.FillRect(Rect{ bounds.X + 6.0f, bounds.Y + bounds.Height * 0.5f, bounds.Width - 12.0f, 1.0f },
                              theme.Border);
            continue;
        }

        const bool highlighted = (row == m_Highlight || row == m_SubmenuIndex) && item.Enabled;
        if (highlighted)
        {
            drawList.FillRoundedRect(bounds, 5.0f, theme.Accent);
        }

        const Color text = !item.Enabled ? kit::WithAlpha(theme.TextMuted, 0.55f)
                                         : (highlighted ? Color{ 1.0f, 1.0f, 1.0f, 1.0f } : theme.Text);
        const Color muted = highlighted ? Color{ 1.0f, 1.0f, 1.0f, 0.8f } : theme.TextMuted;

        if (item.Checkable && item.Checked)
        {
            kit::DrawTick(drawList, Vec2{ bounds.X + MenuCheckColumn * 0.5f, bounds.Y + bounds.Height * 0.5f }, 11.0f,
                          text, 1.7f);
        }

        drawList.DrawTextInRect(item.Text,
                                Rect{ bounds.X + MenuCheckColumn, bounds.Y, bounds.Width - MenuCheckColumn,
                                      bounds.Height },
                                theme.Font, text, TextAlign::Left);

        if (!item.Children.empty())
        {
            kit::DrawChevron(drawList, Vec2{ bounds.X + bounds.Width - 12.0f, bounds.Y + bounds.Height * 0.5f }, 8.0f,
                             kit::Direction::Right, text, 1.5f);
        }
        else if (item.Keys.IsValid())
        {
            drawList.DrawTextInRect(item.Keys.ToString(),
                                    Rect{ bounds.X, bounds.Y, bounds.Width - 10.0f, bounds.Height }, theme.Font,
                                    muted, TextAlign::Right);
        }
    }
}

void Menu::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerMove:
        {
            const int row = RowAt(event.Position);
            if (row != m_HoverRow)
            {
                m_HoverRow = row;
                m_HoverTime = 0.0f;
            }
            if (row >= 0 && IsChoosable(Items[static_cast<size_t>(row)]))
            {
                m_Highlight = row;
            }
            event.Handled = true;
            return;
        }

        case EventType::PointerLeave:
            // Leaving toward an open submenu keeps its row lit.
            m_HoverRow = -1;
            if (m_SubmenuIndex < 0)
            {
                m_Highlight = -1;
            }
            return;

        case EventType::PointerUp:
        {
            if (event.Button == MouseButton::Left || event.Button == MouseButton::Right)
            {
                Choose(RowAt(event.Position));
            }
            event.Handled = true;
            return;
        }

        case EventType::KeyDown:
        {
            switch (event.KeyCode)
            {
                case Key::Down:
                    MoveHighlight(1);
                    event.Handled = true;
                    return;
                case Key::Up:
                    MoveHighlight(-1);
                    event.Handled = true;
                    return;
                case Key::Home:
                    m_Highlight = -1;
                    MoveHighlight(1);
                    event.Handled = true;
                    return;
                case Key::End:
                    m_Highlight = -1;
                    MoveHighlight(-1);
                    event.Handled = true;
                    return;
                case Key::Right:
                    if (m_Highlight >= 0 && !Items[static_cast<size_t>(m_Highlight)].Children.empty())
                    {
                        Choose(m_Highlight);
                    }
                    event.Handled = true;
                    return;
                case Key::Left:
                    // Back out of a submenu into the menu that opened it.
                    if (Owner != nullptr)
                    {
                        Popup* owner = Owner;
                        Close();
                        owner->RequestFocus();
                        event.Handled = true;
                        return;
                    }
                    break;
                case Key::Enter:
                case Key::Space:
                    if (!event.Repeat)
                    {
                        Choose(m_Highlight);
                    }
                    event.Handled = true;
                    return;
                default:
                    break;
            }
            break;
        }

        default:
            break;
    }

    Popup::OnEvent(event);
}

// ---------------------------------------------------------------------------
// MenuBar
// ---------------------------------------------------------------------------

namespace
{

constexpr float BarTitlePadding = 11.0f;

bool TriggerShortcut(std::vector<MenuItem>& items, const Event& keyEvent)
{
    for (MenuItem& item : items)
    {
        if (!item.Enabled || item.Separator)
        {
            continue;
        }
        if (item.Keys.Matches(keyEvent))
        {
            if (item.Checkable)
            {
                item.Checked = !item.Checked;
            }
            if (item.OnSelected)
            {
                // Copied first: the callback may rebuild the very menu this
                // item lives in.
                const std::function<void()> action = item.OnSelected;
                action();
            }
            return true;
        }
        if (TriggerShortcut(item.Children, keyEvent))
        {
            return true;
        }
    }
    return false;
}

} // namespace

MenuBar::MenuBar()
{
    Size = Size2{ Dim::FromScale(1.0f), Dim::FromOffset(30.0f) };
}

MenuBar::~MenuBar()
{
    // The menu lives in the overlay, not inside the bar, so it is let go of
    // here: its callback points at this bar and must not outlive it.
    if (Menu* menu = m_Menu.Get())
    {
        menu->OnClosed = nullptr;
        menu->Visible = false;
        if (Element* parent = menu->GetParent())
        {
            parent->Remove(menu);
        }
    }
}

Vec2 MenuBar::Measure(Vec2 available)
{
    const Vec2 requested = Size.Resolve(available);
    return Vec2{ requested.X, requested.Y > 0.0f ? requested.Y : GetLineHeight() + 12.0f };
}

Rect MenuBar::TitleBounds(int index) const
{
    const Rect bounds = GetBounds();
    float x = bounds.X + 4.0f;
    for (int entry = 0; entry < static_cast<int>(Menus.size()); ++entry)
    {
        const float width = MeasureText(Menus[static_cast<size_t>(entry)].Title).X + BarTitlePadding * 2.0f;
        if (entry == index)
        {
            return Rect{ x, bounds.Y + 3.0f, width, bounds.Height - 6.0f };
        }
        x += width;
    }
    return Rect{};
}

int MenuBar::TitleAt(Vec2 point) const
{
    for (int entry = 0; entry < static_cast<int>(Menus.size()); ++entry)
    {
        if (kit::Contains(TitleBounds(entry), point))
        {
            return entry;
        }
    }
    return -1;
}

void MenuBar::OpenMenu(int index)
{
    if (index < 0 || index >= static_cast<int>(Menus.size()))
    {
        return;
    }

    App app = GetApp();
    if (!app.IsValid())
    {
        return;
    }

    // Made on first use, in the overlay. An overlay cleared wholesale by the
    // program takes the menu with it, and the reference then finds nothing.
    Menu* menu = m_Menu.Get();
    if (menu == nullptr)
    {
        menu = app.GetOverlay()->Add<Menu>();
        menu->Name = "opane.MenuBarMenu";
        m_Menu = menu;
    }

    if (menu->IsOpen())
    {
        // Switching menus keeps what changed in the one being left.
        if (m_Open >= 0 && m_Open < static_cast<int>(Menus.size()))
        {
            Menus[static_cast<size_t>(m_Open)].Items = menu->Items;
        }
        menu->OnClosed = nullptr;
        menu->Close();
    }

    m_Open = index;
    menu->Items = Menus[static_cast<size_t>(index)].Items;
    menu->OnClosed = [this]() {
        // A checked option changed in the menu is kept for next time.
        Menu* closing = m_Menu.Get();
        if (m_Open >= 0 && m_Open < static_cast<int>(Menus.size()) && closing != nullptr)
        {
            Menus[static_cast<size_t>(m_Open)].Items = closing->Items;
        }
        m_Open = -1;
    };
    menu->OpenBelowRect(TitleBounds(index));
}

void MenuBar::OnUpdate(float)
{
    if (Menu* menu = m_Menu.Get(); m_Open >= 0 && (menu == nullptr || !menu->IsOpen()))
    {
        m_Open = -1;
    }
}

bool MenuBar::OnShortcut(const Event& keyEvent)
{
    for (Entry& entry : Menus)
    {
        if (TriggerShortcut(entry.Items, keyEvent))
        {
            return true;
        }
    }
    return false;
}

void MenuBar::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();

    drawList.FillRect(bounds, theme.Surface);
    drawList.FillRect(Rect{ bounds.X, bounds.Y + bounds.Height - 1.0f, bounds.Width, 1.0f }, theme.Border);

    for (int entry = 0; entry < static_cast<int>(Menus.size()); ++entry)
    {
        const Rect title = TitleBounds(entry);
        if (entry == m_Open)
        {
            drawList.FillRoundedRect(title, 5.0f, theme.SurfacePressed);
        }
        else if (entry == m_Hover)
        {
            drawList.FillRoundedRect(title, 5.0f, theme.SurfaceHovered);
        }
        drawList.DrawTextInRect(Menus[static_cast<size_t>(entry)].Title, title, theme.Font, theme.Text,
                                TextAlign::Center);
    }
}

void MenuBar::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerMove:
        {
            m_Hover = TitleAt(event.Position);
            // With one menu open, moving along the bar opens the others.
            if (m_Open >= 0 && m_Hover >= 0 && m_Hover != m_Open)
            {
                OpenMenu(m_Hover);
            }
            break;
        }

        case EventType::PointerLeave:
            m_Hover = -1;
            break;

        case EventType::PointerDown:
        {
            if (event.Button == MouseButton::Left)
            {
                const int index = TitleAt(event.Position);
                if (index >= 0)
                {
                    OpenMenu(index);
                }
            }
            event.Handled = true;
            break;
        }

        case EventType::PointerUp:
            event.Handled = true;
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------

namespace
{

constexpr float ListRowHeight = 26.0f;

// The list a drop-down opens. Private to this file.
class DropdownList : public Popup
{
public:
    Dropdown* Source = nullptr;
    int Highlight = -1;
    float Scroll = 0.0f;

    DropdownList()
    {
        Padding = 0.0f;
    }

    int VisibleCount() const
    {
        if (Source == nullptr)
        {
            return 0;
        }
        return std::max(1, std::min(static_cast<int>(Source->Options.size()), std::max(1, Source->VisibleRows)));
    }

    float MaximumScroll() const
    {
        if (Source == nullptr)
        {
            return 0.0f;
        }
        return std::max(0.0f, static_cast<float>(Source->Options.size()) * ListRowHeight -
                                  static_cast<float>(VisibleCount()) * ListRowHeight);
    }

    Vec2 Measure(Vec2) override
    {
        const float width = Source != nullptr ? Source->GetBounds().Width : 200.0f;
        return Vec2{ std::max(width, 80.0f), static_cast<float>(VisibleCount()) * ListRowHeight + 8.0f };
    }

    Rect RowBounds(int index) const
    {
        const Rect bounds = GetBounds();
        return Rect{ bounds.X + 4.0f, bounds.Y + 4.0f + static_cast<float>(index) * ListRowHeight - Scroll,
                     bounds.Width - 8.0f, ListRowHeight };
    }

    int RowAt(Vec2 point) const
    {
        if (Source == nullptr || !kit::Contains(kit::Inset(GetBounds(), 4.0f), point))
        {
            return -1;
        }
        const int row = static_cast<int>(std::floor((point.Y - GetBounds().Y - 4.0f + Scroll) / ListRowHeight));
        return row >= 0 && row < static_cast<int>(Source->Options.size()) ? row : -1;
    }

    void Reveal(int index)
    {
        const float top = static_cast<float>(index) * ListRowHeight;
        const float view = static_cast<float>(VisibleCount()) * ListRowHeight;
        if (top < Scroll)
        {
            Scroll = top;
        }
        else if (top + ListRowHeight > Scroll + view)
        {
            Scroll = top + ListRowHeight - view;
        }
        Scroll = std::clamp(Scroll, 0.0f, MaximumScroll());
    }

    void Paint(DrawList& drawList) override
    {
        const Theme& theme = GetTheme();
        const Rect bounds = GetBounds();
        kit::DrawRaised(drawList, theme, bounds, 7.0f);
        if (Source == nullptr)
        {
            return;
        }

        drawList.PushClip(kit::Inset(bounds, 4.0f));

        // Only the rows in view.
        const int first = std::max(0, static_cast<int>(Scroll / ListRowHeight));
        const int last = std::min(static_cast<int>(Source->Options.size()), first + VisibleCount() + 1);
        for (int row = first; row < last; ++row)
        {
            const Rect rowBounds = RowBounds(row);
            const bool selected = row == Source->Selected;
            if (row == Highlight)
            {
                drawList.FillRoundedRect(rowBounds, 5.0f, theme.Accent);
            }
            else if (selected)
            {
                drawList.FillRoundedRect(rowBounds, 5.0f, theme.SurfacePressed);
            }
            const Color text = row == Highlight ? Color{ 1.0f, 1.0f, 1.0f, 1.0f } : theme.Text;
            drawList.DrawTextInRect(Source->Options[static_cast<size_t>(row)],
                                    Rect{ rowBounds.X + 10.0f, rowBounds.Y, rowBounds.Width - 20.0f, rowBounds.Height },
                                    theme.Font, text, TextAlign::Left);
        }

        drawList.PopClip();

        // A thin bar says there is more than shows.
        const float maximum = MaximumScroll();
        if (maximum > 0.0f)
        {
            const float view = static_cast<float>(VisibleCount()) * ListRowHeight;
            const float total = view + maximum;
            const float length = std::max(18.0f, view * view / total);
            const float y = bounds.Y + 4.0f + (view - length) * (Scroll / maximum);
            drawList.FillRoundedRect(Rect{ bounds.X + bounds.Width - 7.0f, y, 3.0f, length }, 1.5f,
                                     kit::WithAlpha(theme.TextMuted, 0.6f));
        }
    }

    void Pick(int row)
    {
        if (Source == nullptr || row < 0)
        {
            return;
        }
        Dropdown* source = Source;
        Close();
        source->Select(row);
        source->RequestFocus();
    }

    void OnEvent(Event& event) override
    {
        switch (event.Type)
        {
            case EventType::PointerMove:
            {
                const int row = RowAt(event.Position);
                if (row >= 0)
                {
                    Highlight = row;
                }
                event.Handled = true;
                return;
            }
            case EventType::PointerUp:
                if (event.Button == MouseButton::Left)
                {
                    Pick(RowAt(event.Position));
                }
                event.Handled = true;
                return;
            case EventType::Wheel:
                Scroll = std::clamp(Scroll - event.WheelDelta * ListRowHeight * 3.0f, 0.0f, MaximumScroll());
                event.Handled = true;
                return;
            case EventType::KeyDown:
            {
                if (Source == nullptr)
                {
                    break;
                }
                const int count = static_cast<int>(Source->Options.size());
                switch (event.KeyCode)
                {
                    case Key::Down:
                        Highlight = std::min(count - 1, Highlight + 1);
                        Reveal(Highlight);
                        event.Handled = true;
                        return;
                    case Key::Up:
                        Highlight = std::max(0, Highlight - 1);
                        Reveal(Highlight);
                        event.Handled = true;
                        return;
                    case Key::PageDown:
                        Highlight = std::min(count - 1, Highlight + VisibleCount());
                        Reveal(Highlight);
                        event.Handled = true;
                        return;
                    case Key::PageUp:
                        Highlight = std::max(0, Highlight - VisibleCount());
                        Reveal(Highlight);
                        event.Handled = true;
                        return;
                    case Key::Home:
                        Highlight = 0;
                        Reveal(Highlight);
                        event.Handled = true;
                        return;
                    case Key::End:
                        Highlight = count - 1;
                        Reveal(Highlight);
                        event.Handled = true;
                        return;
                    case Key::Enter:
                    case Key::Space:
                        if (!event.Repeat)
                        {
                            Pick(Highlight);
                        }
                        event.Handled = true;
                        return;
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }
        Popup::OnEvent(event);
    }
};

} // namespace

Dropdown::Dropdown()
{
    Size = Size2{ Dim::FromScale(1.0f), Dim::FromOffset(34.0f) };
    Focusable = true;
}

Dropdown::~Dropdown()
{
    if (Popup* list = m_List.Get())
    {
        if (auto* dropdownList = dynamic_cast<DropdownList*>(list))
        {
            dropdownList->Source = nullptr;
        }
        list->OnClosed = nullptr;
        list->Visible = false;
        if (Element* parent = list->GetParent())
        {
            parent->Remove(list);
        }
    }
}

bool Dropdown::IsListOpen() const
{
    const Popup* list = m_List.Get();
    return list != nullptr && list->IsOpen();
}

void Dropdown::Select(int index)
{
    if (index < -1 || index >= static_cast<int>(Options.size()))
    {
        return;
    }
    if (index != Selected)
    {
        Selected = index;
        if (OnChanged)
        {
            kit::Invoke(OnChanged, Selected);
        }
    }
}

void Dropdown::Open()
{
    App app = GetApp();
    if (!app.IsValid() || Options.empty())
    {
        return;
    }

    // Made on first use, in the overlay. An overlay cleared wholesale by the
    // program takes the list with it, and the reference then finds nothing.
    auto* list = static_cast<DropdownList*>(m_List.Get());
    if (list == nullptr)
    {
        list = app.GetOverlay()->Add<DropdownList>();
        list->Name = "opane.DropdownList";
        m_List = list;
    }

    list->Source = this;
    list->Highlight = std::max(0, Selected);
    list->Scroll = 0.0f;
    list->Reveal(list->Highlight);
    list->OpenBelow(*this);
}

void Dropdown::CloseList()
{
    if (Popup* list = m_List.Get(); list != nullptr && list->IsOpen())
    {
        list->Close();
    }
}

Vec2 Dropdown::Measure(Vec2 available)
{
    const Vec2 requested = Size.Resolve(available);
    if (requested.X > 0.0f && requested.Y > 0.0f)
    {
        return requested;
    }
    float widest = MeasureText(Placeholder).X;
    for (const std::string& option : Options)
    {
        widest = std::max(widest, MeasureText(option).X);
    }
    return Vec2{ requested.X > 0.0f ? requested.X : widest + 48.0f,
                 requested.Y > 0.0f ? requested.Y : GetLineHeight() + 14.0f };
}

const Style* Dropdown::GetDefaultAppearance(const Theme& theme) const
{
    return &theme.Styles.Dropdown;
}

void Dropdown::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    const float radius = std::min(theme.CornerRadius, bounds.Height * 0.5f);

    if (!HasAppearance())
    {
        const Color fill =
            IsListOpen() ? theme.SurfacePressed : (IsHovered() ? theme.SurfaceHovered : theme.Surface);
        drawList.FillRoundedRect(bounds, radius, fill);
        drawList.StrokeRect(bounds, theme.BorderWidth, IsListOpen() ? theme.Accent : theme.Border, radius);
    }
    kit::DrawFocusRing(drawList, *this, bounds, radius);

    const bool chosen = Selected >= 0 && Selected < static_cast<int>(Options.size());
    const std::string& text = chosen ? Options[static_cast<size_t>(Selected)] : Placeholder;

    const Rect textBounds{ bounds.X + 12.0f, bounds.Y, bounds.Width - 40.0f, bounds.Height };
    drawList.PushClip(textBounds);
    drawList.DrawTextInRect(text, textBounds, theme.Font,
                            chosen ? kit::LookText(*this, theme.Text) : theme.TextMuted, TextAlign::Left);
    drawList.PopClip();

    kit::DrawChevron(drawList, Vec2{ bounds.X + bounds.Width - 18.0f, bounds.Y + bounds.Height * 0.5f }, 9.0f,
                     IsListOpen() ? kit::Direction::Up : kit::Direction::Down, theme.TextMuted, 1.6f);
}

void Dropdown::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerDown:
            if (event.Button == MouseButton::Left)
            {
                if (IsListOpen())
                {
                    CloseList();
                }
                else
                {
                    Open();
                }
            }
            event.Handled = true;
            break;

        case EventType::PointerUp:
            event.Handled = true;
            break;

        case EventType::KeyDown:
        {
            const int count = static_cast<int>(Options.size());
            switch (event.KeyCode)
            {
                case Key::Enter:
                case Key::Space:
                    if (!event.Repeat)
                    {
                        Open();
                    }
                    event.Handled = true;
                    break;
                case Key::Down:
                    if (event.Alt)
                    {
                        Open();
                    }
                    else if (count > 0)
                    {
                        Select(std::min(count - 1, Selected + 1));
                    }
                    event.Handled = true;
                    break;
                case Key::Up:
                    if (count > 0)
                    {
                        Select(std::max(0, Selected - 1));
                    }
                    event.Handled = true;
                    break;
                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

namespace
{

constexpr float DialogPadding = 20.0f;
constexpr float DialogButtonHeight = 34.0f;
constexpr float DialogButtonGap = 8.0f;
constexpr float DialogMessageWidth = 420.0f;

} // namespace

Dialog::Dialog()
{
    Modal = true;
    DismissOnOutsideClick = false;
    DismissOnEscape = true;
    Padding = 0.0f;
    Size = Size2{};
}

void Dialog::OnOpened()
{
    m_Finishing = false;
    m_KeyboardFocus = false;
    m_Hover = -1;
    m_Pressed = -1;
    m_Focus = std::clamp(DefaultButton, 0, std::max(0, static_cast<int>(Buttons.size()) - 1));
}

Vec2 Dialog::Measure(Vec2)
{
    App app = GetApp();
    const FontId font = GetTheme().Font;

    float buttonsWidth = 0.0f;
    for (const std::string& button : Buttons)
    {
        buttonsWidth += std::max(84.0f, MeasureText(button).X + 32.0f) + DialogButtonGap;
    }

    const float titleWidth = MeasureText(Title).X;
    const float messageWidth = std::min(DialogMessageWidth, MeasureText(Message).X);
    const float width = std::clamp(std::max({ titleWidth, messageWidth, buttonsWidth }) + DialogPadding * 2.0f,
                                   300.0f, DialogMessageWidth + DialogPadding * 2.0f);

    const float messageHeight = app.IsValid() && !Message.empty()
                                    ? app.MeasureTextWrapped(font, Message, width - DialogPadding * 2.0f).Y
                                    : 0.0f;
    const float titleHeight = Title.empty() ? 0.0f : GetLineHeight() + 10.0f;
    return Vec2{ width, DialogPadding + titleHeight + messageHeight + DialogPadding + DialogButtonHeight +
                            DialogPadding };
}

Rect Dialog::ButtonBounds(int index) const
{
    const Rect bounds = GetBounds();
    float x = bounds.X + bounds.Width - DialogPadding;
    const float y = bounds.Y + bounds.Height - DialogPadding - DialogButtonHeight;
    for (int button = static_cast<int>(Buttons.size()) - 1; button >= 0; --button)
    {
        const float width = std::max(84.0f, MeasureText(Buttons[static_cast<size_t>(button)]).X + 32.0f);
        x -= width;
        if (button == index)
        {
            return Rect{ x, y, width, DialogButtonHeight };
        }
        x -= DialogButtonGap;
    }
    return Rect{};
}

void Dialog::Paint(DrawList& drawList)
{
    const Theme& theme = GetTheme();
    const Rect bounds = GetBounds();
    if (!HasAppearance())
    {
        kit::DrawRaised(drawList, theme, bounds, theme.CornerRadius + 2.0f);
    }

    float y = bounds.Y + DialogPadding;
    if (!Title.empty())
    {
        drawList.DrawTextInRect(Title, Rect{ bounds.X + DialogPadding, y, bounds.Width - DialogPadding * 2.0f, GetLineHeight() },
                                theme.Font, theme.Text, TextAlign::Left);
        y += GetLineHeight() + 10.0f;
    }
    if (!Message.empty())
    {
        drawList.DrawTextWrapped(Message,
                                 Rect{ bounds.X + DialogPadding, y, bounds.Width - DialogPadding * 2.0f,
                                       bounds.Height },
                                 theme.Font, theme.TextMuted, TextAlign::Left);
    }

    for (int button = 0; button < static_cast<int>(Buttons.size()); ++button)
    {
        const Rect rect = ButtonBounds(button);
        const bool isDefault = button == DefaultButton;
        Color fill = isDefault ? theme.Accent : theme.Surface;
        if (button == m_Pressed)
        {
            fill = isDefault ? theme.Accent : theme.SurfacePressed;
        }
        else if (button == m_Hover)
        {
            fill = isDefault ? theme.AccentHovered : theme.SurfaceHovered;
        }
        drawList.FillRoundedRect(rect, theme.CornerRadius, fill);
        drawList.StrokeRect(rect, theme.BorderWidth, theme.Border, theme.CornerRadius);
        if (button == m_Focus && m_KeyboardFocus)
        {
            drawList.StrokeRect(kit::Inset(rect, -2.0f), 2.0f, theme.Accent, theme.CornerRadius + 2.0f);
        }
        drawList.DrawTextInRect(Buttons[static_cast<size_t>(button)], rect, theme.Font,
                                isDefault ? Color{ 1.0f, 1.0f, 1.0f, 1.0f } : theme.Text, TextAlign::Center);
    }
}

void Dialog::OnUpdate(float)
{
}

void Dialog::Finish(int button)
{
    if (m_Finishing)
    {
        return;
    }
    m_Finishing = true;
    Popup::Close();
    if (OnResult)
    {
        const std::function<void(int)> handler = OnResult;
        handler(button);
    }
}

void Dialog::Close()
{
    // However it closes (Escape, or the program calling Close) it reports a
    // result, and without a button press that result is the cancel button.
    if (IsOpen())
    {
        Finish(CancelButton);
    }
}

void Dialog::OnEvent(Event& event)
{
    switch (event.Type)
    {
        case EventType::PointerMove:
        {
            m_Hover = -1;
            for (int button = 0; button < static_cast<int>(Buttons.size()); ++button)
            {
                if (kit::Contains(ButtonBounds(button), event.Position))
                {
                    m_Hover = button;
                }
            }
            event.Handled = true;
            return;
        }

        case EventType::PointerDown:
            m_Pressed = event.Button == MouseButton::Left ? m_Hover : -1;
            event.Handled = true;
            return;

        case EventType::PointerUp:
        {
            const int pressed = m_Pressed;
            m_Pressed = -1;
            if (pressed >= 0 && kit::Contains(ButtonBounds(pressed), event.Position))
            {
                Finish(pressed);
            }
            event.Handled = true;
            return;
        }

        case EventType::KeyDown:
        {
            const int count = static_cast<int>(Buttons.size());
            switch (event.KeyCode)
            {
                case Key::Left:
                    m_Focus = count > 0 ? (m_Focus + count - 1) % count : 0;
                    m_KeyboardFocus = true;
                    event.Handled = true;
                    return;
                case Key::Right:
                case Key::Tab:
                    m_Focus = count > 0 ? (m_Focus + (event.Shift ? count - 1 : 1)) % count : 0;
                    m_KeyboardFocus = true;
                    event.Handled = true;
                    return;
                case Key::Enter:
                case Key::Space:
                    // Enter presses the default button until the keyboard has
                    // chosen another; Space presses whichever has the ring.
                    if (!event.Repeat)
                    {
                        Finish(event.KeyCode == Key::Enter && !m_KeyboardFocus ? DefaultButton : m_Focus);
                    }
                    event.Handled = true;
                    return;
                case Key::Escape:
                    if (!event.Repeat && DismissOnEscape)
                    {
                        Finish(CancelButton);
                    }
                    event.Handled = true;
                    return;
                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
    Popup::OnEvent(event);
}

} // namespace opane
