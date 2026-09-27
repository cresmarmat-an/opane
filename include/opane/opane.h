// opane: application, window, input, interface, and audio.
// Copyright (c) 2026 Cresmar Mat-an. MIT License; see LICENSE.
//
// The application and frame loop, input, a draw list of signed-distance
// shapes, text, the element tree with layout, events, and theming, the
// built-in widgets, custom shaders on any element, audio, and a viewport for
// hosting a world. Documentation: https://cresmarmat-an.github.io/opane/
//
// This header does not include SDL. SDL types appear only as forward
// declarations, so including <opane/opane.h> does not pull SDL into your code.

#pragma once

#include <opane/version.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct SDL_Window;
struct SDL_GPUDevice;

namespace opane
{

class App;
class Renderer;
class Element;

namespace detail
{
struct AppState;
class FontStore;
class UiTree;
class RenderTargetStore;

// Wraps internal state in the public App view. Used by the element tree so
// any element can reach the application it belongs to.
App MakeAppView(AppState* state);
}

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------

struct Vec2
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct Vec3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

struct Rect
{
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};

struct Color
{
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    float A = 1.0f;

    static Color FromBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return Color{ r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    }
};

// ---------------------------------------------------------------------------
// Diagnostics
//
// Every message carries a category and a level. A user hook receives all of
// them, so an application can route diagnostics into its own console.
// ---------------------------------------------------------------------------

enum class LogLevel
{
    Trace,
    Info,
    Warning,
    Error
};

using LogHandler = std::function<void(LogLevel level, const char* category, const char* message)>;

void SetLogHandler(LogHandler handler);
void SetMinimumLogLevel(LogLevel level);
void LogMessage(LogLevel level, const char* category, const char* format, ...);

// ---------------------------------------------------------------------------
// Assets
//
// A relative name such as "click.wav" is searched for, in order, under each
// root you add (most recent first), then under the working directory's ./,
// ./assets/, and ./assets/images|sounds|music|fonts|shaders|models/, then the
// same folders beside the executable. The first match wins and is cached. A
// name that matches nothing is logged with every path that was tried.
// Absolute paths are used as they are.
//
// Every loader in opane uses this: images, fonts, sounds, music, and material
// shaders.
// ---------------------------------------------------------------------------

void AddAssetRoot(const std::string& directory);
void ClearAssetRoots();

// The roots in search order, user roots first. The executable-relative copies
// of the defaults are implied rather than listed.
std::vector<std::string> GetAssetRoots();

// The file a name resolves to, or an empty string (with a report of every
// path tried) when there is none.
std::string ResolveAssetPath(const std::string& path);

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

enum class Key : uint16_t
{
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Escape, Space, Enter, Tab, Backspace, Delete,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Insert,
    Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe,
    Comma, Period, Slash, Grave,
    Count
};

enum class MouseButton : uint8_t
{
    Left = 0,
    Middle,
    Right,
    Count
};

// Per-frame input state. "Down" is level-triggered and true for as long as the
// key is held. "Pressed" and "Released" are edge-triggered and true only during
// the frame in which the transition happened.
class Input
{
public:
    bool IsKeyDown(Key key) const;
    bool WasKeyPressed(Key key) const;
    bool WasKeyReleased(Key key) const;

    bool IsMouseButtonDown(MouseButton button) const;
    bool WasMouseButtonPressed(MouseButton button) const;
    bool WasMouseButtonReleased(MouseButton button) const;

    Vec2 GetMousePosition() const { return m_MousePosition; }
    Vec2 GetMouseDelta() const { return m_MouseDelta; }
    float GetScrollDelta() const { return m_ScrollDelta; }

private:
    friend struct detail::AppState;

    static constexpr size_t KeyCount = static_cast<size_t>(Key::Count);
    static constexpr size_t ButtonCount = static_cast<size_t>(MouseButton::Count);

    bool m_KeyDown[KeyCount] = {};
    bool m_KeyPressed[KeyCount] = {};
    bool m_KeyReleased[KeyCount] = {};

    bool m_ButtonDown[ButtonCount] = {};
    bool m_ButtonPressed[ButtonCount] = {};
    bool m_ButtonReleased[ButtonCount] = {};

    Vec2 m_MousePosition;
    Vec2 m_MouseDelta;
    float m_ScrollDelta = 0.0f;
};

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

struct TextureId
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    bool IsValid() const { return Generation != 0; }
};

enum class PixelFormat
{
    Rgba8,     // four bytes per pixel, straight alpha
    Alpha8     // one byte per pixel, used by the glyph atlas
};

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

struct FontId
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    bool IsValid() const { return Generation != 0; }
};

enum class TextAlign
{
    Left,
    Center,
    Right
};

// ---------------------------------------------------------------------------
// Materials
//
// A material is a custom fragment shader plus a uniform block, and it can be
// attached to any element. The vertex stage stays opane's, so a material
// changes how an element looks without changing how it lays out or batches.
//
// Author the shader against shaders/material.hlsli, which supplies the input
// struct, the uniform block your Params land in, and helpers for anti-aliasing
// procedural edges.
// ---------------------------------------------------------------------------

struct MaterialId
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    bool IsValid() const { return Generation != 0; }

    friend bool operator==(const MaterialId& a, const MaterialId& b)
    {
        return a.Index == b.Index && a.Generation == b.Generation;
    }
    friend bool operator!=(const MaterialId& a, const MaterialId& b) { return !(a == b); }
};

// One named value. The order of these defines which Params[] slot each name
// occupies in the shader.
struct MaterialUniform
{
    std::string Name;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 0.0f;

    MaterialUniform() = default;
    MaterialUniform(std::string name, float x, float y = 0.0f, float z = 0.0f, float w = 0.0f)
        : Name(std::move(name)), X(x), Y(y), Z(z), W(w)
    {
    }
    MaterialUniform(std::string name, Color color)
        : Name(std::move(name)), X(color.R), Y(color.G), Z(color.B), W(color.A)
    {
    }
};

// A compiled shader in each backend's format. opane_add_material fills one in
// for every format the build makes; a format left empty is one the material
// cannot be drawn with.
struct ShaderBytecode
{
    const void* Dxil = nullptr; // Direct3D 12
    size_t DxilSize = 0;
    const void* Spirv = nullptr; // Vulkan
    size_t SpirvSize = 0;
    const void* Msl = nullptr; // Metal, as source text
    size_t MslSize = 0;

    bool IsEmpty() const { return DxilSize == 0 && SpirvSize == 0 && MslSize == 0; }
};

struct MaterialDesc
{
    // Development path: an .hlsl file compiled when the material is created,
    // for whichever backend is running. Saving the file recompiles it in
    // place; a compile error keeps the working pipeline and reports the file,
    // line, and column.
    std::string ShaderPath;

    // Shipping path: bytecode compiled at build time, so no shader compiler is
    // needed at runtime. The function that opane_add_material generates
    // returns it. Takes precedence over ShaderPath when it holds any format.
    ShaderBytecode Bytecode;

    std::string EntryPoint = "FragmentMain";

    // At most eight. Order defines the Params[] slots.
    std::vector<MaterialUniform> Uniforms;

    bool HotReload = true;
};

// A stretch of text with its own font and colour. A line made of several runs
// can mix fonts, sizes, and colours without being split into several elements.
struct TextRun
{
    std::string Text;
    FontId Font;                          // invalid means the font passed alongside
    Color Color{ 0.0f, 0.0f, 0.0f, 0.0f }; // transparent means the colour passed alongside
};

// ---------------------------------------------------------------------------
// Draw list
//
// Painting records commands instead of issuing GPU calls. The list is batched
// and submitted once per frame, so a full screen of interface usually takes
// only a few draw calls.
//
// Every shape is drawn from a signed distance field: rounded corners, circles,
// and strokes cost one quad each, stay sharp at any scale, and are
// anti-aliased in the shader without multisampling.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Styling primitives
//
// What a box can look like: a fill (a colour, a gradient, or an image), a
// border, a radius for each corner, shadows and glows, and a frosted-glass blur
// of whatever is behind it. DrawList can draw each of these separately, and
// DrawBox draws a whole BoxStyle. An element's Appearance holds a BoxStyle for
// each of its states.
// ---------------------------------------------------------------------------

// A radius for each corner, clockwise from the top left. One number rounds all
// four.
struct CornerRadii
{
    float TopLeft = 0.0f;
    float TopRight = 0.0f;
    float BottomRight = 0.0f;
    float BottomLeft = 0.0f;

    CornerRadii() = default;
    CornerRadii(float all) : TopLeft(all), TopRight(all), BottomRight(all), BottomLeft(all) {}
    CornerRadii(float topLeft, float topRight, float bottomRight, float bottomLeft)
        : TopLeft(topLeft), TopRight(topRight), BottomRight(bottomRight), BottomLeft(bottomLeft)
    {
    }

    float Largest() const;
};

// Distances in from each edge of a box.
struct Insets
{
    float Left = 0.0f;
    float Top = 0.0f;
    float Right = 0.0f;
    float Bottom = 0.0f;

    Insets() = default;
    Insets(float all) : Left(all), Top(all), Right(all), Bottom(all) {}
    Insets(float horizontal, float vertical) : Left(horizontal), Top(vertical), Right(horizontal), Bottom(vertical) {}
    Insets(float left, float top, float right, float bottom) : Left(left), Top(top), Right(right), Bottom(bottom) {}
};

// A 2D affine transform: x' = A x + C y + E, y' = B x + D y + F. Rotations are
// clockwise, as the y axis points down.
struct Affine2D
{
    float A = 1.0f;
    float B = 0.0f;
    float C = 0.0f;
    float D = 1.0f;
    float E = 0.0f;
    float F = 0.0f;

    static Affine2D Translation(Vec2 offset);
    static Affine2D Rotation(float degrees);
    static Affine2D Scaling(Vec2 scale);

    // Turns and scales about a point rather than the origin.
    static Affine2D About(Vec2 pivot, float degrees, Vec2 scale);

    // Applies other first, then this.
    Affine2D operator*(const Affine2D& other) const;

    Vec2 Apply(Vec2 point) const;
    Affine2D Inverse() const;
    bool IsIdentity() const;
};

// How an image fills a box whose shape differs from its own.
enum class ImageFit
{
    Stretch,   // fill the box exactly, distorting if the shapes differ
    Contain,   // the whole image, as large as fits, letterboxed if needed
    Cover,     // fill the box, cropping whatever overhangs
    Center,    // at its own size, centred, cropped if larger
    Tile,      // repeated at its own size (times TileScale) from the top left
    NineSlice  // corners kept at their own size, edges and middle stretched
};

struct GradientStop
{
    float Position = 0.0f; // 0 at the start of the gradient, 1 at its end
    Color Color;
};

enum class FillKind
{
    None,
    Solid,
    LinearGradient,
    RadialGradient,
    Image
};

// What fills a box.
struct Fill
{
    FillKind Kind = FillKind::None;

