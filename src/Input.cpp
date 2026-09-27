#include "Internal.h"

#include <array>

namespace opane
{
namespace
{

// One entry per Key, in declaration order. Kept as a table rather than a switch
// so the reverse lookup can be built from the same data.
constexpr std::array<SDL_Scancode, static_cast<size_t>(Key::Count)> BuildKeyTable()
{
    std::array<SDL_Scancode, static_cast<size_t>(Key::Count)> table{};

    for (auto& entry : table)
    {
        entry = SDL_SCANCODE_UNKNOWN;
    }

    table[static_cast<size_t>(Key::A)] = SDL_SCANCODE_A;
    table[static_cast<size_t>(Key::B)] = SDL_SCANCODE_B;
    table[static_cast<size_t>(Key::C)] = SDL_SCANCODE_C;
    table[static_cast<size_t>(Key::D)] = SDL_SCANCODE_D;
    table[static_cast<size_t>(Key::E)] = SDL_SCANCODE_E;
    table[static_cast<size_t>(Key::F)] = SDL_SCANCODE_F;
    table[static_cast<size_t>(Key::G)] = SDL_SCANCODE_G;
    table[static_cast<size_t>(Key::H)] = SDL_SCANCODE_H;
    table[static_cast<size_t>(Key::I)] = SDL_SCANCODE_I;
    table[static_cast<size_t>(Key::J)] = SDL_SCANCODE_J;
    table[static_cast<size_t>(Key::K)] = SDL_SCANCODE_K;
    table[static_cast<size_t>(Key::L)] = SDL_SCANCODE_L;
    table[static_cast<size_t>(Key::M)] = SDL_SCANCODE_M;
    table[static_cast<size_t>(Key::N)] = SDL_SCANCODE_N;
    table[static_cast<size_t>(Key::O)] = SDL_SCANCODE_O;
    table[static_cast<size_t>(Key::P)] = SDL_SCANCODE_P;
    table[static_cast<size_t>(Key::Q)] = SDL_SCANCODE_Q;
    table[static_cast<size_t>(Key::R)] = SDL_SCANCODE_R;
    table[static_cast<size_t>(Key::S)] = SDL_SCANCODE_S;
    table[static_cast<size_t>(Key::T)] = SDL_SCANCODE_T;
    table[static_cast<size_t>(Key::U)] = SDL_SCANCODE_U;
    table[static_cast<size_t>(Key::V)] = SDL_SCANCODE_V;
    table[static_cast<size_t>(Key::W)] = SDL_SCANCODE_W;
    table[static_cast<size_t>(Key::X)] = SDL_SCANCODE_X;
    table[static_cast<size_t>(Key::Y)] = SDL_SCANCODE_Y;
    table[static_cast<size_t>(Key::Z)] = SDL_SCANCODE_Z;

    table[static_cast<size_t>(Key::Num0)] = SDL_SCANCODE_0;
    table[static_cast<size_t>(Key::Num1)] = SDL_SCANCODE_1;
    table[static_cast<size_t>(Key::Num2)] = SDL_SCANCODE_2;
    table[static_cast<size_t>(Key::Num3)] = SDL_SCANCODE_3;
    table[static_cast<size_t>(Key::Num4)] = SDL_SCANCODE_4;
    table[static_cast<size_t>(Key::Num5)] = SDL_SCANCODE_5;
    table[static_cast<size_t>(Key::Num6)] = SDL_SCANCODE_6;
    table[static_cast<size_t>(Key::Num7)] = SDL_SCANCODE_7;
    table[static_cast<size_t>(Key::Num8)] = SDL_SCANCODE_8;
    table[static_cast<size_t>(Key::Num9)] = SDL_SCANCODE_9;

    table[static_cast<size_t>(Key::Escape)]    = SDL_SCANCODE_ESCAPE;
    table[static_cast<size_t>(Key::Space)]     = SDL_SCANCODE_SPACE;
    table[static_cast<size_t>(Key::Enter)]     = SDL_SCANCODE_RETURN;
    table[static_cast<size_t>(Key::Tab)]       = SDL_SCANCODE_TAB;
    table[static_cast<size_t>(Key::Backspace)] = SDL_SCANCODE_BACKSPACE;
    table[static_cast<size_t>(Key::Delete)]    = SDL_SCANCODE_DELETE;

    table[static_cast<size_t>(Key::Left)]  = SDL_SCANCODE_LEFT;
    table[static_cast<size_t>(Key::Right)] = SDL_SCANCODE_RIGHT;
    table[static_cast<size_t>(Key::Up)]    = SDL_SCANCODE_UP;
    table[static_cast<size_t>(Key::Down)]  = SDL_SCANCODE_DOWN;

    table[static_cast<size_t>(Key::Home)]     = SDL_SCANCODE_HOME;
    table[static_cast<size_t>(Key::End)]      = SDL_SCANCODE_END;
    table[static_cast<size_t>(Key::PageUp)]   = SDL_SCANCODE_PAGEUP;
    table[static_cast<size_t>(Key::PageDown)] = SDL_SCANCODE_PAGEDOWN;

    table[static_cast<size_t>(Key::LeftShift)]    = SDL_SCANCODE_LSHIFT;
    table[static_cast<size_t>(Key::RightShift)]   = SDL_SCANCODE_RSHIFT;
    table[static_cast<size_t>(Key::LeftControl)]  = SDL_SCANCODE_LCTRL;
    table[static_cast<size_t>(Key::RightControl)] = SDL_SCANCODE_RCTRL;
    table[static_cast<size_t>(Key::LeftAlt)]      = SDL_SCANCODE_LALT;
    table[static_cast<size_t>(Key::RightAlt)]     = SDL_SCANCODE_RALT;

    table[static_cast<size_t>(Key::F1)]  = SDL_SCANCODE_F1;
    table[static_cast<size_t>(Key::F2)]  = SDL_SCANCODE_F2;
    table[static_cast<size_t>(Key::F3)]  = SDL_SCANCODE_F3;
    table[static_cast<size_t>(Key::F4)]  = SDL_SCANCODE_F4;
    table[static_cast<size_t>(Key::F5)]  = SDL_SCANCODE_F5;
    table[static_cast<size_t>(Key::F6)]  = SDL_SCANCODE_F6;
    table[static_cast<size_t>(Key::F7)]  = SDL_SCANCODE_F7;
    table[static_cast<size_t>(Key::F8)]  = SDL_SCANCODE_F8;
    table[static_cast<size_t>(Key::F9)]  = SDL_SCANCODE_F9;
    table[static_cast<size_t>(Key::F10)] = SDL_SCANCODE_F10;
    table[static_cast<size_t>(Key::F11)] = SDL_SCANCODE_F11;
    table[static_cast<size_t>(Key::F12)] = SDL_SCANCODE_F12;

    table[static_cast<size_t>(Key::Insert)]       = SDL_SCANCODE_INSERT;
    table[static_cast<size_t>(Key::Minus)]        = SDL_SCANCODE_MINUS;
    table[static_cast<size_t>(Key::Equals)]       = SDL_SCANCODE_EQUALS;
    table[static_cast<size_t>(Key::LeftBracket)]  = SDL_SCANCODE_LEFTBRACKET;
    table[static_cast<size_t>(Key::RightBracket)] = SDL_SCANCODE_RIGHTBRACKET;
    table[static_cast<size_t>(Key::Backslash)]    = SDL_SCANCODE_BACKSLASH;
    table[static_cast<size_t>(Key::Semicolon)]    = SDL_SCANCODE_SEMICOLON;
    table[static_cast<size_t>(Key::Apostrophe)]   = SDL_SCANCODE_APOSTROPHE;
    table[static_cast<size_t>(Key::Comma)]        = SDL_SCANCODE_COMMA;
    table[static_cast<size_t>(Key::Period)]       = SDL_SCANCODE_PERIOD;
    table[static_cast<size_t>(Key::Slash)]        = SDL_SCANCODE_SLASH;
    table[static_cast<size_t>(Key::Grave)]        = SDL_SCANCODE_GRAVE;

    return table;
}

constexpr auto KeyTable = BuildKeyTable();

} // namespace

namespace detail
{

SDL_Scancode ToScancode(Key key)
{
    const size_t index = static_cast<size_t>(key);
    if (index >= KeyTable.size())
    {
        return SDL_SCANCODE_UNKNOWN;
    }
    return KeyTable[index];
}

Key FromScancode(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN)
    {
        return Key::Unknown;
    }

