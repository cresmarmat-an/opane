// Internal declarations shared between opane translation units.
// Not installed and not part of the public API.

#pragma once

#include <opane/opane.h>

#include <SDL3/SDL.h>

namespace opane::detail
{

// Maps an opane key to its SDL scancode, and back. Unmapped values resolve to
// SDL_SCANCODE_UNKNOWN and Key::Unknown respectively.
SDL_Scancode ToScancode(Key key);
Key FromScancode(SDL_Scancode scancode);

struct AppState;

// Turns the platform's text input on or off. The element tree calls this as
// focus moves, so a field that accepts typing gets composed characters and an
// on-screen keyboard where the platform has one.
void SetTextInputActive(AppState* app, bool active);

// Shows a pointer shape. The tree calls this as the pointer moves over, or
// drags, an element with a cursor of its own.
void SetCursorShape(AppState* app, CursorShape shape);

// Where an eased movement is at t, from 0 to 1.
float ApplyEasing(Easing easing, float t);

// The window icon opane_set_app_icon builds into a program, handed over by
// the code it generates before main runs. The PNG's bytes live as long as the
// program.
void SetEmbeddedAppIcon(const unsigned char* png, size_t size);

// Asked by the platform what a press on a frameless window means.
SDL_HitTestResult SDLCALL WindowHitTest(SDL_Window* window, const SDL_Point* area, void* data);

} // namespace opane::detail