    // Solid: the colour. Image: the tint the image is multiplied by.
    Color Color{ 1.0f, 1.0f, 1.0f, 1.0f };

    // Gradients: two or more, in order. The gradient runs across the whole
    // box, corner to corner along its direction, as CSS gradients do.
    std::vector<GradientStop> Stops;

    // Linear: the direction, in degrees clockwise from pointing right, so 90
    // runs from the top down and 0 from left to right.
    float Angle = 90.0f;

    // Radial: where the gradient starts, from 0,0 at the box's top left to 1,1
    // at its bottom right, and how far it reaches in units; 0 reaches the
    // farthest corner.
    Vec2 Center{ 0.5f, 0.5f };
    float Radius = 0.0f;

    // Image: the picture and how it fits. TileScale sizes a tiled image's
    // repeats; Slice is a nine-slice image's border, in the image's pixels,
    // drawn at one unit a pixel.
    TextureId Image;
    ImageFit Fit = ImageFit::Cover;
    float TileScale = 1.0f;
    Insets Slice;

    static Fill Solid(struct Color color);
    static Fill Linear(struct Color from, struct Color to, float angle = 90.0f);
    static Fill Linear(std::vector<GradientStop> stops, float angle = 90.0f);
    static Fill Radial(struct Color inner, struct Color outer, Vec2 center = Vec2{ 0.5f, 0.5f }, float radius = 0.0f);
    static Fill FromImage(TextureId image, ImageFit fit = ImageFit::Cover,
                          struct Color tint = { 1.0f, 1.0f, 1.0f, 1.0f });
    static Fill NineSliced(TextureId image, Insets slice, struct Color tint = { 1.0f, 1.0f, 1.0f, 1.0f });

    bool IsEmpty() const { return Kind == FillKind::None; }
};

// A soft shadow beneath a box, or inside it. A glow is a shadow of a bright
// colour with no offset.
struct Shadow
{
    Color Color{ 0.0f, 0.0f, 0.0f, 0.35f };
    Vec2 Offset{ 0.0f, 4.0f };
    float Blur = 12.0f;  // how far it fades, in units
    float Spread = 0.0f; // grows the shadow's shape before it is blurred
    bool Inset = false;  // inside the box, darkening in from its edges
};

// Everything a box can look like. Drawn by DrawList::DrawBox, and by the tree
// beneath any element whose Appearance has one.
struct BoxStyle
{
    Fill Background;

    // Drawn inside the box's edge, so a border never changes its size.
    Color BorderColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float BorderWidth = 0.0f;

    CornerRadii Radius;

    // Beneath the box in order, then the inset ones inside it.
    std::vector<Shadow> Shadows;

    // Blurs whatever is behind the box by this many units before the
    // background is drawn over it: frosted glass. Give the background some
    // transparency, or the blur is covered.
    float BackdropBlur = 0.0f;

    // Multiplies everything the element and its children draw.
    float Opacity = 1.0f;

    // The text colour built-in widgets use over this box; transparent keeps
    // the theme's.
    Color TextColor{ 0.0f, 0.0f, 0.0f, 0.0f };

    // A scale and offset that ease with the state, such as a pressed button
    // moving down a pixel or a hovered card growing slightly. They do not
    // affect layout. The scale is about the box's centre.
    float Scale = 1.0f;
    Vec2 Offset;

    bool IsEmpty() const;
};

class DrawList
{
public:
    void FillRect(const Rect& bounds, Color color);
    void FillRoundedRect(const Rect& bounds, float cornerRadius, Color color);
    void FillCircle(Vec2 center, float radius, Color color);

    // A stroke is centered on the shape's edge.
    void StrokeRect(const Rect& bounds, float thickness, Color color, float cornerRadius = 0.0f);
    void StrokeCircle(Vec2 center, float radius, float thickness, Color color);

    // A straight line with round ends, anti-aliased at any angle. The built-in
    // widgets draw their chevrons, ticks, and crosses this way instead of
    // relying on font glyphs.
    void DrawLine(Vec2 from, Vec2 to, float thickness, Color color);

    void DrawTexture(const Rect& bounds, TextureId texture, Color tint = Color{ 1, 1, 1, 1 });
    void DrawTextureRegion(const Rect& bounds, TextureId texture, const Rect& uv,
                           Color tint = Color{ 1, 1, 1, 1 });

    // Treats the texture's red channel as coverage and the color as the fill.
    // This is how glyphs are drawn, and it is public so a user-written element
    // can use a mask texture the same way.
    void DrawMaskedTextureRegion(const Rect& bounds, TextureId texture, const Rect& uv, Color color);

    // For a texture that already holds premultiplied colour, such as a
    // render-target element's output. Drawing one through DrawTexture would
    // premultiply it a second time and darken its translucent edges.
    void DrawPremultipliedTexture(const Rect& bounds, TextureId texture, float opacity = 1.0f);

    // Draws a single line. The position is the left edge of the baseline's
    // em box, so text sits where the caller expects without measuring first.
    void DrawText(const std::string& text, Vec2 position, FontId font, Color color,
                  TextAlign align = TextAlign::Left);

    // Draws a single line centered vertically inside the rectangle.
    void DrawTextInRect(const std::string& text, const Rect& bounds, FontId font, Color color,
                        TextAlign align = TextAlign::Center);

    // Draws runs one after another on a single line, each in its own font and
    // colour. They share a baseline, so mixed sizes sit on the same line
    // rather than on their own boxes.
    void DrawRichText(const std::vector<TextRun>& runs, Vec2 position, FontId font, Color color,
                      TextAlign align = TextAlign::Left);

    // Draws text broken into lines at the spaces, filling the rectangle from
    // its top. Returns the height of the result.
    float DrawTextWrapped(const std::string& text, const Rect& bounds, FontId font, Color color,
                          TextAlign align = TextAlign::Left);

    // Clips every subsequent command to the intersection of the current clip
    // and this rectangle, until the matching PopClip.
    void PushClip(const Rect& bounds);
    void PopClip();
    Rect GetClip() const;

    // --- styling ------------------------------------------------------------

    // Each corner rounded by its own radius.
    void FillRoundedRect(const Rect& bounds, const CornerRadii& radii, Color color);
    void StrokeRoundedRect(const Rect& bounds, const CornerRadii& radii, float thickness, Color color);

    // A box filled any way a Fill describes, its corners rounded.
    void DrawFill(const Rect& bounds, const Fill& fill, const CornerRadii& radii = {});

    // A soft shadow of a rounded box, or a glow. An inset one is drawn inside
    // the box.
    void DrawShadow(const Rect& bounds, const CornerRadii& radii, const Shadow& shadow);

    // Shadows, background, inset shadows, and border, in that order, with the
    // backdrop blurred first when the style asks for it.
    void DrawBox(const Rect& bounds, const BoxStyle& style);

    // An image fitted to a box any way ImageFit describes, its corners rounded.
    // slice is a nine-slice image's border in its own pixels.
    void DrawImage(const Rect& bounds, TextureId texture, ImageFit fit, Color tint = Color{ 1, 1, 1, 1 },
                   const CornerRadii& radii = {}, const Insets& slice = {}, float tileScale = 1.0f);

    // Blurs everything drawn beneath this box so far, by this many units, and
    // shows the result inside the box with its corners rounded: frosted glass.
    // tint is laid over the blur by its own alpha.
    void BackdropBlur(const Rect& bounds, float radius, const CornerRadii& radii = {},
                      Color tint = Color{ 0, 0, 0, 0 });

    // Moves, turns, and scales everything drawn until the matching
    // PopTransform, on top of any transform already pushed. In interface
    // units.
    void PushTransform(const Affine2D& transform);
    void PopTransform();
    Affine2D GetTransform() const;

    // Multiplies the alpha of everything drawn until the matching PopOpacity,
    // on top of any opacity already pushed.
    void PushOpacity(float opacity);
    void PopOpacity();
    float GetOpacity() const;

    // Pixels per interface unit. Everything recorded here is in interface
    // units, which the list multiplies by this on the way to the GPU; a
    // custom element that wants a line exactly one pixel wide draws it
    // 1 / GetPixelScale() units wide.
    float GetPixelScale() const { return m_Scale; }

    // Every shape recorded from here on is drawn with this material. Pass an
    // empty MaterialId to return to the default. A material change starts a new
    // batch, since it is a different pipeline.
    void SetMaterial(MaterialId material);
    MaterialId GetMaterial() const { return m_CurrentMaterial; }

    void Clear();
    size_t GetCommandCount() const;

private:
    friend struct detail::AppState;
    friend class Renderer;
    friend class detail::UiTree;
    friend class detail::RenderTargetStore;

    struct Vertex
    {
        float PositionX, PositionY;
        float U, V;
        float R, G, B, A;
        float LocalX, LocalY, HalfWidth, HalfHeight;
        float CornerRadius, StrokeWidth, TextureMode, ShapeFlags;
        float RadiusTopLeft, RadiusTopRight, RadiusBottomRight, RadiusBottomLeft;
    };

    struct Batch
    {
        uint32_t FirstIndex = 0;
        uint32_t IndexCount = 0;
        TextureId Texture;
        MaterialId Material;
        Rect Clip;

        // A batch that first blurs what is behind it, by this many pixels,
        // into the texture it then samples: frosted glass. It stands alone.
        float BackdropBlur = 0.0f;
    };

    // One quad, in pixels. Everything drawn goes through here.
    struct Quad
    {
        Rect Bounds;                 // the shape's box
        float Padding = 0.0f;        // how far the quad reaches past it
        float CornerU[4] = {};       // texture coordinates at the padded corners
        float CornerV[4] = {};
        Color Colors[4];             // at the padded corners
        float Radii[4] = {};         // top left, top right, bottom right, bottom left
        float ShapeX = 0.0f;         // ShapeB.x
        float Stroke = 0.0f;         // ShapeB.y
        float Mode = 0.0f;           // ShapeB.z
        bool SkipShape = false;      // ShapeB.w
        TextureId Texture;
        float BackdropBlur = 0.0f;
    };

    // skipShape makes the fragment take its coverage from the texture alone,
    // which is what glyph quads need so they sample the atlas exactly.
    void PushQuad(const Rect& bounds, const Rect& uv, Color color, float cornerRadius,
                  float strokeWidth, float textureMode, TextureId texture, bool skipShape = false);

    // Records a quad through the current transform, opacity, and clip.
    void Emit(Quad& quad);

    // Fills a quad's corner texture coordinates from a rectangle of the
    // texture spread over its unpadded box.
    static void SpreadUv(Quad& quad, const Rect& uv);

    // A quad for a box in units with its corners rounded, in pixels.
    static void ShapeQuad(Quad& quad, const Rect& unitBounds, const CornerRadii& radii, float scale);

    // DrawBox without the style's opacity and nudge, which the tree applies to
    // the whole element instead.
    void DrawBoxShape(const Rect& bounds, const BoxStyle& style);

