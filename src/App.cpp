#include "Assets.h"
#include "Audio.h"
#include "Backend.h"
#include "Host.h"
#include "Images.h"
#include "RenderTargets.h"
#include "Renderer.h"
#include "Text.h"
#include "ThemeFile.h"
#include "Ui.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <unordered_map>

namespace opane
{
namespace detail
{

// One application per process. This is the "one device, one engine, one
// scheduler" rule from the design: a second device would create an entire class
// of ownership and submission-order bugs for nothing gained.
struct AppState
{
    SDL_Window* Window = nullptr;
    SDL_GPUDevice* Device = nullptr;

    SDL_GPUCommandBuffer* CommandBuffer = nullptr;
    SDL_GPURenderPass* RenderPass = nullptr;
    SDL_GPUTexture* SwapchainTexture = nullptr;

    Color ClearColor;
    Input InputState;

    Renderer RendererState;
    DrawList FrameDrawList;
    FontStore Fonts;
    FontId DefaultFont;
    UiTree Tree;
    MaterialStore Materials;
    AudioEngine Audio;
    RenderTargetStore RenderTargets;

    // Loaded images, keyed by the name they were asked for, so the same file
    // is decoded and uploaded once however many elements show it.
    std::unordered_map<std::string, TextureId> LoadedTextures;

    // Shared by every image that could not be loaded.
    TextureId MissingTexture;

    // A world drawn beneath the interface, filling the window.
    WorldHooks MainWorld;

    // The texture a full-window world last drew into, wrapped once per change
    // of size rather than every frame.
    TextureId WorldTexture;
    void* WorldTarget = nullptr;
    int WorldWidth = 0;
    int WorldHeight = 0;

    // Published through SDL's global properties so a guest library can find
    // this application's device, window, frame loop, and audio output.
    HostInterface Host{};
    HostAudio AudioHost{};

    // Sizes a full-window world, wraps its texture when that changed, and
    // records it as the first thing in this frame's draw list.
    void PrepareWorld(const WorldHooks& hooks);

    // Makes the canvas the window's size, or keeps it; false when it cannot.
    bool EnsureCanvas();

    // Where the frame is drawn when it has frosted glass, which has to read
    // back what is beneath it, and the window's own image cannot be read.
    // Copied to the window when the frame ends.
    SDL_GPUTexture* Canvas = nullptr;
    int CanvasWidth = 0;
    int CanvasHeight = 0;
    bool DrawingToCanvas = false;

    // A theme file, watched for changes: the theme it was first read over,
    // so a key taken out of the file goes back to what it was.
    std::string ThemePath;
    Theme ThemeBase;
    bool ThemeHotReload = false;
    int64_t ThemeStamp = 0;
    float ThemeCheck = 0.0f;

    // A frameless window's edges resize it by this many units.
    bool Borderless = false;
    float ResizeBorder = 6.0f;

    bool Open = false;
    bool FrameActive = false;

    int PixelWidth = 0;
    int PixelHeight = 0;

    // Window pixels per window unit. The platform reports the pointer in
    // window units and the interface is laid out in pixels, so every pointer
    // position is multiplied by this on the way in. Without it, clicks land in
    // the wrong place on a scaled display.
    float PixelDensity = 1.0f;

    // Pixels per interface unit, and whether it follows the display or was
    // chosen by the program.
    float UiScale = 1.0f;
    bool UiScaleFollowsDisplay = true;
    bool HighDpi = true;
    void RefreshUiScale();

    // The system's pointer shapes, created on first use and kept.
    SDL_Cursor* Cursors[8] = {};
    void RefreshDensity();

    uint64_t FrameCount = 0;
    uint64_t StartCounter = 0;
    uint64_t LastCounter = 0;
    double CounterFrequency = 1.0;

