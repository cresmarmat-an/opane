// fopen is used on purpose; the _s variants are not portable.
#define _CRT_SECURE_NO_WARNINGS

#include "Text.h"

#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

// stb_truetype is a single-header library compiled with internal linkage, so
// the parts opane does not call are reported as unused. That is expected, and
// the warnings belong to the dependency rather than to this project.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505) // unreferenced local function removed
#pragma warning(disable : 4996) // deprecated CRT function
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace opane
{
namespace
{

// Coverage glyphs go through the same path as any other mask; field glyphs
// have a mode of their own, because the shader has to recover coverage from
// the field rather than read it.
constexpr float TextureModeMask = 2.0f;
constexpr float TextureModeGlyph = 4.0f;

// Text this size and smaller is rasterized as coverage, at the size it will be
// drawn. A distance field cannot represent strokes thinner than one of its
// texels, and at these sizes thin details such as a hyphen or a comma's tail
// would appear or disappear depending on the subpixel position. Rasterized
// coverage does not have this problem.
constexpr float LargestCoverageHeight = 26.0f;

// Larger text is one distance field, magnified. This also lets a material
// shade the letters (an outline, a glow, a gradient), which coverage alone
// cannot do.
constexpr float FieldHeight = 64.0f;

bool WantsField(float pixelHeight)
{
    return pixelHeight > LargestCoverageHeight;
}

// How far the field reaches beyond the outline, in pixels at EmPixels. This
// limits how far outside a glyph's edge a shader can draw (an outline, a glow,
// a shadow), and lets text be scaled up a long way before its corners round
// off.
constexpr int FieldReach = 6;

// The field is stored in a byte: 128 is the edge, and one pixel of distance is
// this many steps away from it.
constexpr unsigned char EdgeValue = 128;
constexpr float DistanceScale = 128.0f / static_cast<float>(FieldReach);

constexpr int FirstAtlasSize = 512;
constexpr int LastAtlasSize = 4096;

// Glyphs are packed this far apart so one letter's field never bleeds into
// the next one's edge.
constexpr int GlyphGap = 2;

constexpr uint32_t ReplacementCharacter = 0xFFFD;

// Tried in order when no font has been loaded, so text works out of the box.
const char* const DefaultFontCandidates[] = {
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/tahoma.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/Library/Fonts/Arial.ttf",
};

bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& outBytes)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (size <= 0)
    {
        std::fclose(file);
        return false;
    }

    outBytes.resize(static_cast<size_t>(size));
    const size_t read = std::fread(outBytes.data(), 1, outBytes.size(), file);
    std::fclose(file);

    return read == outBytes.size();
}

} // namespace

namespace detail
{

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

uint32_t DecodeUtf8(const std::string& text, size_t& index)
{
    if (index >= text.size())
    {
        return 0;
    }

    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80)
    {
        ++index;
        return lead;
    }

    int continuations = 0;
    uint32_t codepoint = 0;
    if ((lead & 0xE0) == 0xC0)
    {
        continuations = 1;
        codepoint = lead & 0x1Fu;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        continuations = 2;
        codepoint = lead & 0x0Fu;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        continuations = 3;
        codepoint = lead & 0x07u;
    }
    else
    {
        // A stray continuation byte or an invalid lead. One byte is consumed
        // so the caller always makes progress.
        ++index;
        return ReplacementCharacter;
    }

    if (index + static_cast<size_t>(continuations) >= text.size())
    {
        ++index;
        return ReplacementCharacter;
    }

    for (int step = 1; step <= continuations; ++step)
    {
        const auto byte = static_cast<unsigned char>(text[index + static_cast<size_t>(step)]);
        if ((byte & 0xC0) != 0x80)
        {
            ++index;
            return ReplacementCharacter;
        }
        codepoint = (codepoint << 6) | (byte & 0x3Fu);
    }

    index += static_cast<size_t>(continuations) + 1;
    return codepoint;
}