    // The clip in pixels, which is what batches carry.
    Rect PixelClip() const;

    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<Batch> m_Batches;
    std::vector<Rect> m_ClipStack;
    std::vector<Affine2D> m_TransformStack; // in pixels
    std::vector<float> m_OpacityStack;
    Rect m_Viewport;
    MaterialId m_CurrentMaterial;
    detail::FontStore* m_Fonts = nullptr;
    Renderer* m_Renderer = nullptr;
    float m_Scale = 1.0f;
};

// ---------------------------------------------------------------------------
// Audio
//
// One engine per process, shared by music and effects. Volume groups let a
// player turn music down without touching interface sounds.
// ---------------------------------------------------------------------------

struct SoundId
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    bool IsValid() const { return Generation != 0; }
};

enum class AudioGroup
{
    Master,
    Music,
    Interface,
    Effects
};

struct SoundPlayback
{
    float Volume = 1.0f;
    float Pitch = 1.0f;
    float Pan = 0.0f;   // -1 left, 0 center, 1 right
    bool Looping = false;

    // Restarts a sound that is already playing, rather than ignoring the call.
    bool Restart = true;
};

// Playback positioned in the world, attenuated by distance from the listener.
struct SpatialPlayback
{
    float Volume = 1.0f;
    float Pitch = 1.0f;
    bool Looping = false;
    bool Restart = true;

    // Full volume within MinDistance, silent beyond MaxDistance, rolling off
    // between them. In the same units as the positions you pass.
    float MinDistance = 1.0f;
    float MaxDistance = 40.0f;
    float Rolloff = 1.0f;
};

// ---------------------------------------------------------------------------
// Layout
//
// Sizes and positions are a fraction of the parent plus an offset in interface
// units, so a layout adapts to the window without arithmetic at the call site.
// Size2::FromScale(0.5f, 1.0f) is half the parent's width and all of its
// height; { {0.5f, -10.0f}, {1.0f, 0.0f} } is half the width minus ten units.
// ---------------------------------------------------------------------------

struct Dim
{
    float Scale = 0.0f;
    float Offset = 0.0f;

    static Dim FromScale(float scale) { return Dim{ scale, 0.0f }; }
    static Dim FromOffset(float offset) { return Dim{ 0.0f, offset }; }

    float Resolve(float parent) const { return Scale * parent + Offset; }
};

struct Size2
{
    Dim X;
    Dim Y;

    static Size2 FromOffset(float width, float height)
    {
        return Size2{ Dim::FromOffset(width), Dim::FromOffset(height) };
    }
    static Size2 FromScale(float width, float height)
    {
        return Size2{ Dim::FromScale(width), Dim::FromScale(height) };
    }
    static Size2 Fill() { return FromScale(1.0f, 1.0f); }

    Vec2 Resolve(Vec2 parent) const { return Vec2{ X.Resolve(parent.X), Y.Resolve(parent.Y) }; }
};

struct Position2
{
    Dim X;
    Dim Y;

    static Position2 FromOffset(float x, float y)
    {
        return Position2{ Dim::FromOffset(x), Dim::FromOffset(y) };
    }
    static Position2 FromScale(float x, float y)
    {
        return Position2{ Dim::FromScale(x), Dim::FromScale(y) };
    }
    // Pair with an anchor point of {0.5, 0.5}; Element::PlaceCentered does both.
    static Position2 Center() { return FromScale(0.5f, 0.5f); }

    Vec2 Resolve(Vec2 parent) const { return Vec2{ X.Resolve(parent.X), Y.Resolve(parent.Y) }; }
};

enum class Corner
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

// How an element positions its children.
enum class LayoutMode
{
    Absolute,   // each child uses its own Position and Size
    Vertical,   // children stack downward, each as wide as the content box
    Horizontal, // children stack rightward, each as tall as the content box

    // The element places each child itself, in PlaceChild. The splitter and
    // the tab view work this way, and so can a layout of your own, such as a
    // grid, a flow layout, or a radial menu.
    Custom
};

// ---------------------------------------------------------------------------
// Appearance
//
// A look for each state an element can be in, blended over time as the state
// changes. Any element can have one, including your own elements. It is drawn
// beneath whatever the element paints itself; a built-in widget with an
// Appearance draws it instead of its own box.
// ---------------------------------------------------------------------------

// How a value moves from where it is to its target. Smooth starts and ends
// gently and suits most interface transitions.
enum class Easing
{
    Linear,
    Smooth,
    In,
    Out
};

struct Style
{
    BoxStyle Normal;

    // Unset states look like Normal. When several apply, the first of
    // Disabled, Pressed, Selected, Hovered, and Focused wins. Hovered,
    // Pressed, and Focused include the element's children: a card counts as
    // hovered while the pointer is over a label inside it. Selected applies
    // to elements you mark with SetSelected (the current tab, for example)
    // and to a checkbox, toggle, or radio button that is on.
    std::optional<BoxStyle> Hovered;
    std::optional<BoxStyle> Pressed;
    std::optional<BoxStyle> Focused;
    std::optional<BoxStyle> Selected;
    std::optional<BoxStyle> Disabled;

    // How long a change of state takes, and its curve.
    float Transition = 0.15f;
    Easing Curve = Easing::Smooth;

    bool IsEmpty() const;
};

// Moves, rotates, and scales an element and its children when they are drawn,
// without changing the layout. Hit testing follows the transform, so the
// pointer finds the element where it appears.
struct ElementTransform
{
    Vec2 Translate;               // in units
    Vec2 Scale{ 1.0f, 1.0f };
    float Rotation = 0.0f;        // degrees, clockwise
    Vec2 Pivot{ 0.5f, 0.5f };     // 0,0 at the element's top left, 1,1 at its bottom right

    bool IsIdentity() const;
};

// The part of the window an element stands for, when the window has no frame
// of its own (AppConfig::Borderless) and the interface draws its own: pressing
// on a Drag element moves the window, and on a Resize one resizes it.
enum class WindowRegion
{
    None,
    Drag,
    ResizeTop,
    ResizeBottom,
    ResizeLeft,
    ResizeRight,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight
};

// ---------------------------------------------------------------------------
// Theme
//
// Colours, sizes, and looks as data, so they can be loaded from a file,
// generated, or swapped at runtime without touching layout code. The built-in
// widgets take their colours and sizes from the theme.
// ---------------------------------------------------------------------------

struct Theme
{
    Color Background{ 0.07f, 0.08f, 0.10f, 1.0f };
    Color Surface{ 0.13f, 0.14f, 0.18f, 1.0f };
    Color SurfaceHovered{ 0.18f, 0.20f, 0.25f, 1.0f };
    Color SurfacePressed{ 0.10f, 0.11f, 0.14f, 1.0f };
    Color Accent{ 0.36f, 0.60f, 0.95f, 1.0f };
    Color AccentHovered{ 0.46f, 0.68f, 1.00f, 1.0f };
    Color Text{ 0.92f, 0.94f, 0.97f, 1.0f };
    Color TextMuted{ 0.58f, 0.62f, 0.70f, 1.0f };
    Color Border{ 1.0f, 1.0f, 1.0f, 0.12f };

    float CornerRadius = 8.0f;
    float Padding = 12.0f;
    float Spacing = 8.0f;
    float BorderWidth = 1.0f;

    FontId Font;

    // Skins for the built-in widgets. A widget whose own Appearance is empty
    // takes its look from here, and one left empty here keeps the built-in
    // look, so a theme restyles as much or as little as it likes.
    //
    // The parts are pieces drawn inside a widget, in its state: Selected is a
    // checkbox ticked, a toggle on, a slider or scroll bar being dragged.
    struct WidgetStyles
    {
        Style Button;
        Style AccentButton;
        Style Panel;     // panels showing the theme's surface
        Style Window;
        Style Popup;     // menus, dialogs, drop-down lists, and popups of your own
        Style TextInput; // single-line fields, number fields, and text areas
        Style Dropdown;
        Style ListView;  // lists and trees
        Style Tooltip;

        Style CheckboxBox;
        Style ToggleTrack;
        Style ToggleKnob;
        Style SliderTrack;
        Style SliderFill;
        Style SliderThumb;
        Style ProgressTrack;
        Style ProgressFill;
        Style ScrollbarThumb;
    };
    WidgetStyles Styles;

    // Looks of your own, by name, for any element whose StyleName names one:
    // a theme file's "styles" beyond the widgets' land here.
    std::vector<std::pair<std::string, Style>> NamedStyles;

    // The named look, or null when there is none by that name.
    const Style* FindStyle(const std::string& name) const;

    static Theme Dark(FontId font);
    static Theme Light(FontId font);
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

enum class EventType
{
    PointerEnter,
    PointerLeave,
    PointerMove,
    PointerDown,
    PointerUp,
    Wheel,
    KeyDown,
    KeyUp,

    // Characters that were typed, already composed: one event may carry
    // several characters, and an accented letter or a character entered
    // through an input method arrives here rather than as key presses.
    Text,

    FocusGained,
    FocusLost
};

struct Event
{
    EventType Type = EventType::PointerMove;
    Vec2 Position;              // in interface units, from the window's top left
    MouseButton Button = MouseButton::Left;
    Key KeyCode = Key::Unknown;
    float WheelDelta = 0.0f;

    // What was typed, as UTF-8. Only for EventType::Text.
    std::string Text;

    // Held while the key was pressed or the text was typed.
    bool Shift = false;
    bool Control = false;
    bool Alt = false;

    // True while a key is being repeated because it is held down. Editing
    // acts on repeats; a shortcut usually should not.
    bool Repeat = false;

    // Set this to stop the event from bubbling to the parent.
    bool Handled = false;
};

// The pointer's shape while it is over an element, or while an element is
// dragging it.
enum class CursorShape
{
    Default,
    Pointer,            // a hand, for something that acts when clicked
    Text,               // an I-beam, for somewhere to type
    ResizeHorizontal,   // a left-right arrow, for a vertical divider
    ResizeVertical,     // an up-down arrow, for a horizontal divider
    ResizeDiagonal,     // for a window's corner grip
    Move,               // for something that is being dragged about
    NotAllowed
};


// ---------------------------------------------------------------------------
// Elements
//
// Everything drawable is an Element. The built-in widgets use the same public
// interface as your own elements, so a custom element takes part in layout,
// painting, input, focus, and batching in the same way.
// ---------------------------------------------------------------------------

class Element
{
public:
    Element() = default;
    virtual ~Element() = default;

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    // --- the authoring surface -------------------------------------------

    // Returns the size this element would like, given what the parent offers.
    virtual Vec2 Measure(Vec2 available);

    // Receives the final rectangle and places children inside it.
    virtual void Arrange(const Rect& bounds);

    // Records this element's own painting. Children are painted afterwards by
    // the tree, so a Paint override does not have to call anything.
    virtual void Paint(DrawList& drawList);

    // Non-rectangular elements override this; the default is the bounds.
    virtual bool HitTest(Vec2 point) const;

    // An event goes to the element under the pointer (or with focus) first,
    // then bubbles up to each ancestor until one sets Handled.
    virtual void OnEvent(Event& event);