    void BeginInputFrame();
    void ApplyEvent(const SDL_Event& event);
    void BeginDrawList();
    void RenderOffscreen();
};

void SetCursorShape(AppState* app, CursorShape shape)
{
    if (app == nullptr)
    {
        return;
    }

    const size_t index = static_cast<size_t>(shape);
    if (index >= std::size(app->Cursors))
    {
        return;
    }

    if (app->Cursors[index] == nullptr)
    {
        SDL_SystemCursor system = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (shape)
        {
            case CursorShape::Default:          system = SDL_SYSTEM_CURSOR_DEFAULT; break;
            case CursorShape::Pointer:          system = SDL_SYSTEM_CURSOR_POINTER; break;
            case CursorShape::Text:             system = SDL_SYSTEM_CURSOR_TEXT; break;
            case CursorShape::ResizeHorizontal: system = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case CursorShape::ResizeVertical:   system = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case CursorShape::ResizeDiagonal:   system = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
            case CursorShape::Move:             system = SDL_SYSTEM_CURSOR_MOVE; break;
            case CursorShape::NotAllowed:       system = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        }
        app->Cursors[index] = SDL_CreateSystemCursor(system);
    }

    if (app->Cursors[index] != nullptr)
    {
        SDL_SetCursor(app->Cursors[index]);
    }
}

void AppState::RefreshDensity()
{
    const float density = Window != nullptr ? SDL_GetWindowPixelDensity(Window) : 0.0f;
    PixelDensity = density > 0.0f ? density : 1.0f;
    RefreshUiScale();
}

void AppState::RefreshUiScale()
{
    if (UiScaleFollowsDisplay)
    {
        // The display's own scale, which already counts pixel density: 2 on a
        // Retina screen, 1.5 on a laptop set to 150%. Without high-density
        // windows the platform does the scaling itself, and this stays at 1.
        const float display = (HighDpi && Window != nullptr) ? SDL_GetWindowDisplayScale(Window) : 1.0f;
        UiScale = display > 0.0f ? display : 1.0f;
    }
    Fonts.SetUiScale(UiScale);
}

void SetTextInputActive(AppState* app, bool active)
{
    if (app == nullptr || app->Window == nullptr)
    {
        return;
    }

    if (active)
    {
        SDL_StartTextInput(app->Window);
    }
    else
    {
        SDL_StopTextInput(app->Window);
    }
}

void AppState::RenderOffscreen()
{
    // Size the geometry buffers once for the largest list about to upload, so
    // no upload has to reallocate part-way through the frame.
    size_t largestVertices = FrameDrawList.m_Vertices.size();
    size_t largestIndices = FrameDrawList.m_Indices.size();
    RenderTargets.GetLargestList(largestVertices, largestIndices);
    RendererState.Reserve(largestVertices, largestIndices);

    RenderTargets.RenderAll(CommandBuffer, Materials);
}

void AppState::PrepareWorld(const WorldHooks& hooks)
{
    if (!hooks.IsValid() || PixelWidth <= 0 || PixelHeight <= 0)
    {
        return;
    }

    if (PixelWidth != WorldWidth || PixelHeight != WorldHeight)
    {
        hooks.SetRenderSize(hooks.Object, PixelWidth, PixelHeight);
        WorldWidth = PixelWidth;
        WorldHeight = PixelHeight;
    }

    // The target changes only when its size does, so comparing the pointer is
    // enough to know when to wrap it again.
    void* target = hooks.GetRenderTarget(hooks.Object);
    if (target != WorldTarget)
    {
        if (WorldTexture.IsValid())
        {
            RendererState.DestroyTexture(WorldTexture);
            WorldTexture = TextureId{};
        }
        if (target != nullptr)
        {
            WorldTexture = RendererState.AdoptTexture(static_cast<SDL_GPUTexture*>(target), WorldWidth, WorldHeight);
        }
        WorldTarget = target;
    }

    if (WorldTexture.IsValid())
    {
        // In interface units, like everything else in the list; the list
        // scales it back to exactly the window's pixels.
        FrameDrawList.DrawTexture(Rect{ 0.0f, 0.0f, static_cast<float>(PixelWidth) / UiScale,
                                        static_cast<float>(PixelHeight) / UiScale },
                                  WorldTexture);
    }
}

bool AppState::EnsureCanvas()
{
    if (Canvas != nullptr && CanvasWidth == PixelWidth && CanvasHeight == PixelHeight)
    {
        return true;
    }
    if (Canvas != nullptr)
    {
        SDL_ReleaseGPUTexture(Device, Canvas);
        Canvas = nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = RendererState.GetTargetFormat();
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = static_cast<uint32_t>(std::max(PixelWidth, 1));
    info.height = static_cast<uint32_t>(std::max(PixelHeight, 1));
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    Canvas = SDL_CreateGPUTexture(Device, &info);
    if (Canvas == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not make the canvas frosted glass is drawn on: %s",
                   SDL_GetError());
        return false;
    }
    CanvasWidth = static_cast<int>(info.width);
    CanvasHeight = static_cast<int>(info.height);
    return true;
}

void AppState::BeginDrawList()
{
    // The frame's painting starts empty. Anything recorded between here and
    // BeginFrame is what gets drawn.
    FrameDrawList.Clear();
    FrameDrawList.m_Viewport =
        Rect{ 0.0f, 0.0f, static_cast<float>(PixelWidth), static_cast<float>(PixelHeight) };
    FrameDrawList.m_Fonts = &Fonts;
    FrameDrawList.m_Renderer = &RendererState;
    FrameDrawList.m_Scale = UiScale;
}

void AppState::BeginInputFrame()
{
    // Edge-triggered state lives for exactly one frame. Level-triggered state
    // persists until the matching release event arrives.
    for (bool& value : InputState.m_KeyPressed)
    {
        value = false;
    }
    for (bool& value : InputState.m_KeyReleased)
    {
        value = false;
    }
    for (bool& value : InputState.m_ButtonPressed)
    {
        value = false;
    }
    for (bool& value : InputState.m_ButtonReleased)
    {
        value = false;
    }

    InputState.m_MouseDelta = Vec2{};
    InputState.m_ScrollDelta = 0.0f;
}

void AppState::ApplyEvent(const SDL_Event& event)
{
    switch (event.type)
    {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            Open = false;
            break;
        }

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        {
            SDL_GetWindowSizeInPixels(Window, &PixelWidth, &PixelHeight);
            RefreshDensity();
            break;
        }

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            Tree.HandlePointerLeave();
            break;
        }

        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            // Keys held when the window lost the keyboard would otherwise stay
            // held forever, since their releases go to another window.
            for (size_t index = 0; index < std::size(InputState.m_KeyDown); ++index)
            {
                if (InputState.m_KeyDown[index])
                {
                    InputState.m_KeyDown[index] = false;
                    InputState.m_KeyReleased[index] = true;
                }
            }
            for (size_t index = 0; index < std::size(InputState.m_ButtonDown); ++index)
            {
                if (InputState.m_ButtonDown[index])
                {
                    InputState.m_ButtonDown[index] = false;
                    InputState.m_ButtonReleased[index] = true;
                }
            }
            Tree.HandleFocusLost();
            break;
        }

        case SDL_EVENT_KEY_DOWN:
        {
            const Key key = FromScancode(event.key.scancode);
            const size_t index = static_cast<size_t>(key);
            if (key == Key::Unknown)
            {
                break;
            }

            const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            const bool control = (event.key.mod & SDL_KMOD_CTRL) != 0;
            const bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;

            // A repeat is a held key, not a new press: editing acts on it, but
            // WasKeyPressed stays true only for the frame the key went down.
            if (!event.key.repeat)
            {
                InputState.m_KeyDown[index] = true;
                InputState.m_KeyPressed[index] = true;
            }
            Tree.HandleKey(key, true, shift, control, alt, event.key.repeat);
            break;
        }