void AppendUtf8(std::string& text, uint32_t codepoint)
{
    if (codepoint < 0x80)
    {
        text.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800)
    {
        text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint < 0x10000)
    {
        text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------

struct FaceRecord
{
    std::string Path;

    // The height its glyphs were rasterized at, and whether they are distance
    // fields or coverage.
    float Em = 0.0f;
    bool Field = false;
    std::vector<unsigned char> Bytes;
    stbtt_fontinfo Info{};
    bool Ready = false;
    int Users = 0;

    // Font units to pixels at Em.
    float EmScale = 0.0f;

    // Vertical metrics, in pixels at Em.
    float Ascent = 0.0f;
    float Descent = 0.0f;
    float LineHeight = 0.0f;

    // The atlas, the shelf packer that fills it, and the CPU copy that is
    // uploaded when it changes.
    int Size = 0;
    std::vector<unsigned char> Pixels;
    TextureId Atlas;
    bool Dirty = false;
    int ShelfX = 0;
    int ShelfY = 0;
    int ShelfHeight = 0;

    // Set when a glyph did not fit. The atlas grows at the start of the next
    // frame rather than at once, because growing repacks it.
    bool NeedsGrow = false;

    // Rasterized so far, in pixels at Em.
    std::unordered_map<uint32_t, GlyphMetrics> Glyphs;
};

namespace
{

void ResetAtlas(FaceRecord& face, int size)
{
    face.Size = size;
    face.Pixels.assign(static_cast<size_t>(size) * static_cast<size_t>(size), 0);
    face.ShelfX = 0;
    face.ShelfY = 0;
    face.ShelfHeight = 0;
    face.Dirty = true;
}

// Finds room for a glyph on the current shelf, or starts a new one. False when
// the atlas is full.
bool Place(FaceRecord& face, int width, int height, int& outX, int& outY)
{
    if (width > face.Size || height > face.Size)
    {
        return false;
    }

    if (face.ShelfX + width > face.Size)
    {
        face.ShelfY += face.ShelfHeight + GlyphGap;
        face.ShelfX = 0;
        face.ShelfHeight = 0;
    }

    if (face.ShelfY + height > face.Size)
    {
        return false;
    }

    outX = face.ShelfX;
    outY = face.ShelfY;
    face.ShelfX += width + GlyphGap;
    face.ShelfHeight = std::max(face.ShelfHeight, height);
    return true;
}

// Rasterizes one glyph into the atlas. False when it did not fit.
bool Rasterize(FaceRecord& face, uint32_t codepoint, GlyphMetrics& metrics)
{
    // A codepoint this face does not have draws as its .notdef box instead of
    // nothing.
    const int glyph = stbtt_FindGlyphIndex(&face.Info, static_cast<int>(codepoint));

    int advance = 0;
    int bearing = 0;
    stbtt_GetGlyphHMetrics(&face.Info, glyph, &advance, &bearing);

    metrics = GlyphMetrics{};
    metrics.Advance = static_cast<float>(advance) * face.EmScale;

    int width = 0;
    int height = 0;
    int offsetX = 0;
    int offsetY = 0;
    unsigned char* field =
        face.Field ? stbtt_GetGlyphSDF(&face.Info, face.EmScale, glyph, FieldReach, EdgeValue,
                                       DistanceScale, &width, &height, &offsetX, &offsetY)
                   : stbtt_GetGlyphBitmap(&face.Info, face.EmScale, face.EmScale, glyph, &width,
                                          &height, &offsetX, &offsetY);
    if (field == nullptr)
    {
        // A space, or anything else with no outline: it still advances the pen.
        return true;
    }

    int x = 0;
    int y = 0;
    if (!Place(face, width, height, x, y))
    {
        stbtt_FreeSDF(field, nullptr);
        return false;
    }


    for (int row = 0; row < height; ++row)
    {
        std::memcpy(face.Pixels.data() + static_cast<size_t>(y + row) * static_cast<size_t>(face.Size) +
                        static_cast<size_t>(x),
                    field + static_cast<size_t>(row) * static_cast<size_t>(width),
                    static_cast<size_t>(width));
    }
    // Both paths allocate through stb_truetype's allocator, and both are freed
    // the same way.
    stbtt_FreeSDF(field, nullptr);

    const float inverse = 1.0f / static_cast<float>(face.Size);
    metrics.U0 = static_cast<float>(x) * inverse;
    metrics.V0 = static_cast<float>(y) * inverse;
    metrics.U1 = static_cast<float>(x + width) * inverse;
    metrics.V1 = static_cast<float>(y + height) * inverse;
    metrics.OffsetX = static_cast<float>(offsetX);
    metrics.OffsetY = static_cast<float>(offsetY);
    metrics.Width = static_cast<float>(width);
    metrics.Height = static_cast<float>(height);

    face.Dirty = true;
    return true;
}

// Doubles the atlas and puts every glyph rasterized so far back into it, so no
// texture coordinate handed out earlier is left pointing at the old packing.
bool Grow(FaceRecord& face)
{
    if (face.Size >= LastAtlasSize)
    {
        return false;
    }

    std::vector<uint32_t> known;
    known.reserve(face.Glyphs.size());
    for (const auto& entry : face.Glyphs)
    {
        known.push_back(entry.first);
    }

    ResetAtlas(face, face.Size * 2);

    for (const uint32_t codepoint : known)
    {
        GlyphMetrics metrics;
        if (!Rasterize(face, codepoint, metrics))
        {
            return false;
        }
        face.Glyphs[codepoint] = metrics;
    }

    LogMessage(LogLevel::Info, "text", "\"%s\" now needs a %dx%d glyph atlas.", face.Path.c_str(),
               face.Size, face.Size);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// FontStore
// ---------------------------------------------------------------------------

FontStore::FontStore() = default;
FontStore::~FontStore() = default;

void FontStore::Initialize(Renderer* renderer)
{
    m_Renderer = renderer;
}

void FontStore::Shutdown()
{
    if (m_Renderer != nullptr)
    {
        for (const std::unique_ptr<FaceRecord>& face : m_Faces)
        {
            if (face->Atlas.IsValid())
            {
                m_Renderer->DestroyTexture(face->Atlas);
            }
        }
    }

    m_Faces.clear();
    m_Fonts.clear();
    m_FreeFonts.clear();
    m_Renderer = nullptr;
}

FaceRecord* FontStore::AcquireFace(const std::string& path, float em, bool field)
{
    for (const std::unique_ptr<FaceRecord>& existing : m_Faces)
    {
        if (existing->Path == path && existing->Em == em && existing->Field == field)
        {
            ++existing->Users;
            return existing.get();
        }
    }

    auto face = std::make_unique<FaceRecord>();
    face->Path = path;
    face->Em = em;
    face->Field = field;

    if (!ReadWholeFile(path, face->Bytes))
    {
        LogMessage(LogLevel::Error, "text", "Could not read the font file \"%s\".", path.c_str());
        return nullptr;
    }

    const int offset = stbtt_GetFontOffsetForIndex(face->Bytes.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&face->Info, face->Bytes.data(), offset))
    {
        LogMessage(LogLevel::Error, "text", "\"%s\" is not a font file this build can read.",
                   path.c_str());
        return nullptr;
    }

    face->EmScale = stbtt_ScaleForPixelHeight(&face->Info, em);

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&face->Info, &ascent, &descent, &lineGap);
    face->Ascent = static_cast<float>(ascent) * face->EmScale;
    face->Descent = static_cast<float>(descent) * face->EmScale;
    face->LineHeight = static_cast<float>(ascent - descent + lineGap) * face->EmScale;

    ResetAtlas(*face, FirstAtlasSize);

    // Basic Latin is rasterized up front, since it is most of what programs
    // show. Doing it here lets the atlas reach the size it needs before
    // anything draws, instead of growing part-way through a frame.
    for (uint32_t codepoint = 32; codepoint <= 126; ++codepoint)
    {
        GlyphMetrics metrics;
        if (!Rasterize(*face, codepoint, metrics))
        {
            if (!Grow(*face) || !Rasterize(*face, codepoint, metrics))
            {
                break;
            }
        }
        face->Glyphs.emplace(codepoint, metrics);
    }

    // The atlas exists from the start, so every size in this band names the
    // same texture for as long as the band lives.
    face->Atlas = m_Renderer->CreateTexture(face->Size, face->Size, PixelFormat::Alpha8,
                                            face->Pixels.data());
    if (!face->Atlas.IsValid())
    {
        return nullptr;
    }

    face->Ready = true;
    face->Users = 1;

    m_Faces.push_back(std::move(face));
    return m_Faces.back().get();
}

FontId FontStore::LoadFromFile(const std::string& path, float pixelHeight)
{
    FontId id;

    if (m_Renderer == nullptr)
    {
        return id;
    }

    if (!(pixelHeight > 0.0f))
    {
        LogMessage(LogLevel::Error, "text", "LoadFont needs a positive pixel height.");
        return id;
    }

    // The face is chosen for the size the text will be on screen (the size
    // asked for times the interface scale), so text at 150% is rasterized at
    // 150% instead of magnified. The size is rounded: a coverage face is
    // rasterized at the height it is drawn at, and two sizes a fraction of a
    // pixel apart look the same.
    const float physical = pixelHeight * m_UiScale;
    const bool field = WantsField(physical);
    const float em = field ? FieldHeight : std::round(physical);

    FaceRecord* face = AcquireFace(path, em, field);
    if (face == nullptr)
    {
        return id;
    }

    uint32_t index;
    if (!m_FreeFonts.empty())
    {
        index = m_FreeFonts.back();
        m_FreeFonts.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(m_Fonts.size());
        m_Fonts.emplace_back();
    }

    FontRecord& record = m_Fonts[index];
    ++record.Generation;
    if (record.Generation == 0)
    {
        record.Generation = 1;
    }

    const float scale = pixelHeight / face->Em;

    record.Alive = true;
    record.Face = face;
    record.PixelHeight = pixelHeight;
    record.Scale = scale;
    record.Ascent = face->Ascent * scale;
    record.Descent = face->Descent * scale;
    record.LineHeight = face->LineHeight * scale;
    record.Atlas = face->Atlas;
    record.Name = path;

    LogMessage(LogLevel::Info, "text", "Loaded \"%s\" at %.1f px, as %s.", path.c_str(),
               static_cast<double>(pixelHeight),
               face->Field ? "a distance field" : "coverage at its own size");

    id.Index = index;
    id.Generation = record.Generation;
    return id;
}

void FontStore::SetUiScale(float scale)
{
    if (!(scale > 0.0f) || scale == m_UiScale)
    {
        return;
    }
    m_UiScale = scale;

    // Every live font moves to a face rasterized for its new size on screen.
    // Its metrics stay in interface units, so nothing laid out with it moves;
    // only the texels behind it change.
    for (FontRecord& record : m_Fonts)
    {
        if (!record.Alive)
        {
            continue;
        }
        const float physical = record.PixelHeight * m_UiScale;
        const bool field = WantsField(physical);
        const float em = field ? FieldHeight : std::round(physical);
        FaceRecord* face = AcquireFace(record.Name, em, field);
        if (face == nullptr)
        {
            continue;
        }
        const float toUnits = record.PixelHeight / face->Em;
        record.Face = face;
        record.Scale = toUnits;
        record.Ascent = face->Ascent * toUnits;
        record.Descent = face->Descent * toUnits;
        record.LineHeight = face->LineHeight * toUnits;
        record.Atlas = face->Atlas;
    }
}

FontId FontStore::LoadDefault(float pixelHeight)
{
    for (const char* candidate : DefaultFontCandidates)
    {
        FontId id = LoadFromFile(candidate, pixelHeight);
        if (id.IsValid())
        {
            return id;
        }
    }

    LogMessage(LogLevel::Warning, "text",
               "No system font was found, so text will not draw. Load one explicitly with "
               "App::LoadFont.");
    return FontId{};
}

void FontStore::Destroy(FontId id)
{
    if (!id.IsValid() || id.Index >= m_Fonts.size())
    {
        return;
    }

    FontRecord& record = m_Fonts[id.Index];
    if (!record.Alive || record.Generation != id.Generation)
    {
        return;
    }

    // The face outlives this size while another font still uses it: the glyphs
    // are the expensive part, and they are shared.
    if (record.Face != nullptr && --record.Face->Users <= 0)
    {
        for (size_t index = 0; index < m_Faces.size(); ++index)
        {
            if (m_Faces[index].get() != record.Face)
            {
                continue;
            }
            if (m_Renderer != nullptr && m_Faces[index]->Atlas.IsValid())
            {
                m_Renderer->DestroyTexture(m_Faces[index]->Atlas);
            }
            m_Faces.erase(m_Faces.begin() + static_cast<ptrdiff_t>(index));
            break;
        }
    }

    record.Alive = false;
    record.Face = nullptr;
    record.Atlas = TextureId{};
    ++record.Generation;
    if (record.Generation == 0)
    {
        record.Generation = 1;
    }
    m_FreeFonts.push_back(id.Index);
}

const FontRecord* FontStore::Resolve(const FontId& id) const
{
    if (!id.IsValid() || id.Index >= m_Fonts.size())
    {
        return nullptr;
    }

    const FontRecord& record = m_Fonts[id.Index];
    if (!record.Alive || record.Generation != id.Generation)
    {
        return nullptr;
    }
    return &record;
}

bool FontStore::Glyph(const FontRecord& font, uint32_t codepoint, GlyphMetrics& metrics)
{
    FaceRecord* face = font.Face;
    if (face == nullptr || !face->Ready)
    {
        return false;
    }

    auto found = face->Glyphs.find(codepoint);
    if (found == face->Glyphs.end())
    {
        GlyphMetrics rasterized;
        if (!Rasterize(*face, codepoint, rasterized))
        {
            // Out of room. Repacking now would move every glyph already drawn
            // this frame out from under it, so the atlas grows at the start of
            // the next frame and this character waits one frame for its turn.
            face->NeedsGrow = true;
            return false;
        }
        found = face->Glyphs.emplace(codepoint, rasterized).first;
    }

    const GlyphMetrics& source = found->second;
    const float scale = font.Scale;

    metrics.U0 = source.U0;
    metrics.V0 = source.V0;
    metrics.U1 = source.U1;
    metrics.V1 = source.V1;
    metrics.OffsetX = source.OffsetX * scale;
    metrics.OffsetY = source.OffsetY * scale;
    metrics.Width = source.Width * scale;
    metrics.Height = source.Height * scale;
    metrics.Advance = source.Advance * scale;
    return true;
}

float FontStore::Kerning(const FontRecord& font, uint32_t first, uint32_t second)
{
    FaceRecord* face = font.Face;
    if (face == nullptr || !face->Ready || first == 0 || second == 0)
    {
        return 0.0f;
    }

    const int kern = stbtt_GetCodepointKernAdvance(&face->Info, static_cast<int>(first),
                                                   static_cast<int>(second));
    return static_cast<float>(kern) * face->EmScale * font.Scale;
}

Vec2 FontStore::Measure(const FontId& id, const std::string& text)
{
    const FontRecord* record = Resolve(id);
    if (record == nullptr)
    {
        return Vec2{};
    }

    float width = 0.0f;
    uint32_t previous = 0;
    size_t index = 0;
    while (index < text.size())
    {
        const uint32_t codepoint = DecodeUtf8(text, index);
        if (codepoint == '\n')
        {
            previous = 0;
            continue;
        }

        GlyphMetrics metrics;
        if (!Glyph(*record, codepoint, metrics))
        {
            continue;
        }
        width += Kerning(*record, previous, codepoint) + metrics.Advance;
        previous = codepoint;
    }

    return Vec2{ width, record->LineHeight };
}

bool FontStore::IsField(const FontRecord& font) const
{
    return font.Face != nullptr && font.Face->Field;
}

std::vector<size_t> FontStore::WrapLines(const FontId& id, const std::string& text,
                                         float maximumWidth)
{
    std::vector<size_t> starts{ 0 };

    const FontRecord* record = Resolve(id);
    if (record == nullptr || text.empty())
    {
        return starts;
    }

    float width = 0.0f;
    uint32_t previous = 0;

    // Where the line could break, and how wide it was up to there.
    size_t lastBreak = std::string::npos;
    float widthAtBreak = 0.0f;

    size_t index = 0;
    while (index < text.size())
    {
        const size_t begin = index;
        const uint32_t codepoint = DecodeUtf8(text, index);

        if (codepoint == '\n')
        {
            starts.push_back(index);
            width = 0.0f;
            previous = 0;
            lastBreak = std::string::npos;
            continue;
        }

        GlyphMetrics metrics;
        if (!Glyph(*record, codepoint, metrics))
        {
            continue;
        }

        const float advance = Kerning(*record, previous, codepoint) + metrics.Advance;

        if (codepoint == ' ')
        {
            lastBreak = index; // the next line starts after the space
            widthAtBreak = width;
        }

        if (width + advance > maximumWidth && width > 0.0f)
        {
            if (lastBreak != std::string::npos && lastBreak > starts.back())
            {
                // Back up to the last space and carry the rest down.
                starts.push_back(lastBreak);
                index = lastBreak;
                width = 0.0f;
                previous = 0;
                lastBreak = std::string::npos;
                (void)widthAtBreak;
                continue;
            }

            // One word wider than the line: break inside it rather than
            // running off the edge.
            starts.push_back(begin);
            index = begin;
            width = 0.0f;
            previous = 0;
            continue;
        }

        width += advance;
        previous = codepoint;
    }

    return starts;
}

float FontStore::GetLineHeight(const FontId& id) const
{
    const FontRecord* record = Resolve(id);
    return record != nullptr ? record->LineHeight : 0.0f;
}

void FontStore::BeginFrame()
{
    for (const std::unique_ptr<FaceRecord>& face : m_Faces)
    {
        if (!face->NeedsGrow)
        {
            continue;
        }
        face->NeedsGrow = false;

        if (!Grow(*face))
        {
            LogMessage(LogLevel::Warning, "text",
                       "\"%s\" has more glyphs in use than a %d px atlas holds; the rest will not "
                       "draw.",
                       face->Path.c_str(), face->Size);
        }
    }
}

void FontStore::UploadPending()
{
    if (m_Renderer == nullptr)
    {
        return;
    }

    for (const std::unique_ptr<FaceRecord>& face : m_Faces)
    {
        if (!face->Dirty || !face->Atlas.IsValid())
        {
            continue;
        }
        const std::vector<const void*> levels{ face->Pixels.data() };
        m_Renderer->UpdateTexture(face->Atlas, face->Size, face->Size, PixelFormat::Alpha8, levels);
        face->Dirty = false;
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// DrawList text
// ---------------------------------------------------------------------------

void DrawList::DrawMaskedTextureRegion(const Rect& bounds, TextureId texture, const Rect& uv,
                                       Color color)
{
    if (!texture.IsValid())
    {
        return;
    }
    PushQuad(bounds, uv, color, 0.0f, 0.0f, TextureModeMask, texture, true);
}

void DrawList::DrawText(const std::string& text, Vec2 position, FontId font, Color color,
                        TextAlign align)
{
    if (m_Fonts == nullptr || text.empty() || color.A <= 0.0f)
    {
        return;
    }

    const detail::FontRecord* record = m_Fonts->Resolve(font);
    if (record == nullptr)
    {
        return;
    }

    float penX = position.X;
    if (align != TextAlign::Left)
    {
        const float width = m_Fonts->Measure(font, text).X;
        penX -= (align == TextAlign::Center) ? width * 0.5f : width;
    }

    // position.Y names the top of the line, so the baseline sits one ascent
    // below it. Callers then place text by its box rather than by a baseline
    // they would have to look up.
    const float baselineY = position.Y + record->Ascent;

    uint32_t previous = 0;
    size_t index = 0;
    while (index < text.size())
    {
        const uint32_t codepoint = detail::DecodeUtf8(text, index);
        if (codepoint == '\n')
        {
            previous = 0;
            continue;
        }

        detail::GlyphMetrics glyph;
        if (!m_Fonts->Glyph(*record, codepoint, glyph))
        {
            continue;
        }

        penX += m_Fonts->Kerning(*record, previous, codepoint);
        previous = codepoint;

        if (glyph.Width > 0.0f && glyph.Height > 0.0f)
        {
            const Rect bounds{ penX + glyph.OffsetX, baselineY + glyph.OffsetY, glyph.Width,
                               glyph.Height };
            const Rect uv{ glyph.U0, glyph.V0, glyph.U1 - glyph.U0, glyph.V1 - glyph.V0 };

            PushQuad(bounds, uv, color, 0.0f, 0.0f, m_Fonts->IsField(*record) ? TextureModeGlyph : TextureModeMask,
                     record->Atlas, true);
        }

        penX += glyph.Advance;
    }
}

void DrawList::DrawRichText(const std::vector<TextRun>& runs, Vec2 position, FontId font,
                            Color color, TextAlign align)
{
    if (m_Fonts == nullptr || runs.empty())
    {
        return;
    }

    // Runs share a baseline rather than a box top, so a larger word in the
    // middle of a sentence grows upward and stays on the line.
    float ascent = 0.0f;
    float width = 0.0f;
    for (const TextRun& run : runs)
    {
        const FontId runFont = run.Font.IsValid() ? run.Font : font;
        const detail::FontRecord* record = m_Fonts->Resolve(runFont);
        if (record == nullptr)
        {
            continue;
        }
        ascent = std::max(ascent, record->Ascent);
        width += m_Fonts->Measure(runFont, run.Text).X;
    }

    float penX = position.X;
    if (align == TextAlign::Center)
    {
        penX -= width * 0.5f;
    }
    else if (align == TextAlign::Right)
    {
        penX -= width;
    }

    for (const TextRun& run : runs)
    {
        const FontId runFont = run.Font.IsValid() ? run.Font : font;
        const detail::FontRecord* record = m_Fonts->Resolve(runFont);
        if (record == nullptr)
        {
            continue;
        }

        const Color runColor = run.Color.A > 0.0f ? run.Color : color;
        DrawText(run.Text, Vec2{ penX, position.Y + ascent - record->Ascent }, runFont, runColor);
        penX += m_Fonts->Measure(runFont, run.Text).X;
    }
}

float DrawList::DrawTextWrapped(const std::string& text, const Rect& bounds, FontId font,
                                Color color, TextAlign align)
{
    if (m_Fonts == nullptr || text.empty())
    {
        return 0.0f;
    }

    const float lineHeight = m_Fonts->GetLineHeight(font);
    const std::vector<size_t> starts = m_Fonts->WrapLines(font, text, bounds.Width);

    float x = bounds.X;
    if (align == TextAlign::Center)
    {
        x = bounds.X + bounds.Width * 0.5f;
    }
    else if (align == TextAlign::Right)
    {
        x = bounds.X + bounds.Width;
    }

    for (size_t line = 0; line < starts.size(); ++line)
    {
        const size_t begin = starts[line];
        const size_t end = (line + 1 < starts.size()) ? starts[line + 1] : text.size();

        std::string content = text.substr(begin, end - begin);
        while (!content.empty() && (content.back() == '\n' || content.back() == ' '))
        {
            content.pop_back();
        }

        DrawText(content, Vec2{ x, bounds.Y + lineHeight * static_cast<float>(line) }, font, color,
                 align);
    }

    return lineHeight * static_cast<float>(starts.size());
}

void DrawList::DrawTextInRect(const std::string& text, const Rect& bounds, FontId font, Color color,
                              TextAlign align)
{
    if (m_Fonts == nullptr)
    {
        return;
    }

    const float lineHeight = m_Fonts->GetLineHeight(font);
    const float y = bounds.Y + (bounds.Height - lineHeight) * 0.5f;

    float x = bounds.X;
    if (align == TextAlign::Center)
    {
        x = bounds.X + bounds.Width * 0.5f;
    }
    else if (align == TextAlign::Right)
    {
        x = bounds.X + bounds.Width;
    }

    DrawText(text, Vec2{ x, y }, font, color, align);
}

} // namespace opane