    virtual void OnUpdate(float deltaSeconds);

    // Where a child goes, for an element whose ChildLayout is Custom. Called
    // once per visible child each layout, with the element's content box.
    // The default gives every child the whole box.
    virtual Rect PlaceChild(Element& child, size_t index, const Rect& content);

    // A key press that the focused element did not handle. Every element in
    // the tree is asked, front to back, until one returns true. A menu bar
    // uses this so Ctrl+S works wherever focus is, while a focused text field
    // still gets Ctrl+C first.
    virtual bool OnShortcut(const Event& keyEvent);

    // Called on a parent just before one of its children is destroyed, so an
    // element that keeps track of its children (a tab view or a dock space,
    // for example) can drop one it did not remove itself.
    virtual void OnChildRemoved(Element& child);

    // Whether this element accepts typing while it has focus. Returning true
    // turns on the platform's text input (and the on-screen keyboard, where
    // there is one) while it holds focus. Typed text arrives as
    // EventType::Text.
    virtual bool WantsText() const;

    // --- layout properties ------------------------------------------------

    Size2 Size = Size2::Fill();
    Position2 Position;
    Vec2 AnchorPoint;          // which point of this element Position refers to
    LayoutMode ChildLayout = LayoutMode::Absolute;
    float Padding = 0.0f;
    float Spacing = 0.0f;
    bool Visible = true;
    bool Interactive = true;
    bool ClipChildren = false;
    std::string Name;

    // In a stack, a share of the space left over once the children without
    // Flex have been measured. Two children with Flex 1 split the remainder
    // evenly; 2 and 1 split it two to one. Zero, the default, means the
    // element keeps its own size.
    //
    // For example, a 280-unit sidebar next to a viewport with Flex 1 gives the
    // viewport the rest of the row, even when the sidebar's width changes.
    float Flex = 0.0f;

    // Never measured smaller than this, on either axis. Zero means no limit.
    Vec2 MinSize;

    // Whether Tab stops here, and whether a click gives it the keyboard. The
    // built-in controls set it; set it on an element of your own that reacts
    // to keys. Tab and Shift+Tab move between focusable elements in the
    // order they appear in the tree.
    bool Focusable = false;

    // Shown beside the pointer after it has rested here a moment. An element
    // without one shows its nearest ancestor's.
    std::string Tooltip;

    // The pointer's shape over this element. It can be changed from OnEvent
    // while the pointer is over the element, and the change shows at once.
    CursorShape Cursor = CursorShape::Default;

    // A custom shader for this element. The tree applies it before Paint and
    // clears it afterwards, so any element, built-in or your own, can use one
    // without changes to its drawing code.
    MaterialId Material;

    // Renders this element and everything inside it into a texture of its own
    // size, then composites the result. On its own this changes nothing
    // visible; PostProcess uses it.
    bool RenderToTexture = false;

    // Materials run over the rendered subtree, in order, each reading the
    // previous one's output through Surface: a blur, a colour grade, or a
    // dissolve over a whole panel, for example. Setting any implies
    // RenderToTexture.
    //
    // In these passes Surface holds premultiplied colour, so a post-process
    // shader returns its result as it is rather than through Premultiply.
    std::vector<MaterialId> PostProcess;

    // --- appearance -------------------------------------------------------

    // How the element's box looks in each state, drawn beneath its own
    // painting. A built-in widget with one draws it instead of its own box.
    Style Appearance;

    // Moves, turns, and scales the element and its children as they are
    // drawn, without changing the layout.
    ElementTransform Transform;

    // Multiplies everything the element and its children draw.
    float Opacity = 1.0f;

    // A disabled element takes no pointer or keyboard input, and looks it:
    // built-in widgets dim, and an Appearance shows its Disabled state.
    bool Enabled = true;

    // Replaces Paint: the element draws only what this function draws.
    // Children are still painted afterwards by the tree.
    std::function<void(DrawList& drawList, Element& element)> Painter;

    // For a window without a frame: what pressing here does to the window.
    WindowRegion Region = WindowRegion::None;

    // The name of a look in Theme::NamedStyles (usually from a theme file),
    // used when Appearance is empty. Editing the file restyles every element
    // that uses the name.
    std::string StyleName;

    // The look a widget takes from the theme when it has neither an
    // Appearance nor a StyleName. The built-in widgets return their entry of
    // Theme::Styles; an element of your own can return a look of its own.
    virtual const Style* GetDefaultAppearance(const Theme& theme) const;

    // The look the element has: its Appearance, its named style, or its
    // default, in that order. Null when it has none, and draws itself.
    const Style* GetEffectiveAppearance() const;

    // The look it has reached this frame, between one state and the next.
    // Built-in widgets read it for their text colour.
    const BoxStyle& GetCurrentLook() const { return m_Look; }

    // True when the element has a look, which is when a built-in widget draws
    // that instead of its own box.
    bool HasAppearance() const { return GetEffectiveAppearance() != nullptr; }

    // True when this element and every one above it is enabled.
    bool IsEnabled() const;

    // Whether the element is the chosen one of a set, such as the current tab.
    // Its Appearance then shows the Selected state. Checkboxes, toggles, and
    // radio buttons are selected while they are on; mark other elements
    // yourself.
    void SetSelected(bool selected) { m_Selected = selected; }
    virtual bool IsSelected() const { return m_Selected; }

    // --- tree -------------------------------------------------------------

    // Creates a child of any Element type, including user-written ones, and
    // returns it. The tree owns the child.
    template <typename T, typename... Args>
    T* Add(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = child.get();
        AdoptChild(std::unique_ptr<Element>(child.release()));
        return raw;
    }

    // Queued, and applied at the end of the frame, so removing a child from
    // inside its own event handler is safe.
    void Remove(Element* child);
    void RemoveAllChildren();

    // Moves this element and everything inside it to a new parent, keeping
    // its state, children, and focus. The dock space uses it to move a panel
    // into a floating window. Refused, with a warning, when the new parent is
    // inside this element or belongs to another tree.
    bool MoveTo(Element* newParent);

    // Draws this element over its siblings and gives it the pointer first.
    // A window clicked in a stack of windows comes to the front this way.
    void BringToFront();

    Element* GetParent() const { return m_Parent; }
    const std::vector<Element*>& GetChildren() const { return m_ChildViews; }

    // A token that expires when this element is destroyed; ElementRef watches
    // it. Created the first time it is requested.
    std::weak_ptr<const void> GetLifetime() const;

    // --- state ------------------------------------------------------------

    Rect GetBounds() const { return m_Bounds; }

    // Where this element's children are laid out. By default, its bounds
    // minus its padding. A scroll view overrides it to shift its children up
    // and let them extend past the bottom edge.
    virtual Rect GetContentBounds() const;

    bool IsHovered() const { return m_Hovered; }
    bool IsPressed() const { return m_Pressed; }
    bool IsFocused() const { return m_Focused; }

    // Focused, and reached with the keyboard instead of a click. Use it to
    // decide whether to draw a focus ring.
    bool IsFocusVisible() const;

    // True when this element is inside the other one, or is it.
    bool IsInside(const Element* ancestor) const;

    // The element and every descendant, depth first. Stops early when the
    // callback returns false.
    void ForEachDescendant(const std::function<bool(Element&)>& visit);

    const Theme& GetTheme() const;

    void PlaceCentered();
    void PlaceAtCorner(Corner corner, float margin = 0.0f);
    void RequestFocus();

    // A press captures the pointer for the element it landed on, so the drag
    // that follows keeps reaching that element wherever the pointer goes.
    // These hand the capture to another element (a window taking over a drag
    // that began on its title, for example) or release it early.
    void CapturePointer();
    void ReleasePointer();
    bool HasPointerCapture() const;

    // Moves one of this element's material uniforms to a new value over the
    // given time. The element keeps drawing normally throughout; only the
    // value changes. Animating the same uniform again replaces the animation
    // in flight, starting from wherever it had reached.
    void AnimateUniform(const std::string& name, float x, float y = 0.0f, float z = 0.0f,
                        float w = 0.0f, float seconds = 0.2f, Easing easing = Easing::Smooth);
    void AnimateUniform(const std::string& name, Color color, float seconds = 0.2f,
                        Easing easing = Easing::Smooth);

    void MarkLayoutDirty();

protected:
    Rect m_Bounds;

    // Measures with the theme's font, or with a font of your own. Available to
    // any element, including user-written ones, so a custom widget can size
    // itself to its text.
    Vec2 MeasureText(const std::string& text) const;
    Vec2 MeasureText(const std::string& text, FontId font) const;
    float GetLineHeight() const;
    float GetLineHeight(FontId font) const;

    // The application this element belongs to, for loading an image, playing
    // a sound, or animating a material from inside an element. Invalid until
    // the element has been added to a tree.
    App GetApp() const;

private:
    friend class detail::UiTree;

    void AdoptChild(std::unique_ptr<Element> child);

    detail::UiTree* m_Tree = nullptr;
    Element* m_Parent = nullptr;
    std::vector<std::unique_ptr<Element>> m_Children;
    std::vector<Element*> m_ChildViews;

    bool m_Hovered = false;
    bool m_Pressed = false;
    bool m_Focused = false;

    bool m_Selected = false;

    // Where the Appearance is between states: the look it left, the look it
    // is heading for, and how far along it is.
    BoxStyle m_Look;
    BoxStyle m_LookFrom;
    int m_LookState = -1;
    float m_LookProgress = 1.0f;

    mutable std::shared_ptr<const void> m_Lifetime;
};

// A reference to an element that knows when the element is gone. Get()
// returns null once the element has been destroyed, however it was removed,
// where a raw pointer would dangle.
//
// Use it to hold on to an element you do not own, such as a popup opened in
// the overlay or a sibling. When two elements are removed together they are
// destroyed one after the other, and an ElementRef held by the second sees
// null instead of the destroyed first.
template <typename T>
class ElementRef
{
public:
    ElementRef() = default;

    ElementRef(T* element)
        : m_Element(element)
    {
        if (element != nullptr)
        {
            m_Lifetime = element->GetLifetime();
        }
    }

    T* Get() const { return m_Lifetime.expired() ? nullptr : m_Element; }
    T* operator->() const { return Get(); }
    explicit operator bool() const { return Get() != nullptr; }

    void Reset() { *this = ElementRef(); }

private:
    T* m_Element = nullptr;
    std::weak_ptr<const void> m_Lifetime;
};

// ---------------------------------------------------------------------------
// Built-in widgets
//
// These use only the public API above. Anything they do, an element of your
// own can do too.
// ---------------------------------------------------------------------------

class Panel : public Element
{
public:
    Color BackgroundColor{ 0, 0, 0, 0 };  // transparent means "use the theme"
    bool UseThemeSurface = true;
    float CornerRadius = -1.0f;           // negative means "use the theme"
    bool DrawBorder = true;

    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
};

class Label : public Element
{
public:
    std::string Text;
    TextAlign Align = TextAlign::Left;
    bool Muted = false;
    Color TextColor{ 0, 0, 0, 0 };        // transparent means "use the theme"