        case SDL_EVENT_KEY_UP:
        {
            const Key key = FromScancode(event.key.scancode);
            const size_t index = static_cast<size_t>(key);
            if (key != Key::Unknown)
            {
                const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                const bool control = (event.key.mod & SDL_KMOD_CTRL) != 0;
                const bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;
                InputState.m_KeyDown[index] = false;
                InputState.m_KeyReleased[index] = true;
                Tree.HandleKey(key, false, shift, control, alt, false);
            }
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
        {
            // Already composed by the platform, so an accented letter or a
            // character entered through an input method arrives whole.
            if (event.text.text != nullptr)
            {
                Tree.HandleText(event.text.text);
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION:
        {
            const float toUnits = PixelDensity / UiScale;
            InputState.m_MousePosition = Vec2{ event.motion.x * toUnits, event.motion.y * toUnits };
            InputState.m_MouseDelta.X += event.motion.xrel * toUnits;
            InputState.m_MouseDelta.Y += event.motion.yrel * toUnits;
            Tree.HandlePointerMove(InputState.m_MousePosition);
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            size_t index = static_cast<size_t>(MouseButton::Count);
            switch (event.button.button)
            {
                case SDL_BUTTON_LEFT:   index = static_cast<size_t>(MouseButton::Left); break;
                case SDL_BUTTON_MIDDLE: index = static_cast<size_t>(MouseButton::Middle); break;
                case SDL_BUTTON_RIGHT:  index = static_cast<size_t>(MouseButton::Right); break;
                default: break;
            }

            if (index < static_cast<size_t>(MouseButton::Count))
            {
                const bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                InputState.m_ButtonDown[index] = down;
                if (down)
                {
                    InputState.m_ButtonPressed[index] = true;
                }
                else
                {
                    InputState.m_ButtonReleased[index] = true;
                }

                const float toUnits = PixelDensity / UiScale;
                InputState.m_MousePosition = Vec2{ event.button.x * toUnits, event.button.y * toUnits };
                Tree.HandlePointerButton(InputState.m_MousePosition,
                                         static_cast<MouseButton>(index), down);
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
        {
            InputState.m_ScrollDelta += event.wheel.y;
            Tree.HandleWheel(InputState.m_MousePosition, event.wheel.y);
            break;
        }

        default:
            break;
    }
}

namespace
{
AppState g_State;
bool g_Started = false;

// The picture opane_set_app_icon built into the program, handed over before
// main runs. Held in a function so it is there whatever order static
// initialization takes.
struct EmbeddedIcon
{
    const unsigned char* Png = nullptr;
    size_t Size = 0;
};

EmbeddedIcon& GetEmbeddedIcon()
{
    static EmbeddedIcon icon;
    return icon;
}

// Decodes an image and puts it on the window. what names it in the log.
bool ApplyWindowIcon(SDL_Window* window, const uint8_t* bytes, size_t size, const char* what)
{
    DecodedImage image;
    std::string error;
    if (!DecodeImage(bytes, size, image, error))
    {
        LogMessage(LogLevel::Error, "app", "Could not decode the icon %s: %s", what, error.c_str());
        return false;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(image.Width, image.Height, SDL_PIXELFORMAT_RGBA32,
                                                 image.Pixels.data(), image.Width * 4);
    const bool applied = surface != nullptr && SDL_SetWindowIcon(window, surface);
    if (!applied)
    {
        LogMessage(LogLevel::Error, "app", "Could not set the window's icon from %s: %s", what, SDL_GetError());
    }
    SDL_DestroySurface(surface);
    return applied;
}
} // namespace

void SetEmbeddedAppIcon(const unsigned char* png, size_t size)
{
    GetEmbeddedIcon() = EmbeddedIcon{ png, size };
}

SDL_HitTestResult SDLCALL WindowHitTest(SDL_Window* window, const SDL_Point* area, void* data)
{
    auto* state = static_cast<AppState*>(data);
    if (state == nullptr || area == nullptr)
    {
        return SDL_HITTEST_NORMAL;
    }

    // The platform asks in window units; the interface is in its own.
    const float toUnits = state->PixelDensity / std::max(state->UiScale, 1e-3f);
    const Vec2 point{ static_cast<float>(area->x) * toUnits, static_cast<float>(area->y) * toUnits };

    // The edges resize, while the window is not filling the screen.
    const bool maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    const bool resizable = (SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) != 0;
    if (resizable && !maximized && state->ResizeBorder > 0.0f)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        const float border = state->ResizeBorder / toUnits;
        const bool left = area->x < border;
        const bool right = area->x >= width - border;
        const bool top = area->y < border;
        const bool bottom = area->y >= height - border;
        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
    }

    switch (state->Tree.RegionAt(point))
    {
    case WindowRegion::Drag: return SDL_HITTEST_DRAGGABLE;
    case WindowRegion::ResizeTop: return SDL_HITTEST_RESIZE_TOP;
    case WindowRegion::ResizeBottom: return SDL_HITTEST_RESIZE_BOTTOM;
    case WindowRegion::ResizeLeft: return SDL_HITTEST_RESIZE_LEFT;
    case WindowRegion::ResizeRight: return SDL_HITTEST_RESIZE_RIGHT;
    case WindowRegion::ResizeTopLeft: return SDL_HITTEST_RESIZE_TOPLEFT;
    case WindowRegion::ResizeTopRight: return SDL_HITTEST_RESIZE_TOPRIGHT;
    case WindowRegion::ResizeBottomLeft: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    case WindowRegion::ResizeBottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    default: return SDL_HITTEST_NORMAL;
    }
}

int HostAddAudioSource(void* context, void (*read)(void* user, float* frames, uint32_t frameCount), void* user)
{
    return static_cast<AppState*>(context)->Audio.AddExternalSource(read, user);
}

void HostRemoveAudioSource(void* context, int source)
{
    static_cast<AppState*>(context)->Audio.RemoveExternalSource(source);
}

// A guest calls this to show its world in this application's window: the
// world becomes the main world for one run of the loop.
void HostRunWorld(void* context, const HostWorldHooks* world)
{
    auto* state = static_cast<AppState*>(context);
    if (state == nullptr || world == nullptr || world->Version != HostProtocolVersion)
    {
        return;
    }

    WorldHooks hooks;
    hooks.Object = world->World;
    hooks.Update = world->Update;
    hooks.SetRenderSize = world->SetRenderSize;
    hooks.Render = world->Render;
    hooks.GetRenderTarget = world->GetRenderTarget;

    App app = MakeAppView(state);
    app.SetMainWorld(hooks);
    app.Run();

    // The world belongs to the caller and goes out of scope after this
    // returns, so it must not stay registered.
    app.ClearMainWorld();
}

App MakeAppView(AppState* state)
{
    App app;
    app.m_State = state;
    return app;
}

} // namespace detail

App StartApp(const AppConfig& config)
{
    App app;

    if (detail::g_Started)
    {
        LogMessage(LogLevel::Error, "app",
                   "StartApp called twice. opane supports one application per process; "
                   "reuse the App returned by the first call.");
        return app;
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        LogMessage(LogLevel::Error, "app", "SDL_Init failed: %s", SDL_GetError());
        return app;
    }

    detail::AppState& state = detail::g_State;

    SDL_WindowFlags flags = 0;
    if (config.Resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.HighDpi)
    {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (config.Hidden)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (config.Borderless)
    {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    state.Borderless = config.Borderless;
    state.ResizeBorder = config.ResizeBorder;

    state.Window = SDL_CreateWindow(config.Title.c_str(), config.Width, config.Height, flags);
    if (state.Window == nullptr)
    {
        LogMessage(LogLevel::Error, "app", "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return app;
    }

    // A frameless window asks the interface what each press means: move the
    // window, resize it, or neither.
    if (config.Borderless)
    {
        SDL_SetWindowHitTest(state.Window, detail::WindowHitTest, &state);
    }

    // Without either, the window keeps what the platform gives it (on
    // Windows, the .exe's own icon).
    if (!config.Icon.empty())
    {
        detail::MakeAppView(&state).SetIcon(config.Icon);
    }
    else if (const detail::EmbeddedIcon& icon = detail::GetEmbeddedIcon(); icon.Png != nullptr)
    {
        detail::ApplyWindowIcon(state.Window, icon.Png, icon.Size, "built into the program");
    }

    // Automatic takes the first backend this machine runs among those opane
    // has shaders for; anything else is that backend or nothing.
    state.Device = detail::CreateDevice(config.Backend, config.DebugGpu);
    if (state.Device == nullptr)
    {
        SDL_DestroyWindow(state.Window);
        state.Window = nullptr;
        SDL_Quit();
        return app;
    }

    {
        const SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(state.Device);
        const char* gpuName = SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "an unnamed GPU");
        LogMessage(LogLevel::Info, "gpu", "Drawing with %s on %s.",
                   GetGraphicsBackendName(detail::DeviceBackend(state.Device)), gpuName);
    }

    if (!SDL_ClaimWindowForGPUDevice(state.Device, state.Window))
    {
        LogMessage(LogLevel::Error, "gpu", "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(state.Device);
        state.Device = nullptr;
        SDL_DestroyWindow(state.Window);
        state.Window = nullptr;
        SDL_Quit();
        return app;
    }

    const SDL_GPUPresentMode desiredPresentMode =
        config.VSync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;

    SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    if (SDL_WindowSupportsGPUPresentMode(state.Device, state.Window, desiredPresentMode))
    {
        presentMode = desiredPresentMode;
    }
    else if (desiredPresentMode != SDL_GPU_PRESENTMODE_VSYNC)
    {
        LogMessage(LogLevel::Warning, "gpu",
                   "Immediate present mode is unsupported on this device; falling back to vsync.");
    }

    SDL_SetGPUSwapchainParameters(state.Device, state.Window,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode);

    SDL_GetWindowSizeInPixels(state.Window, &state.PixelWidth, &state.PixelHeight);
    state.HighDpi = config.HighDpi;
    state.UiScaleFollowsDisplay = !(config.UiScale > 0.0f);
    if (!state.UiScaleFollowsDisplay)
    {
        state.UiScale = std::clamp(config.UiScale, 0.25f, 8.0f);
    }

    if (!state.RendererState.Initialize(state.Device, state.Window))
    {
        LogMessage(LogLevel::Error, "gpu", "The interface renderer could not start.");
        SDL_ReleaseWindowFromGPUDevice(state.Device, state.Window);
        SDL_DestroyGPUDevice(state.Device);
        state.Device = nullptr;
        SDL_DestroyWindow(state.Window);
        state.Window = nullptr;
        SDL_Quit();
        return app;
    }

    state.Materials.Initialize(&state.RendererState);
    state.Fonts.Initialize(&state.RendererState);
    // The scale is settled before the first font loads, so the default font
    // is rasterized for the screen it will be on.
    state.RefreshDensity();
    state.DefaultFont = state.Fonts.LoadDefault(16.0f);
    state.RenderTargets.Initialize(&state.RendererState, &state.Fonts);
    state.Tree.Initialize(Theme::Dark(state.DefaultFont), &state.Fonts, &state.RenderTargets, &state);

    // Audio failing is not fatal. A machine with no device still runs the
    // program, silently.
    state.Audio.Initialize();

    state.ClearColor = config.ClearColor;
    state.Open = true;
    state.FrameActive = false;
    state.FrameCount = 0;
    state.CounterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());
    state.StartCounter = SDL_GetPerformanceCounter();
    state.LastCounter = state.StartCounter;

    detail::g_Started = true;

    // Anything that links the same SDL can now find this device, window, and
    // frame loop. ludifex uses this to render into the window without either
    // library including the other's headers.
    state.Host.Version = detail::HostProtocolVersion;
    state.Host.Context = &state;
    state.Host.RunWorld = &detail::HostRunWorld;

    const SDL_PropertiesID globals = SDL_GetGlobalProperties();
    SDL_SetPointerProperty(globals, detail::HostDeviceProperty, state.Device);
    SDL_SetPointerProperty(globals, detail::HostWindowProperty, state.Window);
    SDL_SetPointerProperty(globals, detail::HostInterfaceProperty, &state.Host);

    // One audio device for the process: a guest mixes into this engine's
    // output rather than opening one of its own.
    if (state.Audio.IsRunning())
    {
        state.AudioHost.Version = detail::HostProtocolVersion;
        state.AudioHost.Context = &state;
        state.AudioHost.SampleRate = state.Audio.GetSampleRate();
        state.AudioHost.Channels = state.Audio.GetChannels();
        state.AudioHost.AddSource = &detail::HostAddAudioSource;
        state.AudioHost.RemoveSource = &detail::HostRemoveAudioSource;
        SDL_SetPointerProperty(globals, detail::HostAudioProperty, &state.AudioHost);
    }

    LogMessage(LogLevel::Info, "app", "Started \"%s\" at %dx%d using the %s GPU backend.",
               config.Title.c_str(), state.PixelWidth, state.PixelHeight,
               SDL_GetGPUDeviceDriver(state.Device));

    app.m_State = &state;
    return app;
}

bool App::IsOpen() const
{
    return m_State != nullptr && m_State->Open;
}

float App::PollEvents()
{
    if (m_State == nullptr)
    {
        return 0.0f;
    }

    m_State->BeginInputFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        m_State->ApplyEvent(event);
    }

    m_State->BeginDrawList();

    const uint64_t now = SDL_GetPerformanceCounter();
    const double elapsed = static_cast<double>(now - m_State->LastCounter) / m_State->CounterFrequency;
    m_State->LastCounter = now;

    // A delta this large means the process was suspended or sitting in a
    // debugger. Passing it downstream would make physics take a huge jump, so
    // it is clamped here rather than in every consumer.
    constexpr double MaxDelta = 0.25;
    return static_cast<float>(std::min(elapsed, MaxDelta));
}

void App::BeginFrame()
{
    if (m_State == nullptr || m_State->FrameActive)
    {
        return;
    }

    m_State->CommandBuffer = SDL_AcquireGPUCommandBuffer(m_State->Device);
    if (m_State->CommandBuffer == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_State->CommandBuffer, m_State->Window,
                                               &m_State->SwapchainTexture, &width, &height))
    {
        LogMessage(LogLevel::Warning, "gpu", "Swapchain acquire failed: %s", SDL_GetError());
    }

    m_State->FrameActive = true;

    // A null swapchain texture is normal while the window is minimized. The
    // command buffer still has to be submitted, which EndFrame does.
    if (m_State->SwapchainTexture == nullptr)
    {
        return;
    }

    m_State->PixelWidth = static_cast<int>(width);
    m_State->PixelHeight = static_cast<int>(height);

    // Glyphs rasterized while painting reach the GPU before anything that
    // names them is drawn.
    m_State->Fonts.UploadPending();

    // Render-target elements and their post-process chains finish first, so
    // the main pass can sample their results.
    m_State->RenderOffscreen();

    // Geometry has to reach the GPU before the render pass opens, because a
    // copy pass cannot run inside one.
    m_State->RendererState.Upload(m_State->CommandBuffer, m_State->FrameDrawList);

    // Frosted glass reads back what is beneath it, which the window's own
    // image does not allow, so a frame that has any is drawn to a canvas and
    // copied to the window at the end.
    m_State->DrawingToCanvas = false;
    if (m_State->RendererState.NeedsSampledTarget(m_State->FrameDrawList))
    {
        m_State->DrawingToCanvas = m_State->EnsureCanvas();
    }

    SDL_GPUColorTargetInfo targetInfo{};
    targetInfo.texture = m_State->DrawingToCanvas ? m_State->Canvas : m_State->SwapchainTexture;
    targetInfo.clear_color = SDL_FColor{ m_State->ClearColor.R, m_State->ClearColor.G,
                                         m_State->ClearColor.B, m_State->ClearColor.A };
    targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    targetInfo.store_op = SDL_GPU_STOREOP_STORE;

    m_State->RenderPass = SDL_BeginGPURenderPass(m_State->CommandBuffer, &targetInfo, 1, nullptr);

    m_State->RendererState.Render(m_State->CommandBuffer, m_State->RenderPass,
                                  m_State->DrawingToCanvas ? m_State->Canvas : nullptr, m_State->FrameDrawList,
                                  m_State->PixelWidth, m_State->PixelHeight, &m_State->Materials);
}

void App::EndFrame()
{
    if (m_State == nullptr || !m_State->FrameActive)
    {
        return;
    }

    if (m_State->RenderPass != nullptr)
    {
        SDL_EndGPURenderPass(m_State->RenderPass);
        m_State->RenderPass = nullptr;
    }

    if (m_State->DrawingToCanvas && m_State->SwapchainTexture != nullptr && m_State->CommandBuffer != nullptr)
    {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = m_State->Canvas;
        blit.source.w = static_cast<uint32_t>(m_State->CanvasWidth);
        blit.source.h = static_cast<uint32_t>(m_State->CanvasHeight);
        blit.destination.texture = m_State->SwapchainTexture;
        blit.destination.w = static_cast<uint32_t>(m_State->PixelWidth);
        blit.destination.h = static_cast<uint32_t>(m_State->PixelHeight);
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(m_State->CommandBuffer, &blit);
    }
    m_State->DrawingToCanvas = false;

    if (m_State->CommandBuffer != nullptr)
    {
        SDL_SubmitGPUCommandBuffer(m_State->CommandBuffer);
        m_State->CommandBuffer = nullptr;
    }

    m_State->SwapchainTexture = nullptr;
    m_State->FrameActive = false;
    ++m_State->FrameCount;
}

void App::Run(const std::function<void(float)>& onFrame)
{
    if (m_State == nullptr)
    {
        return;
    }

    while (IsOpen())
    {
        const float delta = PollEvents();

        // The main world goes into the draw list first, so everything the
        // callback and the interface paint lands on top of it.
        const bool hasWorld = m_State->MainWorld.IsValid();
        if (hasWorld)
        {
            m_State->PrepareWorld(m_State->MainWorld);
        }

        // The callback runs before the frame is begun so that state it changes
        // takes effect in this frame rather than the next one.
        if (onFrame)
        {
            onFrame(delta);
        }

        // Advanced after the callback, so input the callback turned into forces
        // or velocities is simulated this frame. The world submits its own
        // drawing before the interface does, so its image is ready in time.
        if (hasWorld && m_State->MainWorld.IsValid())
        {
            m_State->MainWorld.Update(m_State->MainWorld.Object, delta);
            m_State->MainWorld.Render(m_State->MainWorld.Object);
        }

        // The interface paints after the callback, so widgets land on top of
        // whatever the callback drew.
        UpdateInterface(delta);

        BeginFrame();
        EndFrame();
    }
}

void App::Run()
{
    Run(std::function<void(float)>());
}

void App::SetMainWorld(const WorldHooks& hooks)
{
    if (m_State == nullptr)
    {
        return;
    }
    if (!hooks.IsValid())
    {
        LogMessage(LogLevel::Warning, "app", "SetMainWorld was given an incomplete world; ignoring it.");
        return;
    }
    m_State->MainWorld = hooks;

    // A different world, so whatever was wrapped for the last one is stale.
    m_State->WorldWidth = 0;
    m_State->WorldHeight = 0;
}

void App::ClearMainWorld()
{
    if (m_State == nullptr)
    {
        return;
    }
    m_State->MainWorld = WorldHooks{};
    if (m_State->WorldTexture.IsValid())
    {
        m_State->RendererState.DestroyTexture(m_State->WorldTexture);
        m_State->WorldTexture = TextureId{};
    }
    m_State->WorldTarget = nullptr;
    m_State->WorldWidth = 0;
    m_State->WorldHeight = 0;
}

void App::RenderWorld(const WorldHooks& hooks)
{
    if (m_State == nullptr || !hooks.IsValid())
    {
        return;
    }
    m_State->PrepareWorld(hooks);
    hooks.Render(hooks.Object);
}

void App::Close()
{
    if (m_State != nullptr)
    {
        m_State->Open = false;
    }
}

void App::Shutdown()
{
    if (m_State == nullptr || !detail::g_Started)
    {
        return;
    }

    if (m_State->FrameActive)
    {
        EndFrame();
    }

    // A guest holding GPU objects on this device releases them now, while the
    // device still exists; afterwards nothing can find the device.
    const SDL_PropertiesID globals = SDL_GetGlobalProperties();
    if (auto* hook = static_cast<detail::DeviceReleaseHook*>(
            SDL_GetPointerProperty(globals, detail::HostDeviceReleaseProperty, nullptr)))
    {
        if (hook->Version == detail::HostProtocolVersion && hook->Release != nullptr)
        {
            hook->Release(hook->Context);
        }
    }
    SDL_ClearProperty(globals, detail::HostDeviceReleaseProperty);
    SDL_ClearProperty(globals, detail::HostDeviceProperty);
    SDL_ClearProperty(globals, detail::HostWindowProperty);
    SDL_ClearProperty(globals, detail::HostInterfaceProperty);
    SDL_ClearProperty(globals, detail::HostAudioProperty);

    m_State->MainWorld = WorldHooks{};
    m_State->WorldTexture = TextureId{};
    m_State->WorldTarget = nullptr;

    m_State->Audio.Shutdown();
    m_State->Tree.Shutdown();

    for (SDL_Cursor*& cursor : m_State->Cursors)
    {
        if (cursor != nullptr)
        {
            SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }
    }
    m_State->RenderTargets.Shutdown();
    m_State->Materials.Shutdown();
    m_State->Fonts.Shutdown();
    if (m_State->Canvas != nullptr)
    {
        SDL_ReleaseGPUTexture(m_State->Device, m_State->Canvas);
        m_State->Canvas = nullptr;
    }
    m_State->RendererState.Shutdown();

    if (m_State->Device != nullptr)
    {
        if (m_State->Window != nullptr)
        {
            SDL_ReleaseWindowFromGPUDevice(m_State->Device, m_State->Window);
        }
        SDL_DestroyGPUDevice(m_State->Device);
        m_State->Device = nullptr;
    }

    if (m_State->Window != nullptr)
    {
        SDL_DestroyWindow(m_State->Window);
        m_State->Window = nullptr;
    }

    SDL_Quit();

    m_State->Open = false;
    detail::g_Started = false;

    LogMessage(LogLevel::Info, "app", "Shut down after %llu frames.",
               static_cast<unsigned long long>(m_State->FrameCount));

    m_State = nullptr;
}

void App::SetTitle(const std::string& title)
{
    if (m_State != nullptr && m_State->Window != nullptr)
    {
        SDL_SetWindowTitle(m_State->Window, title.c_str());
    }
}

void App::MinimizeWindow()
{
    if (m_State != nullptr && m_State->Window != nullptr)
    {
        SDL_MinimizeWindow(m_State->Window);
    }
}

void App::MaximizeWindow()
{
    if (m_State != nullptr && m_State->Window != nullptr)
    {
        SDL_MaximizeWindow(m_State->Window);
    }
}

void App::RestoreWindow()
{
    if (m_State != nullptr && m_State->Window != nullptr)
    {
        SDL_RestoreWindow(m_State->Window);
    }
}

bool App::IsWindowMaximized() const
{
    return m_State != nullptr && m_State->Window != nullptr &&
           (SDL_GetWindowFlags(m_State->Window) & SDL_WINDOW_MAXIMIZED) != 0;
}

namespace
{

int64_t FileStamp(const std::string& path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

// Reads the theme file over the base theme and puts the result in place.
bool ApplyThemeFile(detail::AppState& state, App app)
{
    size_t size = 0;
    void* bytes = SDL_LoadFile(state.ThemePath.c_str(), &size);
    if (bytes == nullptr)
    {
        LogMessage(LogLevel::Error, "theme", "Could not read \"%s\": %s", state.ThemePath.c_str(), SDL_GetError());
        return false;
    }
    const std::string text(static_cast<const char*>(bytes), size);
    SDL_free(bytes);

    Theme theme = state.ThemeBase;
    std::string error;
    std::vector<std::string> warnings;
    const std::string directory = std::filesystem::path(state.ThemePath).parent_path().generic_string();
    if (!detail::ReadThemeFile(text, directory, app, theme, error, warnings))
    {
        LogMessage(LogLevel::Error, "theme", "\"%s\", %s. The theme that worked stays.", state.ThemePath.c_str(),
                   error.c_str());
        return false;
    }
    for (const std::string& warning : warnings)
    {
        LogMessage(LogLevel::Warning, "theme", "\"%s\", %s", state.ThemePath.c_str(), warning.c_str());
    }
    state.Tree.SetTheme(theme);
    return true;
}

} // namespace

bool App::LoadTheme(const std::string& path, bool hotReload)
{
    if (m_State == nullptr)
    {
        return false;
    }

    const std::string resolved = detail::ResolveAsset(path, "theme");
    if (resolved.empty())
    {
        return false;
    }

    // The base is the theme before any file, so reloading one file, or
    // loading another, starts from the same place.
    if (m_State->ThemePath.empty())
    {
        m_State->ThemeBase = m_State->Tree.GetTheme();
    }
    m_State->ThemePath = resolved;
    m_State->ThemeHotReload = hotReload;
    m_State->ThemeStamp = FileStamp(resolved);
    m_State->ThemeCheck = 0.0f;

    const bool loaded = ApplyThemeFile(*m_State, *this);
    if (loaded)
    {
        LogMessage(LogLevel::Info, "theme", "Loaded \"%s\"%s.", resolved.c_str(),
                   hotReload ? " with hot reload" : "");
    }
    return loaded;
}

bool App::SetIcon(const std::string& path)
{
    if (m_State == nullptr || m_State->Window == nullptr)
    {
        return false;
    }
    std::vector<uint8_t> bytes;
    std::string resolved;
    if (!detail::LoadAssetFile(path, "image", bytes, &resolved))
    {
        return false;
    }
    return detail::ApplyWindowIcon(m_State->Window, bytes.data(), bytes.size(), ("\"" + resolved + "\"").c_str());
}

void App::SetClearColor(Color color)
{
    if (m_State != nullptr)
    {
        m_State->ClearColor = color;
    }
}

Color App::GetClearColor() const
{
    return m_State != nullptr ? m_State->ClearColor : Color{};
}

Vec2 App::GetWindowSize() const
{
    if (m_State == nullptr)
    {
        return Vec2{};
    }
    return Vec2{ static_cast<float>(m_State->PixelWidth) / m_State->UiScale,
                 static_cast<float>(m_State->PixelHeight) / m_State->UiScale };
}

Vec2 App::GetPixelSize() const
{
    if (m_State == nullptr)
    {
        return Vec2{};
    }
    return Vec2{ static_cast<float>(m_State->PixelWidth), static_cast<float>(m_State->PixelHeight) };
}

float App::GetUiScale() const
{
    return m_State != nullptr ? m_State->UiScale : 1.0f;
}

void App::SetUiScale(float scale)
{
    if (m_State == nullptr)
    {
        return;
    }
    if (!(scale > 0.0f))
    {
        m_State->UiScaleFollowsDisplay = true;
    }
    else
    {
        // Kept to a range where the interface is still usable, so a stray
        // value cannot make it vanish or fill the screen with one button.
        m_State->UiScaleFollowsDisplay = false;
        m_State->UiScale = std::clamp(scale, 0.25f, 8.0f);
    }
    m_State->RefreshUiScale();
}

float App::GetTimeSeconds() const
{
    if (m_State == nullptr)
    {
        return 0.0f;
    }
    const uint64_t now = SDL_GetPerformanceCounter();
    return static_cast<float>(static_cast<double>(now - m_State->StartCounter) / m_State->CounterFrequency);
}

uint64_t App::GetFrameCount() const
{
    return m_State != nullptr ? m_State->FrameCount : 0;
}

const Input& App::GetInput() const
{
    static const Input empty;
    return m_State != nullptr ? m_State->InputState : empty;
}

DrawList& App::GetDrawList()
{
    static DrawList empty;
    return m_State != nullptr ? m_State->FrameDrawList : empty;
}

TextureId App::CreateTexture(int width, int height, PixelFormat format, const void* pixels)
{
    if (m_State == nullptr)
    {
        return TextureId{};
    }
    return m_State->RendererState.CreateTexture(width, height, format, pixels);
}

TextureId App::WrapExternalTexture(void* sdlGpuTexture, int width, int height)
{
    if (m_State == nullptr)
    {
        return TextureId{};
    }
    return m_State->RendererState.AdoptTexture(static_cast<SDL_GPUTexture*>(sdlGpuTexture), width,
                                               height);
}

TextureId App::LoadTexture(const std::string& path)
{
    if (m_State == nullptr)
    {
        return TextureId{};
    }

    const auto cached = m_State->LoadedTextures.find(path);
    if (cached != m_State->LoadedTextures.end())
    {
        return cached->second;
    }

    std::vector<uint8_t> bytes;
    std::string resolved;
    detail::DecodedImage image;
    bool loaded = false;

    if (detail::LoadAssetFile(path, "image", bytes, &resolved))
    {
        std::string error;
        loaded = detail::DecodeImage(bytes.data(), bytes.size(), image, error);
        if (!loaded)
        {
            LogMessage(LogLevel::Error, "image", "Could not decode \"%s\": %s", resolved.c_str(),
                       error.c_str());
        }
    }

    if (!loaded)
    {
        // A missing image still draws something, and something unmissable.
        if (!m_State->MissingTexture.IsValid())
        {
            const detail::DecodedImage checker = detail::MakeCheckerboard();
            const std::vector<std::vector<uint8_t>> levels = detail::BuildMipChain(checker);
            std::vector<const void*> pointers;
            for (const std::vector<uint8_t>& level : levels)
            {
                pointers.push_back(level.data());
            }
            m_State->MissingTexture = m_State->RendererState.CreateTextureLevels(
                checker.Width, checker.Height, PixelFormat::Rgba8, pointers);
        }
        return m_State->MissingTexture;
    }

    const std::vector<std::vector<uint8_t>> levels = detail::BuildMipChain(image);
    std::vector<const void*> pointers;
    pointers.reserve(levels.size());
    for (const std::vector<uint8_t>& level : levels)
    {
        pointers.push_back(level.data());
    }

    const TextureId texture =
        m_State->RendererState.CreateTextureLevels(image.Width, image.Height, PixelFormat::Rgba8, pointers);

    if (texture.IsValid())
    {
        m_State->LoadedTextures[path] = texture;
        LogMessage(LogLevel::Info, "image", "Loaded \"%s\" (%dx%d, %zu mip levels).", resolved.c_str(),
                   image.Width, image.Height, levels.size());
    }
    return texture;
}

void App::DestroyTexture(TextureId texture)
{
    if (m_State == nullptr)
    {
        return;
    }

    // The shared placeholder outlives any one request for it.
    if (texture.Index == m_State->MissingTexture.Index &&
        texture.Generation == m_State->MissingTexture.Generation)
    {
        return;
    }

    for (auto entry = m_State->LoadedTextures.begin(); entry != m_State->LoadedTextures.end(); ++entry)
    {
        if (entry->second.Index == texture.Index && entry->second.Generation == texture.Generation)
        {
            m_State->LoadedTextures.erase(entry);
            break;
        }
    }

    m_State->RendererState.DestroyTexture(texture);
}

Vec2 App::GetTextureSize(TextureId texture) const
{
    if (m_State == nullptr)
    {
        return Vec2{};
    }
    return m_State->RendererState.GetTextureSize(texture);
}

Element* App::GetRoot()
{
    return m_State != nullptr ? m_State->Tree.GetRoot() : nullptr;
}

Element* App::GetOverlay()
{
    return m_State != nullptr ? m_State->Tree.GetOverlay() : nullptr;
}

Element* App::GetFocusedElement() const
{
    return m_State != nullptr ? m_State->Tree.GetFocused() : nullptr;
}

bool App::ContainsElement(const Element* element) const
{
    return m_State != nullptr && m_State->Tree.Contains(element);
}

Menu* App::ShowMenu(const std::vector<MenuItem>& items, Vec2 windowPosition)
{
    Element* overlay = GetOverlay();
    if (overlay == nullptr)
    {
        return nullptr;
    }

    // One context menu at a time, reused: showing a second while the first is
    // open replaces it.
    Menu* menu = nullptr;
    for (Element* child : overlay->GetChildren())
    {
        if (child->Name == "opane.ContextMenu")
        {
            menu = dynamic_cast<Menu*>(child);
            break;
        }
    }
    if (menu == nullptr)
    {
        menu = overlay->Add<Menu>();
        menu->Name = "opane.ContextMenu";
    }

    menu->Close();
    menu->Items = items;
    menu->OpenAt(windowPosition);
    return menu;
}

Dialog* App::ShowDialog(const std::string& title, const std::string& message,
                        const std::vector<std::string>& buttons, std::function<void(int button)> onResult)
{
    Element* overlay = GetOverlay();
    if (overlay == nullptr)
    {
        return nullptr;
    }

    Dialog* dialog = overlay->Add<Dialog>();
    dialog->Title = title;
    dialog->Message = message;
    dialog->Buttons = buttons.empty() ? std::vector<std::string>{ "OK" } : buttons;
    dialog->CancelButton = -1;
    dialog->OnResult = [dialog, handler = std::move(onResult)](int button) {
        if (handler)
        {
            handler(button);
        }
        // A dialog shown this way belongs to no one, so it removes itself
        // once answered.
        if (Element* parent = dialog->GetParent())
        {
            parent->Remove(dialog);
        }
    };
    dialog->OpenCentered();
    return dialog;
}

const Theme& App::GetTheme() const
{
    static const Theme fallback;
    return m_State != nullptr ? m_State->Tree.GetTheme() : fallback;
}

void App::SetTheme(const Theme& theme)
{
    if (m_State != nullptr)
    {
        m_State->Tree.SetTheme(theme);
    }
}

void App::UpdateInterface(float deltaSeconds)
{
    if (m_State == nullptr)
    {
        return;
    }

    // Any atlas that ran out of room last frame is grown before painting
    // starts, since growing one moves every glyph in it.
    m_State->Fonts.BeginFrame();

    // Materials see the clock before anything paints, so a shader animating on
    // OpaneTime advances in step with the frame.
    m_State->Materials.Tick(GetTimeSeconds(), deltaSeconds);
    m_State->Materials.ReloadChanged();

    // A watched theme file, checked a few times a second.
    if (m_State->ThemeHotReload && !m_State->ThemePath.empty())
    {
        m_State->ThemeCheck += deltaSeconds;
        if (m_State->ThemeCheck >= 0.25f)
        {
            m_State->ThemeCheck = 0.0f;
            const int64_t stamp = FileStamp(m_State->ThemePath);
            if (stamp != 0 && stamp != m_State->ThemeStamp)
            {
                m_State->ThemeStamp = stamp;
                if (ApplyThemeFile(*m_State, *this))
                {
                    LogMessage(LogLevel::Info, "theme", "Reloaded \"%s\".", m_State->ThemePath.c_str());
                }
            }
        }
    }

    m_State->Tree.Update(deltaSeconds, GetWindowSize());

    // Render targets are claimed while painting; anything not claimed this
    // frame belonged to an element that went away, and is released.
    m_State->RenderTargets.BeginFrame();
    m_State->Tree.Paint(m_State->FrameDrawList);
    m_State->RenderTargets.EndFrame();
}

SoundId App::LoadSound(const std::string& path, AudioGroup group)
{
    if (m_State == nullptr)
    {
        return SoundId{};
    }
    return m_State->Audio.Load(path, group, false);
}

SoundId App::LoadMusic(const std::string& path)
{
    if (m_State == nullptr)
    {
        return SoundId{};
    }
    return m_State->Audio.Load(path, AudioGroup::Music, true);
}

void App::PlaySound(SoundId sound, const SoundPlayback& playback)
{
    if (m_State != nullptr)
    {
        m_State->Audio.Play(sound, playback);
    }
}

void App::PlaySoundAt(SoundId sound, Vec3 position, const SpatialPlayback& playback)
{
    if (m_State != nullptr)
    {
        m_State->Audio.PlayAt(sound, position, playback);
    }
}

void App::SetSoundPosition(SoundId sound, Vec3 position)
{
    if (m_State != nullptr)
    {
        m_State->Audio.SetPosition(sound, position);
    }
}

void App::SetListener(Vec3 position, Vec3 forward, Vec3 up)
{
    if (m_State != nullptr)
    {
        m_State->Audio.SetListener(position, forward, up);
    }
}

void App::StopSound(SoundId sound)
{
    if (m_State != nullptr)
    {
        m_State->Audio.Stop(sound);
    }
}

void App::StopGroup(AudioGroup group)
{
    if (m_State != nullptr)
    {
        m_State->Audio.StopGroup(group);
    }
}

bool App::IsSoundPlaying(SoundId sound) const
{
    return m_State != nullptr && m_State->Audio.IsPlaying(sound);
}

void App::DestroySound(SoundId sound)
{
    if (m_State != nullptr)
    {
        m_State->Audio.Destroy(sound);
    }
}

void App::SetGroupVolume(AudioGroup group, float volume)
{
    if (m_State != nullptr)
    {
        m_State->Audio.SetGroupVolume(group, volume);
    }
}

float App::GetGroupVolume(AudioGroup group) const
{
    return m_State != nullptr ? m_State->Audio.GetGroupVolume(group) : 0.0f;
}

bool App::IsAudioRunning() const
{
    return m_State != nullptr && m_State->Audio.IsRunning();
}

MaterialId App::CreateMaterial(const MaterialDesc& desc)
{
    if (m_State == nullptr)
    {
        return MaterialId{};
    }
    return m_State->Materials.Create(desc);
}

void App::DestroyMaterial(MaterialId material)
{
    if (m_State != nullptr)
    {
        m_State->Materials.Destroy(material);
    }
}

void App::SetMaterialUniform(MaterialId material, const std::string& name, float x, float y,
                             float z, float w)
{
    if (m_State != nullptr)
    {
        m_State->Materials.SetUniform(material, name, x, y, z, w);
    }
}

void App::SetMaterialUniform(MaterialId material, const std::string& name, Color color)
{
    SetMaterialUniform(material, name, color.R, color.G, color.B, color.A);
}

void App::AnimateMaterialUniform(MaterialId material, const std::string& name, float x, float y,
                                 float z, float w, float seconds, Easing easing)
{
    if (m_State != nullptr)
    {
        const float target[4] = { x, y, z, w };
        m_State->Materials.Animate(material, name, target, seconds, easing);
    }
}

void App::AnimateMaterialUniform(MaterialId material, const std::string& name, Color color,
                                 float seconds, Easing easing)
{
    AnimateMaterialUniform(material, name, color.R, color.G, color.B, color.A, seconds, easing);
}

void App::CancelMaterialAnimations(MaterialId material)
{
    if (m_State != nullptr)
    {
        m_State->Materials.CancelAnimations(material);
    }
}

std::string App::GetClipboardText() const
{
    char* text = SDL_GetClipboardText();
    if (text == nullptr)
    {
        return std::string{};
    }
    std::string copy = text;
    SDL_free(text);
    return copy;
}

void App::SetClipboardText(const std::string& text)
{
    SDL_SetClipboardText(text.c_str());
}

FontId App::LoadFont(const std::string& path, float pixelHeight)
{
    if (m_State == nullptr)
    {
        return FontId{};
    }
    const std::string resolved = detail::ResolveAsset(path, "text");
    if (resolved.empty())
    {
        return FontId{};
    }
    return m_State->Fonts.LoadFromFile(resolved, pixelHeight);
}

FontId App::GetDefaultFont() const
{
    return m_State != nullptr ? m_State->DefaultFont : FontId{};
}

Vec2 App::MeasureText(FontId font, const std::string& text) const
{
    if (m_State == nullptr)
    {
        return Vec2{};
    }
    return m_State->Fonts.Measure(font, text);
}

Vec2 App::MeasureRichText(const std::vector<TextRun>& runs, FontId font) const
{
    if (m_State == nullptr)
    {
        return Vec2{};
    }

    Vec2 total;
    for (const TextRun& run : runs)
    {
        const FontId runFont = run.Font.IsValid() ? run.Font : font;
        const Vec2 size = m_State->Fonts.Measure(runFont, run.Text);
        total.X += size.X;
        total.Y = std::max(total.Y, size.Y);
    }
    return total;
}

Vec2 App::MeasureTextWrapped(FontId font, const std::string& text, float maximumWidth) const
{
    if (m_State == nullptr)
    {
        return Vec2{};
    }

    const std::vector<size_t> starts = m_State->Fonts.WrapLines(font, text, maximumWidth);
    float widest = 0.0f;
    for (size_t line = 0; line < starts.size(); ++line)
    {
        const size_t begin = starts[line];
        const size_t end = (line + 1 < starts.size()) ? starts[line + 1] : text.size();
        widest = std::max(widest, m_State->Fonts.Measure(font, text.substr(begin, end - begin)).X);
    }

    return Vec2{ widest, m_State->Fonts.GetLineHeight(font) * static_cast<float>(starts.size()) };
}

float App::GetFontLineHeight(FontId font) const
{
    if (m_State == nullptr)
    {
        return 0.0f;
    }
    return m_State->Fonts.GetLineHeight(font);
}

SDL_Window* App::GetWindow() const
{
    return m_State != nullptr ? m_State->Window : nullptr;
}

SDL_GPUDevice* App::GetGpuDevice() const
{
    return m_State != nullptr ? m_State->Device : nullptr;
}

GraphicsBackend App::GetGraphicsBackend() const
{
    return detail::DeviceBackend(GetGpuDevice());
}

} // namespace opane
