# Hosting a world

opane can show a world, such as a ludifex `World3D` or `World2D`, under the
interface or inside a viewport in your layout. It does this without depending
on ludifex: neither library includes the other's headers. A world is anything
with these four methods:

```cpp
void  Update(float seconds);
void  SetRenderSize(int width, int height);
void  Render();
void* GetRenderTarget();   // an SDL_GPUTexture*
```

A ludifex world has them, and so can a renderer of your own. `HooksFor` turns
any object with these methods into a `WorldHooks` of four function pointers at
compile time; that is the only connection between the two libraries.

## Filling the window

```cpp
opane::App app = opane::StartApp({ .Title = "Game" });
ludifex::World3D world = ludifex::CreateWorld3D();

app.SetMainWorld(world);
app.Run();
```

Every frame the world is sized to the window, advanced, drawn, and shown
beneath the interface, so every element (and anything your frame function
paints) appears on top of it. With `Run(callback)`, the world is advanced after
your function, so input your function turned into forces is simulated in the
same frame. `ClearMainWorld()` removes it. The world must stay alive for as
long as the loop that shows it.

## Inside a layout

```cpp
opane::Viewport* viewport = root->Add<opane::Viewport>();
viewport->Size = opane::Size2{ opane::Dim{ 1.0f, -280.0f }, opane::Dim::FromScale(1.0f) };
viewport->SetWorld(world);

// Pointer events arrive with Position converted to 0..1 across the viewport,
// which is what World3D::PickFromView expects.
viewport->OnViewEvent = [&](const opane::Event& viewEvent) {
    if (viewEvent.Type == opane::EventType::PointerDown)
    {
        ludifex::RayHit hit = world.PickFromView(viewEvent.Position.X, viewEvent.Position.Y);
        if (hit.Hit) { /* ... */ }
    }
};
```

The viewport sizes the world's image to itself, so resizing the window or the
layout resizes the world's image too. It advances and draws the world every
frame and shows it, clipped and ordered like any other element. The world is
drawn at the viewport's size in pixels, so it stays sharp at any
[interface scale](interface-scale.md).

Other `Viewport` members:

| Member | Meaning |
|---|---|
| `UpdatesWorld` | Whether the viewport advances its world each frame. Turn it off when something else already does, such as a second viewport showing the same world, or the world would be stepped twice. |
| `EmptyColor` | Drawn while there is no world or texture. |
| `DrawBorder` | A thin border in the theme's colour. |
| `Texture` | Show any texture instead of a world. |
| `ToNormalized(point)` | Converts a window position to 0..1 across the viewport. |
| `ClearWorld()` | Stops showing the world. |

## Writing the loop yourself

```cpp
while (app.IsOpen())
{
    const float delta = app.PollEvents();

    world.Update(delta);
    app.RenderWorld(world);   // sizes it, draws it, and records it beneath what follows
    app.UpdateInterface(delta);

    app.BeginFrame();
    app.EndFrame();
}
```

Call `RenderWorld` between `PollEvents` and `BeginFrame`, before anything that
should appear on top. It does not advance the world; that is up to you.

## How the two libraries connect

There is one GPU device, and it belongs to opane. `StartApp` publishes the
device, the window, and its frame loop through SDL's global properties, which
both libraries can read because they link the same SDL:

| Property | Holds |
|---|---|
| `host.gpu_device` | `SDL_GPUDevice*` |
| `host.window` | `SDL_Window*` |
| `host.interface` | a small versioned struct with the frame loop, so `world.Run()` can run inside this window |
| `host.audio` | a way to add a source to opane's mix, so a world's sound shares the one audio device |
| `host.device_release` | set by the world: a function opane calls in `Shutdown`, before destroying the device |

This is why ludifex needs no setup call after `StartApp`, and why `world.Run()`
runs inside opane's window, beneath its interface, instead of opening a second
window. It is also why calling `Shutdown` while a world still exists is safe:
the world releases its GPU objects while the device still exists, and becomes
inert.

## Connecting by hand

To connect things yourself, for example to use a device from another engine,
pass the handles directly:

```cpp
ludifex::AdoptHost({ app.GetGpuDevice(), app.GetWindow() });
opane::TextureId view = app.WrapExternalTexture(world.GetRenderTarget(), width, height);
drawList.DrawTexture({ 0, 0, windowSize.X, windowSize.Y }, view);
```

A world's render target keeps its address until its size changes, so wrapping
it again after each resize is enough.

## Limitations

- One main world at a time. Any number of viewports can each show a world.
- The world is drawn beneath the interface or inside a viewport. It cannot be
  drawn on top of elements, except by putting those elements behind the
  viewport in the tree.
- Both libraries must link the same SDL3 library for the automatic connection
  to work.