    // A font of its own. Leave it invalid to use the theme's font.
    FontId Font;

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
};

class Button : public Element
{
public:
    std::string Text;
    std::function<void()> OnClick;
    std::function<void()> OnHoverStart;
    std::function<void()> OnHoverEnd;
    bool Accent = false;

    Button();

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;

private:
    float m_HoverAmount = 0.0f;
    float m_PressAmount = 0.0f;
};

class Checkbox : public Element
{
public:
    std::string Text;
    bool Checked = false;
    std::function<void(bool)> OnChanged;

    Checkbox();

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    bool IsSelected() const override { return Checked; }
};

// ---------------------------------------------------------------------------
// Worlds
//
// opane can show a world (a ludifex World3D or World2D, or a type of your own)
// without depending on it. A world is four calls through an opaque pointer:
// advance it, set its image size, draw it, and return the texture it drew.
// HooksFor builds them from any object with these methods:
//
//   void  Update(float seconds);
//   void  SetRenderSize(int width, int height);
//   void  Render();
//   void* GetRenderTarget();   // an SDL_GPUTexture*
//
// The two libraries never include each other's headers; they meet only here.
// ---------------------------------------------------------------------------

struct WorldHooks
{
    void* Object = nullptr;
    void (*Update)(void* object, float seconds) = nullptr;
    void (*SetRenderSize)(void* object, int width, int height) = nullptr;
    void (*Render)(void* object) = nullptr;
    void* (*GetRenderTarget)(void* object) = nullptr;

    bool IsValid() const
    {
        return Object != nullptr && Update != nullptr && SetRenderSize != nullptr && Render != nullptr &&
               GetRenderTarget != nullptr;
    }
};

// True for anything that is not already a WorldHooks, so a WorldHooks passed
// to SetMainWorld or SetWorld is used as it is rather than wrapped again.
template <typename World>
concept HostableWorld = !std::is_same_v<std::remove_cvref_t<World>, WorldHooks>;

template <HostableWorld World>
WorldHooks HooksFor(World& world)
{
    WorldHooks hooks;
    hooks.Object = &world;
    hooks.Update = [](void* object, float seconds) { static_cast<World*>(object)->Update(seconds); };
    hooks.SetRenderSize = [](void* object, int width, int height) {
        static_cast<World*>(object)->SetRenderSize(width, height);
    };
    hooks.Render = [](void* object) { static_cast<World*>(object)->Render(); };
    hooks.GetRenderTarget = [](void* object) -> void* { return static_cast<World*>(object)->GetRenderTarget(); };
    return hooks;
}

// Shows a texture rendered elsewhere, usually a world, as an element in the
// tree, so a world can sit inside a layout with panels around it.
//
// Give it a world with SetWorld and it sizes the world's image to the element,
// advances and draws the world every frame, and shows the result. Or set
// Texture yourself to show any other texture.
class Viewport : public Element
{
public:
    TextureId Texture;

    // Drawn while no texture is set.
    Color EmptyColor{ 0.06f, 0.07f, 0.09f, 1.0f };

    bool DrawBorder = true;

    // Whether this viewport advances its world each frame. Turn it off when
    // something else already does (another viewport showing the same world,
    // or your own loop) so the world is not stepped twice.
    bool UpdatesWorld = true;

    // Called for pointer and wheel events inside the viewport, with Position
    // already converted to 0..1 across the element. Feed that straight to
    // World3D::PickFromView.
    std::function<void(const Event& viewEvent)> OnViewEvent;

    Viewport();
    ~Viewport() override;

    template <HostableWorld World>
    void SetWorld(World& world)
    {
        SetWorld(HooksFor(world));
    }
    void SetWorld(const WorldHooks& hooks);
    void ClearWorld();

    // Converts a window position into 0..1 across this element.
    Vec2 ToNormalized(Vec2 windowPoint) const;

    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;

private:
    WorldHooks m_World;
    void* m_WorldTarget = nullptr;
    int m_WorldWidth = 0;
    int m_WorldHeight = 0;
};

// Shows a texture. Measures to the texture's own size, so an image with no
// explicit Size lays out at its natural dimensions.
class Image : public Element
{
public:
    TextureId Texture;
    ImageFit Fit = ImageFit::Contain;
    Color Tint{ 1.0f, 1.0f, 1.0f, 1.0f };

    Image();

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
};

// A single line of editable text. Focus it by clicking, type into it, move
// about with the arrow keys and Home and End, select with Shift or by
// dragging, and cut, copy, and paste with the usual shortcuts.
class TextInput : public Element
{
public:
    std::string Text;

    // Shown, dimmed, while the field is empty.
    std::string Placeholder;

    // Zero means no limit. Counted in characters, not bytes, so an accented
    // letter counts once.
    int MaxLength = 0;

    // Draws every character as a dot, for a password.
    bool Masked = false;

    bool ReadOnly = false;

    // Called whenever the text changes, and when Enter is pressed.
    std::function<void(const std::string&)> OnChanged;
    std::function<void(const std::string&)> OnSubmitted;

    TextInput();

    // The caret and the selection, as byte offsets into Text.
    size_t GetCaret() const { return m_Caret; }
    void SetCaret(size_t offset);
    void SelectAll();

    // Ctrl+Z and Ctrl+Y, or Ctrl+Shift+Z. Typing in one burst undoes as one.
    void Undo();
    void Redo();

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;
    bool WantsText() const override;

private:
    std::string Visible() const;
    size_t OffsetAtX(float x) const;
    float XAtOffset(size_t offset) const;
    void Insert(const std::string& text);
    void DeleteSelection();
    void MoveCaret(size_t offset, bool extend);
    bool HasSelection() const { return m_Caret != m_Anchor; }

    // Keeps the caret and the selection inside Text and on character
    // boundaries, which assigning Text from outside can break.
    void Sanitize();
    void Remember(bool mergeable);
    void Changed();

    size_t m_Caret = 0;
    size_t m_Anchor = 0;
    float m_ScrollX = 0.0f;
    float m_Blink = 0.0f;
    bool m_Dragging = false;

    struct Snapshot
    {
        std::string Text;
        size_t Caret = 0;
    };
    std::vector<Snapshot> m_Undo;
    std::vector<Snapshot> m_Redo;
    float m_Clock = 0.0f;
    float m_LastEdit = -10.0f;
    float m_LastClick = -10.0f;
};

// A window onto a taller stack of children. Children are laid out by the
// ChildLayout as usual and then shifted by however far it is scrolled, so
// anything that works inside a panel works inside one of these.
class ScrollView : public Element
{
public:
    // How far down the content the view is, in interface units. Clamped to
    // the content.
    float Offset = 0.0f;

    // Interface units per wheel notch.
    float WheelStep = 60.0f;

    bool ShowBar = true;

    ScrollView();

    // How tall the children are altogether, and how much of that is out of
    // sight. Both are known after layout.
    float GetContentHeight() const { return m_ContentHeight; }
    float GetMaximumOffset() const;

    void ScrollTo(float offset);
    void ScrollBy(float amount) { ScrollTo(Offset + amount); }

    Rect GetContentBounds() const override;
    void Arrange(const Rect& bounds) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;

private:
    Rect BarBounds() const;

    float m_ContentHeight = 0.0f;
    float m_Shown = 0.0f;      // how far children have actually been shifted
    float m_BarFade = 0.0f;
    bool m_Dragging = false;
    float m_DragOrigin = 0.0f;
    float m_DragOffset = 0.0f;
};

class Slider : public Element
{
public:
    float Minimum = 0.0f;
    float Maximum = 1.0f;
    float Value = 0.5f;

    // What one arrow key moves it by, and what dragged values snap to. Zero
    // means a hundredth of the range and no snapping.
    float Step = 0.0f;

    std::function<void(float)> OnChanged;

    Slider();

    // Sets the value, clamped and snapped, and reports it if it changed.
    void SetValue(float value);

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;

private:
    void SetFromPointer(float pointerX);
    bool m_Dragging = false;
};

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

// A bar that fills as work completes. Indeterminate, it sweeps instead, for
// work whose length is not known.
class ProgressBar : public Element
{
public:
    float Value = 0.0f;            // 0 to 1
    bool Indeterminate = false;

    // Drawn over the bar, centred. "{}%" is replaced by the percentage.
    std::string Text;

    ProgressBar();

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnUpdate(float deltaSeconds) override;

private:
    float m_Sweep = 0.0f;
    float m_Shown = 0.0f;
};

// One of a set of choices. Radio buttons with the same Group under the same
// parent are one set: checking one unchecks the rest.
class RadioButton : public Element
{
public:
    std::string Text;
    std::string Group;
    bool Checked = false;
    std::function<void()> OnSelected;

    RadioButton();

    // Checks this one and unchecks the others in its set.
    void Select();

    bool IsSelected() const override { return Checked; }

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
};

// An on-off switch with a sliding knob.
class Toggle : public Element
{
public:
    std::string Text;
    bool On = false;
    std::function<void(bool)> OnChanged;

    Toggle();

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;
    bool IsSelected() const override { return On; }

private:
    float m_Knob = 0.0f;
};

// A number that is dragged to change, or typed. Drag sideways to scrub it,
// hold Shift to scrub slowly, double-click to type an exact value, and use
// the arrow keys to step it.
class NumberField : public Element
{
public:
    double Value = 0.0;
    double Minimum = -1.0e300;
    double Maximum = 1.0e300;

    // How far one arrow press, or one pixel of drag, moves the value.
    double Step = 1.0;

    // Decimal places shown. The value itself is kept at full precision.
    int Precision = 2;

    // Drawn before the number, dimmed: "X", "Width", "dB".
    std::string Prefix;
    std::string Suffix;

    std::function<void(double)> OnChanged;

    NumberField();

    void SetValue(double value);

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;
    bool WantsText() const override;

    // The value as it is shown, at the field's precision.
    std::string Format(double value) const;

private:
    void BeginEditing();
    void FinishEditing(bool commit);

    bool m_Dragging = false;
    bool m_Moved = false;
    float m_DragStartX = 0.0f;
    double m_DragStartValue = 0.0;
    bool m_Editing = false;
    std::string m_EditText;
    float m_Blink = 0.0f;
    float m_LastClick = -1.0f;
    float m_Clock = 0.0f;
};

// ---------------------------------------------------------------------------
// Popups
//
// Menus, drop-down lists, dialogs, and tooltips float above the interface in
// the overlay, App::GetOverlay(): a layer laid out over the whole window,
// painted after everything else, and offered the pointer first.
// ---------------------------------------------------------------------------

// Something that floats and goes away on its own. Put one in the overlay,
// fill it like any element, and open it where it belongs.
//
//   opane::Popup* popup = app.GetOverlay()->Add<opane::Popup>();
//   popup->ChildLayout = opane::LayoutMode::Vertical;
//   popup->Add<opane::Label>()->Text = "Hello";
//   popup->OpenBelow(*button);
//
// It stays on screen: opened near an edge, it moves in rather than being cut
// off, and opened below something near the bottom, it opens above instead.
class Popup : public Element
{
public:
    // A press outside the popup closes it and is not passed on, so the click
    // that dismisses a menu does not also press whatever was under it.
    bool DismissOnOutsideClick = true;
    bool DismissOnEscape = true;