    for (size_t index = 1; index < KeyTable.size(); ++index)
    {
        if (KeyTable[index] == scancode)
        {
            return static_cast<Key>(index);
        }
    }

    return Key::Unknown;
}

} // namespace detail

bool Input::IsKeyDown(Key key) const
{
    const size_t index = static_cast<size_t>(key);
    return index < KeyCount && m_KeyDown[index];
}

bool Input::WasKeyPressed(Key key) const
{
    const size_t index = static_cast<size_t>(key);
    return index < KeyCount && m_KeyPressed[index];
}

bool Input::WasKeyReleased(Key key) const
{
    const size_t index = static_cast<size_t>(key);
    return index < KeyCount && m_KeyReleased[index];
}

bool Input::IsMouseButtonDown(MouseButton button) const
{
    const size_t index = static_cast<size_t>(button);
    return index < ButtonCount && m_ButtonDown[index];
}

bool Input::WasMouseButtonPressed(MouseButton button) const
{
    const size_t index = static_cast<size_t>(button);
    return index < ButtonCount && m_ButtonPressed[index];
}

bool Input::WasMouseButtonReleased(MouseButton button) const
{
    const size_t index = static_cast<size_t>(button);
    return index < ButtonCount && m_ButtonReleased[index];
}

} // namespace opane
