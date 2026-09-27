// Font loading and the glyph atlas. Not installed and not part of the public
// API.
//
// Glyphs are packed into single-channel atlases, one per font file per size
// that is in use, and rasterized the first time they are needed, so only the
// characters a program shows are rasterized.
//
// Small text is rasterized as coverage at the size it is drawn, because thin
// strokes at those sizes are thinner than one texel of a practical distance
// field. Larger text is one distance field, magnified, which stays sharp at
// any size and lets a material shade the letters.

#pragma once

#include "Internal.h"

#include <memory>
#include <string>
#include <vector>

namespace opane
{

class Renderer;

namespace detail
{

// Where a glyph is in the atlas and where it sits relative to the pen, in
// pixels at the font's own size.
struct GlyphMetrics
{
    // Atlas region, in normalized texture coordinates.
    float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;

    float OffsetX = 0.0f, OffsetY = 0.0f;
    float Width = 0.0f, Height = 0.0f;
    float Advance = 0.0f;
};

// One loaded font file: the atlas, the rasterizer, and every glyph rasterized
// so far. Several sizes of the same file share one face.
struct FaceRecord;

// A face at a size. This is what a FontId names.
struct FontRecord
{
    uint32_t Generation = 0;
    bool Alive = false;

    FaceRecord* Face = nullptr;

    float PixelHeight = 0.0f;
    float Scale = 1.0f; // em-space metrics to pixels at this size
    float Ascent = 0.0f;
    float Descent = 0.0f;
    float LineHeight = 0.0f;

    TextureId Atlas;
    std::string Name;
};

// Decodes one UTF-8 character, advancing index past it. Malformed bytes decode
// to U+FFFD and advance by one, so broken text draws as replacement characters
// rather than hanging or running off the end.
uint32_t DecodeUtf8(const std::string& text, size_t& index);

// Appends one character as UTF-8. Used by text editing.
void AppendUtf8(std::string& text, uint32_t codepoint);

class FontStore
{
public:
    FontStore();
    ~FontStore();

    FontStore(const FontStore&) = delete;
    FontStore& operator=(const FontStore&) = delete;

    void Initialize(Renderer* renderer);
    void Shutdown();

    FontId LoadFromFile(const std::string& path, float pixelHeight);
    FontId LoadDefault(float pixelHeight);
    void Destroy(FontId id);

    // Pixels per interface unit. Fonts are asked for in interface units and
    // rasterized at that times this, so text stays sharp at any scale.
    void SetUiScale(float scale);
    float GetUiScale() const { return m_UiScale; }

    const FontRecord* Resolve(const FontId& id) const;

    // Fills in where the glyph is and how it sits, in pixels at this font's
    // size, rasterizing it if this is the first time it has been asked for.
    bool Glyph(const FontRecord& font, uint32_t codepoint, GlyphMetrics& metrics);

    // How much closer the second character sits to the first, in pixels.
    float Kerning(const FontRecord& font, uint32_t first, uint32_t second);

    // Whether this size is drawn from a distance field or from coverage.
    bool IsField(const FontRecord& font) const;

    Vec2 Measure(const FontId& id, const std::string& text);

    // Where the text has to break to fit the width, as byte offsets of the
    // start of each line. Breaks at spaces where it can and mid-word where a
    // single word is wider than the whole line.
    std::vector<size_t> WrapLines(const FontId& id, const std::string& text, float maximumWidth);

    float GetLineHeight(const FontId& id) const;

    // Grows any atlas that ran out of room last frame. Called before anything
    // paints: growing repacks the atlas, which moves every glyph in it, so it
    // must never happen between one glyph being drawn and another.
    void BeginFrame();

    // Sends newly rasterized glyphs to the GPU. Called once a frame, after
    // painting and before anything is submitted, so a glyph asked for while
    // painting is on the card by the time the batch that uses it is drawn.
    void UploadPending();

private:
    FaceRecord* AcquireFace(const std::string& path, float em, bool field);

    Renderer* m_Renderer = nullptr;
    float m_UiScale = 1.0f;
    std::vector<std::unique_ptr<FaceRecord>> m_Faces;
    std::vector<FontRecord> m_Fonts;
    std::vector<uint32_t> m_FreeFonts;
};

} // namespace detail
} // namespace opane