    // Blocks and dims everything beneath it until it closes.
    bool Modal = false;
    Color ModalShade{ 0.0f, 0.0f, 0.0f, 0.45f };

    // The popup that opened this one. A press inside a submenu does not close
    // the menu that opened it, and closing a menu closes what it opened.
    Popup* Owner = nullptr;

    std::function<void()> OnClosed;

    Popup();

    void OpenAt(Vec2 windowPosition);
    void OpenBelow(const Element& anchor);
    void OpenBeside(const Element& anchor);
    void OpenCentered();
    void OpenBelowRect(const Rect& anchor);
    void OpenBesideRect(const Rect& anchor);

    // Virtual, so a popup that reports how it ended (a dialog closed with
    // Escape still reports "cancelled") sees every way it can close.
    virtual void Close();
    bool IsOpen() const { return Visible; }

    void Arrange(const Rect& bounds) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;

protected:
    // Called after a popup opens, before its first layout.
    virtual void OnOpened();

private:
    enum class Placement
    {
        AtPoint,
        Below,
        Beside,
        Centered
    };

    Placement m_Placement = Placement::AtPoint;
    Rect m_Anchor;
    ElementRef<Element> m_PreviousFocus;
};

// A key and the modifiers held with it: Ctrl+S, Ctrl+Shift+Z, F5.
struct Shortcut
{
    Key KeyCode = Key::Unknown;
    bool Control = false;
    bool Shift = false;
    bool Alt = false;

    bool IsValid() const { return KeyCode != Key::Unknown; }
    bool Matches(const Event& keyEvent) const;

    // "Ctrl+Shift+Z". Parse accepts the same form, case-insensitively, and
    // returns an invalid shortcut for anything it does not understand.
    std::string ToString() const;
    static Shortcut Parse(const std::string& text);
};

// One line of a menu: an action, a checkable option, a separator, or a
// submenu when it has Children.
struct MenuItem
{
    std::string Text;
    std::function<void()> OnSelected;

    // Shown on the right. It works anywhere in the window while the menu's
    // bar is in the tree, not only while the menu is open.
    Shortcut Keys;

    bool Enabled = true;
    bool Checkable = false;
    bool Checked = false;
    bool Separator = false;

    std::vector<MenuItem> Children;

    static MenuItem Action(const std::string& text, std::function<void()> onSelected,
                           const std::string& shortcut = {});
    static MenuItem Check(const std::string& text, bool checked, std::function<void()> onSelected);
    static MenuItem Submenu(const std::string& text, std::vector<MenuItem> children);
    static MenuItem Divider();
};

// A list of items that pops up: from a menu bar, as a context menu, or on
// its own. The arrow keys move through it, Right opens a submenu, Left and
// Escape close one, and Enter chooses.
class Menu : public Popup
{
public:
    std::vector<MenuItem> Items;

    Menu();
    ~Menu() override;

    void Close() override;

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;

    // The row under a window position, or -1.
    int RowAt(Vec2 windowPoint) const;

protected:
    void OnOpened() override;

private:
    Rect RowBounds(int index) const;
    void Choose(int index);
    void OpenSubmenu(int index);
    void CloseSubmenu();
    void MoveHighlight(int direction);

    int m_Highlight = -1;
    int m_SubmenuIndex = -1;
    ElementRef<Menu> m_Submenu;
    float m_HoverTime = 0.0f;
    int m_HoverRow = -1;
};

// A row of menu titles along the top of a window. Clicking a title opens its
// menu; while one is open, moving over another title switches to it. The
// shortcuts of every item work while the bar is in the tree.
class MenuBar : public Element
{
public:
    struct Entry
    {
        std::string Title;
        std::vector<MenuItem> Items;
    };

    std::vector<Entry> Menus;

    MenuBar();
    ~MenuBar() override;

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;
    bool OnShortcut(const Event& keyEvent) override;

private:
    Rect TitleBounds(int index) const;
    int TitleAt(Vec2 point) const;
    void OpenMenu(int index);

    ElementRef<Menu> m_Menu;
    int m_Open = -1;
    int m_Hover = -1;
};

// Picks one of several options from a list that drops down.
class Dropdown : public Element
{
public:
    std::vector<std::string> Options;
    int Selected = -1;

    // Shown while nothing is selected.
    std::string Placeholder = "Choose...";

    // At most this many rows show at once; the rest scroll.
    int VisibleRows = 8;

    std::function<void(int index)> OnChanged;

    Dropdown();
    ~Dropdown() override;

    void Select(int index);
    void Open();
    void CloseList();
    bool IsListOpen() const;

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;

private:
    ElementRef<Popup> m_List;
};

// A modal question: a title, a message, and a row of buttons. The callback
// gets the index of the button pressed, or -1 when it was dismissed.
class Dialog : public Popup
{
public:
    std::string Title;
    std::string Message;
    std::vector<std::string> Buttons{ "OK" };

    // Which button Enter presses, and which one Escape means. -1 for Escape
    // means "dismissed".
    int DefaultButton = 0;
    int CancelButton = -1;

    std::function<void(int button)> OnResult;

    Dialog();

    void Close() override;

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;

protected:
    void OnOpened() override;

private:
    Rect ButtonBounds(int index) const;
    void Finish(int button);

    int m_Hover = -1;
    int m_Pressed = -1;
    int m_Focus = 0;
    bool m_Finishing = false;
    bool m_KeyboardFocus = false;
};

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------

// Two panes with a divider between them that can be dragged. The first two
// children are the panes.
class Splitter : public Element
{
public:
    // False puts the panes side by side; true stacks them.
    bool Stacked = false;

    // The first pane's share of the space, 0 to 1.
    float Ratio = 0.5f;

    // Neither pane is dragged smaller than this, in interface units.
    float MinimumPane = 48.0f;

    float DividerThickness = 6.0f;

    std::function<void(float ratio)> OnRatioChanged;

    Splitter();

    Rect GetDividerBounds() const;

    Rect PlaceChild(Element& child, size_t index, const Rect& content) override;
    void Arrange(const Rect& bounds) override;
    bool HitTest(Vec2 point) const override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;

private:
    float FirstExtent(const Rect& content) const;

    bool m_Dragging = false;
    float m_GrabOffset = 0.0f;
};

// Pages behind a row of tabs. Add pages with AddPage; one is shown at a time.
class TabView : public Element
{
public:
    int Active = 0;

    // Draws a close button on each tab. OnCloseRequested decides; without
    // one, the page is removed.
    bool Closable = false;

    float TabHeight = 30.0f;

    std::function<void(int index)> OnActiveChanged;
    std::function<bool(int index)> OnCloseRequested;

    TabView();

    // A page is a plain element; lay out what it holds with its ChildLayout.
    Element* AddPage(const std::string& title);

    // Any element can be a page. The tree takes ownership.
    template <typename T, typename... Args>
    T* AddPageOf(const std::string& title, Args&&... args)
    {
        T* page = Add<T>(std::forward<Args>(args)...);
        SetTitle(page, title);
        return page;
    }

    void RemovePage(int index);
    int GetPageCount() const;
    Element* GetPage(int index) const;
    int IndexOf(const Element* page) const;

    std::string GetTitle(int index) const;
    void SetTitle(const Element* page, const std::string& title);

    void Activate(int index);

    Rect GetTabBounds(int index) const;

    Rect PlaceChild(Element& child, size_t index, const Rect& content) override;
    Rect GetContentBounds() const override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;
    void OnChildRemoved(Element& child) override;

private:
    Rect CloseBounds(int index) const;

    std::vector<std::pair<const Element*, std::string>> m_Titles;
    int m_HoverTab = -1;
    int m_HoverClose = -1;
};

// A movable, resizable frame with a title bar. Children go in the body below
// the title. Most often a child of the overlay, so it floats over everything.
class Window : public Element
{
public:
    std::string Title;
    bool Closable = true;
    bool Movable = true;
    bool Resizable = true;
    Vec2 MinimumSize{ 160.0f, 100.0f };
    float TitleHeight = 30.0f;

    // Return false to keep the window open.
    std::function<bool()> OnCloseRequested;
    std::function<void()> OnClosed;

    Window();

    // Where the window is, in its parent's space. Setting it moves the window.
    void SetFrame(const Rect& frame);
    Rect GetFrame() const;

    Rect GetTitleBounds() const;
    Rect GetContentBounds() const override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;

    // Starts moving the window as though its title had been pressed at this
    // point. A dock space uses it to hand a torn-off tab to the pointer.
    void BeginMove(Vec2 windowPoint);

protected:
    // Called while the window is being moved by its title, and when the move
    // ends. A dock space listens here to show where the window would dock.
    virtual void OnMoved(Vec2 pointer);
    virtual void OnMoveEnded(Vec2 pointer);

private:
    enum class Grip
    {
        None,
        Move,
        Left,
        Right,
        Bottom,
        BottomLeft,
        BottomRight
    };

    Grip GripAt(Vec2 point) const;
    Rect CloseBounds() const;

    Grip m_Grip = Grip::None;
    Vec2 m_GrabPoint;
    Rect m_GrabFrame;
    bool m_HoverClose = false;
};

// ---------------------------------------------------------------------------
// Docking
//
// A dock space holds panels and lets them be rearranged by dragging: tabbed
// together, split beside one another, or torn off to float. The arrangement
// can be saved to a string and restored.
// ---------------------------------------------------------------------------

// Something that lives in a dock space. Fill it like any element.
class DockPanel : public Element
{
public:
    std::string Title;

    // Names the panel in a saved layout. Defaults to the title; set it when
    // the title can change, such as a document named after its file.
    std::string Id;

    bool Closable = true;

    // Called when the panel is closed from its tab or its window. A closed
    // panel is hidden, not destroyed: DockSpace::Show brings it back.
    std::function<void()> OnClosed;

    DockPanel();

    const std::string& GetId() const { return Id.empty() ? Title : Id; }

    void Paint(DrawList& drawList) override;
};

enum class DockSide
{
    Center, // tabbed with whatever is there
    Left,
    Right,
    Top,
    Bottom
};

class DockSpace : public Element
{
public:
    // Tabs may be dragged out into floating windows. Off, a tab dragged away
    // from any target goes back where it was.
    bool AllowFloating = true;

    float TabHeight = 28.0f;
    float DividerThickness = 5.0f;
    float MinimumPane = 60.0f;

    std::function<void()> OnLayoutChanged;

    DockSpace();
    ~DockSpace() override;

