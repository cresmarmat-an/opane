// The host handshake: how a guest library such as ludifex finds opane's GPU
// device, window, and frame loop without either library including the other's
// headers.
//
// Everything travels through SDL's global property set, which both libraries
// already share because they link the same SDL. The structs below are plain C
// layouts; ludifex declares identical copies on its side, and the Version
// field guards the layout. Not installed and not part of the public API.
//
//   "host.gpu_device"      SDL_GPUDevice*    set by the host
//   "host.window"          SDL_Window*       set by the host
//   "host.interface"       HostInterface*    set by the host
//   "host.audio"           HostAudio*        set by the host when audio runs
//   "host.device_release"  DeviceReleaseHook* set by the guest, called by the
//                                             host before the device is destroyed

#pragma once

#include <cstdint>

namespace opane::detail
{

constexpr const char* HostDeviceProperty = "host.gpu_device";
constexpr const char* HostWindowProperty = "host.window";
constexpr const char* HostInterfaceProperty = "host.interface";
constexpr const char* HostDeviceReleaseProperty = "host.device_release";

constexpr uint32_t HostProtocolVersion = 1;

// A world the host can run, as four calls over an opaque pointer.
struct HostWorldHooks
{
    uint32_t Version;
    void* World;
    void (*Update)(void* world, float seconds);
    void (*SetRenderSize)(void* world, int width, int height);
    void (*Render)(void* world);
    void* (*GetRenderTarget)(void* world);
};

struct HostInterface
{
    uint32_t Version;
    void* Context;

    // Runs the host's frame loop with this world shown beneath its interface,
    // and returns when the window closes.
    void (*RunWorld)(void* context, const HostWorldHooks* world);
};

// The host's audio output, offered to a guest so the process has one device
// and one mix. A guest adds a source: the host calls Read on its audio thread
// for exactly the frames it is about to mix, interleaved 32-bit float at the
// host's rate and channel count.
constexpr const char* HostAudioProperty = "host.audio";

struct HostAudio
{
    uint32_t Version;
    void* Context;
    uint32_t SampleRate;
    uint32_t Channels;

    // Returns an id for RemoveSource, or -1 when the source could not be added.
    int (*AddSource)(void* context, void (*read)(void* user, float* frames, uint32_t frameCount), void* user);
    void (*RemoveSource)(void* context, int source);
};

// Registered by a guest that holds GPU objects made on the host's device, so
// it can release them while the device still exists.
struct DeviceReleaseHook
{
    uint32_t Version;
    void* Context;
    void (*Release)(void* context);
};

} // namespace opane::detail
