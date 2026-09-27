# Graphics backends

opane draws through SDL's GPU API, which runs on three graphics APIs:
Direct3D 12, Vulkan, and Metal. By default the choice is automatic: the first
backend the machine can run, tried in the order Metal, Direct3D 12, Vulkan.
In practice a Windows PC uses Direct3D 12 (or Vulkan where Direct3D 12 is
missing), a Mac uses Metal, and Linux uses Vulkan. Only backends this build of
opane has shaders for are tried; see
[Installation](../getting-started/installation.md#requirements).

## Choosing a backend

Name a backend to use it and no other:

```cpp
opane::App app = opane::StartApp({ .Title = "Editor", .Backend = opane::GraphicsBackend::Vulkan });
```

A named backend is strict. When the machine cannot run it, `StartApp` fails
and the log says why, instead of silently falling back to another one.

To build a settings screen, ask which backends would work:

```cpp
for (opane::GraphicsBackend backend :
     { opane::GraphicsBackend::Direct3D12, opane::GraphicsBackend::Vulkan, opane::GraphicsBackend::Metal })
{
    if (opane::IsGraphicsBackendAvailable(backend))
    {
        // offer opane::GetGraphicsBackendName(backend): "Direct3D 12", "Vulkan", or "Metal"
    }
}
```

`IsGraphicsBackendAvailable` is cheap enough for a settings screen but should
not be called every frame. After `StartApp`, `app.GetGraphicsBackend()` tells
you which backend is in use; it never returns `Automatic`.

The backend is fixed when the device is created, so a change takes effect the
next time the program starts: save the choice and pass it to `StartApp`.

## Trying another backend without rebuilding

Set the `SDL_GPU_DRIVER` environment variable to `direct3d12`, `vulkan`, or
`metal`. It changes the automatic choice. A backend named in `AppConfig`
ignores it.

## Materials on every backend

Everything behaves the same on each backend, including custom shaders. A
material compiled with `opane_add_material` carries a version for every
backend the build targets, and one created from a `ShaderPath` is compiled at
runtime for whichever backend is running. See
[Custom shaders](../extending/custom-shaders.md).

## Validation

`AppConfig::DebugGpu = true` turns on the graphics API's validation layer when
it is installed. It is slow and meant for tracking down GPU errors during
development.

## Limitations

- The backend cannot be changed while the program runs.
- Metal is built on macOS but has not been run on Apple hardware yet.
- There is no OpenGL, WebGPU, or software backend. A machine without
  Direct3D 12, Vulkan, or Metal support cannot run opane.