    // Creates a panel and docks it. With no relative panel, the side is taken
    // relative to the whole space; with one, relative to that panel's group.
    // Share is the new panel's part of the split.
    template <typename T = DockPanel, typename... Args>
    T* AddPanel(const std::string& title, DockSide side = DockSide::Center, DockPanel* relativeTo = nullptr,
                float share = 0.3f, Args&&... args)
    {
        T* panel = Add<T>(std::forward<Args>(args)...);
        panel->Title = title;
        Dock(panel, side, relativeTo, share);
        return panel;
    }

    // Docks a panel that is already in this space, wherever it is now:
    // floating, closed, or docked somewhere else.
    void Dock(DockPanel* panel, DockSide side, DockPanel* relativeTo = nullptr, float share = 0.3f);

    // Tears a panel out into a floating window at a frame in window
    // coordinates.
    void Float(DockPanel* panel, const Rect& frame);

    // Hides a panel; Show brings it back where it last was.
    void ClosePanel(DockPanel* panel);
    void Show(DockPanel* panel);

    // Brings a panel's tab to the front of its group, and a floating one's
    // window to the front of the others.
    void Activate(DockPanel* panel);

    bool IsDocked(const DockPanel* panel) const;
    bool IsFloating(const DockPanel* panel) const;
    bool IsOpen(const DockPanel* panel) const;

    DockPanel* FindPanel(const std::string& id) const;
    std::vector<DockPanel*> GetPanels() const;

    // The arrangement as text: splits and their ratios, groups and their tabs,
    // floating windows and their frames, closed panels. Panels are named by
    // GetId(). The format is plain, versioned, and meant to be kept in a
    // settings file.
    std::string SaveLayout() const;

    // Restores a saved arrangement. Panels the text names but this space does
    // not have are skipped; panels this space has but the text does not name
    // are tabbed into the largest group. Returns false, changing nothing,
    // when the text is not a layout this build can read.
    bool LoadLayout(const std::string& layout);

    Rect PlaceChild(Element& child, size_t index, const Rect& content) override;
    void Arrange(const Rect& bounds) override;
    void Paint(DrawList& drawList) override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;
    void OnChildRemoved(Element& child) override;

    // Implementation. Declared here so the pieces inside this space can reach
    // it; not meant to be used directly.
    struct Node;
    class FloatWindow;
    class Guides;

private:
    struct Target
    {
        Node* Group = nullptr;   // null: relative to the whole space
        DockSide Side = DockSide::Center;
        bool Valid = false;
        Rect Preview;
    };

    struct ClosedPanel
    {
        DockPanel* Panel = nullptr;

        // A panel that shared its group, so Show can put it back beside it.
        DockPanel* Neighbour = nullptr;
    };

    Node* FindGroup(const DockPanel* panel) const;
    Node* GroupAt(Vec2 point) const;
    Node* LargestGroup() const;
    FloatWindow* FindWindow(const DockPanel* panel) const;
    void Detach(DockPanel* panel);
    void Unfloat(DockPanel* panel);
    void Prune();
    void Insert(DockPanel* panel, DockSide side, Node* group, float share);
    void LayoutNodes(Node* node, const Rect& bounds);
    void PaintNode(Node* node, DrawList& drawList);
    Rect TabRect(const Node* group, int index) const;
    Rect CloseRect(const Node* group, int index) const;
    int TabAt(const Node* group, Vec2 point) const;
    Node* DividerAt(Node* node, Vec2 point) const;
    Target TargetAt(Vec2 point, const DockPanel* moving) const;
    void EnsureGuides();
    void ShowGuides(bool shown);
    void Changed();
    void CollectPanels(std::vector<DockPanel*>& out) const;

    void BeginWindowDrag(FloatWindow* window, Vec2 pointer);
    void UpdateWindowDrag(Vec2 pointer);
    void EndWindowDrag(Vec2 pointer);
    void ForgetWindow(FloatWindow* window);

    std::unique_ptr<Node> m_Root;
    std::vector<FloatWindow*> m_Floating;
    std::vector<ClosedPanel> m_Closed;
    Guides* m_Guides = nullptr;

    // A tab being dragged, and the group it came from.
    DockPanel* m_DragPanel = nullptr;
    Node* m_DragGroup = nullptr;
    Vec2 m_DragStart;
    bool m_DragLive = false;
    bool m_Reordering = false;
    Vec2 m_Pointer;
    Target m_Target;

    // A divider being dragged.
    Node* m_ResizeNode = nullptr;

    // A floating window being dragged by its title.
    FloatWindow* m_DragWindow = nullptr;

    Node* m_HoverGroup = nullptr;
    int m_HoverTab = -1;
    bool m_HoverClose = false;
    DockPanel* m_PressClose = nullptr;
};

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------

// A column of selectable rows. Only the rows in view are drawn, so very long
// lists stay fast.
class ListView : public Element
{
public:
    std::vector<std::string> Items;

    // -1 when nothing is selected.
    int Selected = -1;

    // Ctrl-click and Shift-click add to the selection.
    bool MultiSelect = false;

    float RowHeight = 26.0f;

    std::function<void(int index)> OnSelected;

    // A double-click, or Enter.
    std::function<void(int index)> OnActivated;

    // A right-click on a row, with the window position for a context menu.
    std::function<void(int index, Vec2 position)> OnContextMenu;

    ListView();

    bool IsSelected(int index) const;
    const std::vector<int>& GetSelection() const { return m_Selection; }
    void SetSelection(const std::vector<int>& indices);

    void ScrollTo(int index);
    int RowAt(Vec2 windowPoint) const;

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;

private:
    void SelectRow(int index, bool extend, bool toggle);
    float MaximumScroll() const;

    std::vector<int> m_Selection;
    int m_Anchor = -1;
    float m_Scroll = 0.0f;
    float m_LastClick = -1.0f;
    int m_LastClickRow = -1;
    float m_Clock = 0.0f;
};

// A hierarchy of rows that open and close. The items are data: rebuild them
// when what they describe changes, and the open state and selection carry
// over by Id.
class TreeView : public Element
{
public:
    struct Item
    {
        std::string Text;

        // Identifies the item across rebuilds. Give every item a distinct one.
        uint64_t Id = 0;

        std::vector<Item> Children;
        bool Expanded = false;
    };

    std::vector<Item> Items;

    // 0 when nothing is selected.
    uint64_t Selected = 0;

    float RowHeight = 24.0f;
    float Indent = 18.0f;

    std::function<void(const Item& item)> OnSelected;
    std::function<void(const Item& item)> OnActivated;
    std::function<void(const Item& item, Vec2 position)> OnContextMenu;
    std::function<void(const Item& item, bool expanded)> OnExpanded;

    TreeView();

    Item* Find(uint64_t id);
    void Select(uint64_t id);
    void ExpandAll(bool expanded);

    // Opens every ancestor of the item and scrolls it into view.
    void Reveal(uint64_t id);

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;

private:
    struct Row
    {
        Item* Target = nullptr;
        int Depth = 0;
    };

    void Flatten();
    int RowIndexOf(uint64_t id) const;
    int RowAt(Vec2 point) const;
    float MaximumScroll() const;
    void ScrollToRow(int row);
    void SetExpanded(Item& item, bool expanded);

    std::vector<Row> m_Rows;
    float m_Scroll = 0.0f;
    float m_LastClick = -1.0f;
    uint64_t m_LastClickId = 0;
    float m_Clock = 0.0f;
};

// ---------------------------------------------------------------------------
// Multi-line text
// ---------------------------------------------------------------------------

// Editable text over many lines: the caret, selection, and shortcuts of a
// TextInput, plus Enter for a new line, the vertical arrows, Page Up and
// Down, word wrap, and undo.
class TextArea : public Element
{
public:
    std::string Text;
    std::string Placeholder;
    bool ReadOnly = false;

    // Breaks long lines at spaces to fit the width, rather than scrolling
    // sideways.
    bool WordWrap = true;

    // Tab inserts this many spaces rather than moving focus. Zero gives Tab
    // back to focus traversal.
    int TabSpaces = 4;

    std::function<void(const std::string&)> OnChanged;

    TextArea();

    size_t GetCaret() const { return m_Caret; }
    void SetCaret(size_t offset);
    void SelectAll();
    std::string GetSelectedText() const;

    // Replaces the selection, as typing would, and records it for undo.
    void InsertText(const std::string& text);

    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }
    void Undo();
    void Redo();

    Vec2 Measure(Vec2 available) override;
    void Paint(DrawList& drawList) override;
    const Style* GetDefaultAppearance(const Theme& theme) const override;
    void OnEvent(Event& event) override;
    void OnUpdate(float deltaSeconds) override;
    bool WantsText() const override;

private:
    struct Line
    {
        size_t Start = 0;
        size_t End = 0;   // exclusive, before any newline
    };

    struct Change
    {
        size_t At = 0;
        std::string Removed;
        std::string Inserted;
        size_t CaretBefore = 0;
        size_t CaretAfter = 0;
    };

    void Reflow();
    int LineOf(size_t offset) const;
    float XOf(size_t offset) const;
    size_t OffsetAt(int line, float x) const;
    size_t OffsetAtPoint(Vec2 point) const;
    void Replace(size_t from, size_t to, const std::string& text, bool mergeable);
    void MoveCaret(size_t offset, bool extend);
    void EnsureCaretVisible();
    bool HasSelection() const { return m_Caret != m_Anchor; }
    Rect TextBounds() const;

    std::vector<Line> m_Lines;
    std::string m_FlowedText;
    float m_FlowedWidth = -1.0f;

    size_t m_Caret = 0;
    size_t m_Anchor = 0;
    float m_GoalX = -1.0f;
    float m_Scroll = 0.0f;
    float m_ScrollX = 0.0f;
    float m_Blink = 0.0f;
    bool m_Dragging = false;

    std::vector<Change> m_Undo;
    std::vector<Change> m_Redo;
    float m_Clock = 0.0f;
    float m_LastEdit = -10.0f;
    float m_LastClick = -10.0f;
};

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

// The graphics API everything is drawn with.
enum class GraphicsBackend
{
    // The first this machine runs, tried in the order Metal, Direct3D 12,
    // Vulkan: Direct3D 12 on Windows, falling back to Vulkan where Direct3D 12
    // is missing; Metal on Apple hardware; Vulkan on Linux. The SDL_GPU_DRIVER
    // environment variable ("direct3d12", "vulkan", or "metal") overrides the
    // order, so another backend can be tried without rebuilding.
    Automatic,
    Direct3D12,
    Vulkan,
    Metal,
};

// "Automatic", "Direct3D 12", "Vulkan", or "Metal", for a settings screen.
const char* GetGraphicsBackendName(GraphicsBackend backend);

// True when this build of opane carries shaders for the backend and this
// machine can run it. Automatic asks whether any backend is usable. Cheap
// enough for a settings screen, not for every frame.
bool IsGraphicsBackendAvailable(GraphicsBackend backend);

struct AppConfig
{
    std::string Title = "opane";

    // The window's icon: an image found through the asset roots, ideally
    // square and at least 256 pixels. Empty keeps the icon the program was
    // built with (see opane_set_app_icon, which also sets the .exe icon on
    // Windows and the app bundle icon on macOS), or the system default.
    std::string Icon;
    int Width = 1280;
    int Height = 720;
    bool Resizable = true;
    bool VSync = true;
    bool HighDpi = true;
    bool DebugGpu = false;

    // Anything but Automatic is strict: when this machine cannot run the
    // backend named, StartApp fails and logs why instead of falling back to
    // another, and SDL_GPU_DRIVER is ignored.
    GraphicsBackend Backend = GraphicsBackend::Automatic;

    // Pixels per interface unit. Zero follows the display: 1 at 100%, 1.5 at
    // 150%, 2 on a Retina screen, updated when the window moves to a display
    // with a different scale. Sizes, offsets, and fonts are all in interface
    // units, so an interface looks the same size on every display and stays
    // sharp.
    float UiScale = 0.0f;

    // Opens the window hidden, for automated tests that drive the interface
    // with synthetic input and for tools that only draw offscreen. Layout,
    // input, and painting still run; nothing is shown.
    bool Hidden = false;

    // A window with no frame from the system, for an interface that draws its
    // own title bar and edges: an element whose Region is Drag moves the
    // window, and one marked Resize... resizes it. Pressing within
    // ResizeBorder units of an edge resizes it too, unless it is maximized.
    bool Borderless = false;
    float ResizeBorder = 6.0f;

    Color ClearColor = Color{ 0.06f, 0.07f, 0.09f, 1.0f };
};

// The application owns the window, the GPU device, and the frame loop.
//
// App is a non-owning view onto internal state. Copying an App copies the
// view, not the application; Shutdown() releases the underlying resources.
class App
{
public:
    App() = default;

    bool IsValid() const { return m_State != nullptr; }
    bool IsOpen() const;

    // Pumps the event queue and returns the seconds elapsed since the previous
    // call, clamped so that stopping in a debugger does not produce a huge
    // time step.
    float PollEvents();

    // Acquires a command buffer and the swapchain texture, then begins a render
    // pass that clears to the current clear color.
    void BeginFrame();

    // Ends the render pass and submits the command buffer.
    void EndFrame();

    // Convenience loop. The callback runs once per frame before the frame is
    // begun, so state set inside it takes effect in the same frame. A main
    // world, if one is set, is advanced after the callback and drawn beneath
    // everything the callback and the interface paint.
    void Run(const std::function<void(float deltaSeconds)>& onFrame);

    // The same loop with no callback: the interface and the main world are
    // the whole program. Returns when the window closes.
    void Run();

    // Shows a world beneath the interface, filling the window: every frame it
    // is sized to the window, advanced, drawn, and composited under the UI.
    // Anything HooksFor accepts works: a ludifex World3D or World2D, or your
    // own type. The world must outlive the loop it is shown in.
    template <HostableWorld World>
    void SetMainWorld(World& world)
    {
        SetMainWorld(HooksFor(world));
    }
    void SetMainWorld(const WorldHooks& hooks);
    void ClearMainWorld();

    // For a hand-written loop: sizes the world to the window, draws it, and
    // records it as a full-window image beneath whatever is painted after.
    // Call it between PollEvents and BeginFrame, before painting anything that
    // belongs on top. It does not advance the world; call its Update yourself.
    template <HostableWorld World>
    void RenderWorld(World& world)
    {
        RenderWorld(HooksFor(world));
    }
    void RenderWorld(const WorldHooks& hooks);

    void Close();
    void Shutdown();

    void SetTitle(const std::string& title);

    // For a window that draws its own title bar.
    void MinimizeWindow();
    void MaximizeWindow();
    void RestoreWindow();
    bool IsWindowMaximized() const;

    // Loads a theme from a JSON file over the current one: colours, sizes, a
    // font, looks for the built-in widgets, and looks of your own by name for
    // StyleName. With hotReload, saving the file restyles the interface while
    // it runs; a file with a mistake keeps the theme that worked and logs the
    // line and column. False, having logged why, when it cannot be loaded.
    bool LoadTheme(const std::string& path, bool hotReload = true);

    // Changes the window's icon while the program runs: an image found
    // through the asset roots. False, having logged why, when it cannot be
    // loaded; the icon is then left as it was.
    bool SetIcon(const std::string& path);
    void SetClearColor(Color color);
    Color GetClearColor() const;

    // The window's size in interface units: the space the root element lays
    // out in, and the units every pointer position arrives in.
    Vec2 GetWindowSize() const;

    // The window's size in pixels, for anything that renders at the display's
    // resolution rather than laying out on it.
    Vec2 GetPixelSize() const;

    // Pixels per interface unit. SetUiScale(0) goes back to following the
    // display.
    float GetUiScale() const;
    void SetUiScale(float scale);

    float GetTimeSeconds() const;
    uint64_t GetFrameCount() const;

    const Input& GetInput() const;

    // The draw list for the frame being built. It is cleared by PollEvents and
    // submitted by BeginFrame, so painting belongs between those two calls.
    // Run calls its callback there.
    DrawList& GetDrawList();

    // Uploads pixels and returns a handle. Rgba8 expects four bytes per pixel,
    // Alpha8 one. The pixels are copied, so the caller's buffer can go away.
    TextureId CreateTexture(int width, int height, PixelFormat format, const void* pixels);

    // Loads an image file (PNG, JPEG, BMP, TGA, GIF, PSD, or HDR) through the
    // asset roots, with a full mip chain so it stays smooth when drawn
    // smaller. The same name returns the same texture. A file that cannot be
    // found or decoded returns a magenta-and-black checkerboard and logs why.
    TextureId LoadTexture(const std::string& path);

    void DestroyTexture(TextureId texture);
    Vec2 GetTextureSize(TextureId texture) const;

    // Wraps a texture another library owns, such as a world's render target,
    // so it can be drawn like any other. opane does not take ownership:
    // DestroyTexture releases the handle, never the texture.
    // Pass an SDL_GPUTexture*.
    TextureId WrapExternalTexture(void* sdlGpuTexture, int width, int height);

    // The system clipboard, as UTF-8. Empty when it holds no text.
    std::string GetClipboardText() const;
    void SetClipboardText(const std::string& text);

    // Loads a TrueType font at one size. Load the same file again for another
    // size.
    FontId LoadFont(const std::string& path, float pixelHeight);

    // A system font found at startup, so text works before any font is loaded.
    FontId GetDefaultFont() const;

    Vec2 MeasureText(FontId font, const std::string& text) const;

    // How wide a line of runs is, and how tall wrapped text would be.
    Vec2 MeasureRichText(const std::vector<TextRun>& runs, FontId font) const;
    Vec2 MeasureTextWrapped(FontId font, const std::string& text, float maximumWidth) const;

    float GetFontLineHeight(FontId font) const;

    // Decoded once and kept in memory. Use it for short effects.
    SoundId LoadSound(const std::string& path, AudioGroup group = AudioGroup::Effects);

    // Streamed from disk instead of decoded up front. Use it for music.
    SoundId LoadMusic(const std::string& path);

    void PlaySound(SoundId sound, const SoundPlayback& playback = {});
    void StopSound(SoundId sound);
    void StopGroup(AudioGroup group);
    bool IsSoundPlaying(SoundId sound) const;
    void DestroySound(SoundId sound);

    // Positional playback. The sound is attenuated by its distance from the
    // listener, so a world can place sounds where things happen.
    void PlaySoundAt(SoundId sound, Vec3 position, const SpatialPlayback& playback = {});
    void SetSoundPosition(SoundId sound, Vec3 position);

    // Where the listener is and which way it faces. Drive this from your
    // camera each frame; until you do, it sits at the origin facing -Z.
    void SetListener(Vec3 position, Vec3 forward, Vec3 up = Vec3{ 0.0f, 1.0f, 0.0f });

    void SetGroupVolume(AudioGroup group, float volume);
    float GetGroupVolume(AudioGroup group) const;

    // False when no audio device could be opened. Every audio call is then a
    // safe no-op.
    bool IsAudioRunning() const;

    // Compiles and returns a material. On failure the returned id is invalid
    // and the compiler's message has gone to the log.
    MaterialId CreateMaterial(const MaterialDesc& desc);
    void DestroyMaterial(MaterialId material);

    // Setting an unknown name is a no-op with a warning.
    void SetMaterialUniform(MaterialId material, const std::string& name, float x, float y = 0.0f,
                            float z = 0.0f, float w = 0.0f);
    void SetMaterialUniform(MaterialId material, const std::string& name, Color color);

    // Moves a uniform to a new value over the given time. Animating the same
    // uniform again replaces the animation in flight, starting from the value
    // it had reached.
    void AnimateMaterialUniform(MaterialId material, const std::string& name, float x, float y = 0.0f,
                                float z = 0.0f, float w = 0.0f, float seconds = 0.2f,
                                Easing easing = Easing::Smooth);
    void AnimateMaterialUniform(MaterialId material, const std::string& name, Color color,
                                float seconds = 0.2f, Easing easing = Easing::Smooth);
    void CancelMaterialAnimations(MaterialId material);

    // The root of the element tree. It fills the window and is where interface
    // construction starts.
    Element* GetRoot();

    // A layer over the whole interface, laid out over the window, painted
    // after everything else, and offered the pointer first. Popups, menus,
    // dialogs, and floating windows live here. It lets the pointer through
    // wherever nothing in it is.
    Element* GetOverlay();

    // The element that has the keyboard, or null.
    Element* GetFocusedElement() const;

    // True while the element is somewhere in this interface. Addresses are
    // compared, never followed, so asking about one that may already have
    // been removed is safe.
    bool ContainsElement(const Element* element) const;

    // A context menu at a window position, built fresh from the items. One
    // at a time: showing another replaces the first.
    Menu* ShowMenu(const std::vector<MenuItem>& items, Vec2 windowPosition);

    // A modal dialog with a message and buttons. The callback gets the index
    // of the button pressed, or -1 when it was dismissed.
    Dialog* ShowDialog(const std::string& title, const std::string& message,
                       const std::vector<std::string>& buttons = { "OK" },
                       std::function<void(int button)> onResult = {});

    const Theme& GetTheme() const;
    void SetTheme(const Theme& theme);

    // Lays out, updates, and paints the element tree. Run calls this after the
    // frame callback, so the interface draws on top of anything the callback
    // painted. A hand-written loop calls it in the same place.
    void UpdateInterface(float deltaSeconds);

    // The backend the device was created on: never Automatic.
    GraphicsBackend GetGraphicsBackend() const;

    // The SDL window and GPU device opane uses, for anything the library does
    // not cover.
    SDL_Window* GetWindow() const;
    SDL_GPUDevice* GetGpuDevice() const;

private:
    friend App StartApp(const AppConfig& config);
    friend App detail::MakeAppView(detail::AppState* state);

    detail::AppState* m_State = nullptr;
};

// Initializes SDL, creates the window and GPU device, and returns the
// application. On failure the returned App is invalid, IsValid() is false, and
// the reason has been reported through the log handler.
App StartApp(const AppConfig& config = {});

} // namespace opane
