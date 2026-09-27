// Theme files: a theme written as JSON, read over a theme in memory.
//
//   {
//     "colors": { "background": "#0b0d12", "surface": "#161a22", "accent": "#5b8cff", ... },
//     "cornerRadius": 10, "padding": 12, "spacing": 8, "borderWidth": 1,
//     "font": { "path": "fonts/Inter.ttf", "size": 16 },
//     "widgets": { "button": { ...a style... }, "sliderThumb": { ... }, ... },
//     "styles": { "card": { ...a style... } }
//   }
//
// A style is either a box (the look when nothing is happening) or an object with
// "normal", and "hovered", "pressed", "focused", "selected", and "disabled",
// each of which lists only what differs from normal, plus "transition" and
// "curve". A box has "background" (a colour, or an object with "linear",
// "radial", or "image"), "border", "radius", "shadows" or "glow",
// "backdropBlur", "opacity", "textColor", "scale", and "offset". The
// documentation's page on theme files describes them all.

#include "ThemeFile.h"

#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace opane::detail
{
namespace
{

class ThemeReader
{
public:
    ThemeReader(App app, std::string directory) : m_App(app), m_Directory(std::move(directory)) {}

    void Read(const JsonValue& root, Theme& theme)
    {
        if (!root.IsObject())
        {
            Warn(root, "a theme file is an object: { \"colors\": ..., \"widgets\": ... }");
            return;
        }

        for (const auto& [key, value] : root.Object)
        {
            if (key == "colors")
            {
                ReadColors(value, theme);
            }
            else if (key == "cornerRadius")
            {
                ReadNumber(value, theme.CornerRadius);
            }
            else if (key == "padding")
            {
                ReadNumber(value, theme.Padding);
            }
            else if (key == "spacing")
            {
                ReadNumber(value, theme.Spacing);
            }
            else if (key == "borderWidth")
            {
                ReadNumber(value, theme.BorderWidth);
            }
            else if (key == "font")
            {
                ReadFont(value, theme);
            }
            else if (key == "widgets")
            {
                ReadWidgets(value, theme);
            }
            else if (key == "styles")
            {
                ReadNamedStyles(value, theme);
            }
            else if (key != "$schema" && key != "name" && key != "comment")
            {
                Unknown(value, key, "the theme");
            }
        }
    }

    const std::vector<std::string>& GetWarnings() const { return m_Warnings; }

private:
    void Warn(const JsonValue& at, const std::string& what)
    {
        char where[64];
        std::snprintf(where, sizeof(where), "line %d, column %d: ", at.Line, at.Column);
        m_Warnings.push_back(where + what);
    }

    void Unknown(const JsonValue& at, const std::string& key, const char* in)
    {
        Warn(at, "\"" + key + "\" is not something " + in + " has, and is ignored");
    }

    bool ReadNumber(const JsonValue& value, float& out)
    {
        if (!value.IsNumber())
        {
            Warn(value, "expected a number");
            return false;
        }
        out = static_cast<float>(value.Number);
        return true;
    }

    bool ReadVec2(const JsonValue& value, Vec2& out)
    {
        if (value.IsNumber())
        {
            out = Vec2{ static_cast<float>(value.Number), static_cast<float>(value.Number) };
            return true;
        }
        if (value.IsArray() && value.Array.size() == 2 && value.Array[0].IsNumber() && value.Array[1].IsNumber())
        {
            out = Vec2{ static_cast<float>(value.Array[0].Number), static_cast<float>(value.Array[1].Number) };
            return true;
        }
        Warn(value, "expected two numbers, [x, y]");
        return false;
    }

    static int HexDigit(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    // "#rgb", "#rgba", "#rrggbb", "#rrggbbaa", a few names, or [r, g, b, a]
    // from 0 to 1.
    bool ReadColor(const JsonValue& value, Color& out)
    {
        if (value.IsArray() && (value.Array.size() == 3 || value.Array.size() == 4))
        {
            float channels[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            for (size_t index = 0; index < value.Array.size(); ++index)
            {
                if (!value.Array[index].IsNumber())
                {
                    Warn(value, "a colour's channels are numbers from 0 to 1");
                    return false;
                }
                channels[index] = static_cast<float>(value.Array[index].Number);
            }
            out = Color{ channels[0], channels[1], channels[2], channels[3] };
            return true;
        }

        if (value.IsString())
        {
            const std::string& text = value.String;
            if (text == "transparent")
            {
                out = Color{ 0.0f, 0.0f, 0.0f, 0.0f };
                return true;
            }
            if (text == "white")
            {
                out = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                return true;
            }
            if (text == "black")
            {
                out = Color{ 0.0f, 0.0f, 0.0f, 1.0f };
                return true;
            }
            if (!text.empty() && text[0] == '#')
            {
                std::vector<int> digits;
                for (size_t index = 1; index < text.size(); ++index)
                {
                    digits.push_back(HexDigit(text[index]));
                    if (digits.back() < 0)
                    {
                        break;
                    }
                }
                const bool valid = !digits.empty() && digits.back() >= 0;
                if (valid && (digits.size() == 3 || digits.size() == 4))
                {
                    const float alpha = digits.size() == 4 ? digits[3] / 15.0f : 1.0f;
                    out = Color{ digits[0] / 15.0f, digits[1] / 15.0f, digits[2] / 15.0f, alpha };
                    return true;
                }
                if (valid && (digits.size() == 6 || digits.size() == 8))
                {
                    auto Byte = [&](size_t at) { return (digits[at] * 16 + digits[at + 1]) / 255.0f; };
                    out = Color{ Byte(0), Byte(2), Byte(4), digits.size() == 8 ? Byte(6) : 1.0f };
                    return true;
                }
            }
        }

        Warn(value, "expected a colour: \"#rrggbb\", \"#rrggbbaa\", or [r, g, b, a] from 0 to 1");
        return false;
    }

    void ReadColors(const JsonValue& value, Theme& theme)
    {
        if (!value.IsObject())
        {
            Warn(value, "\"colors\" is an object of names and colours");
            return;
        }
        const std::pair<const char*, Color*> names[] = {
            { "background", &theme.Background }, { "surface", &theme.Surface },
            { "surfaceHovered", &theme.SurfaceHovered }, { "surfacePressed", &theme.SurfacePressed },
            { "accent", &theme.Accent }, { "accentHovered", &theme.AccentHovered }, { "text", &theme.Text },
            { "textMuted", &theme.TextMuted }, { "border", &theme.Border },
        };
        for (const auto& [key, color] : value.Object)
        {
            bool known = false;
            for (const auto& name : names)
            {
                if (key == name.first)
                {
                    ReadColor(color, *name.second);
                    known = true;
                }
            }
            if (!known)
            {
                Unknown(color, key, "\"colors\"");
            }
        }
    }

    void ReadFont(const JsonValue& value, Theme& theme)
    {
        const JsonValue* path = value.Find("path");
        const JsonValue* size = value.Find("size");
        if (path == nullptr || !path->IsString())
        {
            Warn(value, "\"font\" needs a \"path\"");
            return;
        }
        float pixels = 16.0f;
        if (size != nullptr)
        {
            ReadNumber(*size, pixels);
        }
        const FontId font = m_App.LoadFont(Locate(path->String), pixels);
        if (font.IsValid())
        {
            theme.Font = font;
        }
    }

    // Beside the theme file first, then wherever assets are found.
    std::string Locate(const std::string& path) const
    {
        std::error_code error;
        const std::filesystem::path beside = std::filesystem::path(m_Directory) / path;
        if (!m_Directory.empty() && std::filesystem::exists(beside, error))
        {
            return beside.generic_string();
        }
        return path;
    }

    void ReadStops(const JsonValue& value, std::vector<GradientStop>& out)
    {
        if (!value.IsArray() || value.Array.empty())
        {
            Warn(value, "a gradient is a list of colours, or of { \"at\": 0.5, \"color\": ... }");
            return;
        }
        const size_t count = value.Array.size();
        for (size_t index = 0; index < count; ++index)
        {
            const JsonValue& entry = value.Array[index];
            GradientStop stop;
            stop.Position = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
            if (entry.IsObject())
            {
                if (const JsonValue* at = entry.Find("at"))
                {
                    ReadNumber(*at, stop.Position);
                }
                if (const JsonValue* color = entry.Find("color"))
                {
                    ReadColor(*color, stop.Color);
                }
            }
            else
            {
                ReadColor(entry, stop.Color);
            }
            out.push_back(stop);
        }
    }

    void ReadFill(const JsonValue& value, Fill& fill)
    {
        if (!value.IsObject())
        {
            Color color;
            if (ReadColor(value, color))
            {
                fill = Fill::Solid(color);
            }
            return;
        }

        if (const JsonValue* linear = value.Find("linear"))
        {
            fill = Fill{};
            fill.Kind = FillKind::LinearGradient;
            ReadStops(*linear, fill.Stops);
        }
        else if (const JsonValue* radial = value.Find("radial"))
        {
            fill = Fill{};
            fill.Kind = FillKind::RadialGradient;
            ReadStops(*radial, fill.Stops);
        }
        else if (const JsonValue* image = value.Find("image"))
        {
            fill = Fill{};
            fill.Kind = FillKind::Image;
            if (image->IsString())
            {
                fill.Image = m_App.LoadTexture(Locate(image->String));
            }
            else
            {
                Warn(*image, "\"image\" is a path");
            }
        }
        else if (const JsonValue* color = value.Find("color"))
        {
            Color solid;
            if (ReadColor(*color, solid))
            {
                fill = Fill::Solid(solid);
            }
            return;
        }
        else
        {
            Warn(value, "a background is a colour, or an object with \"linear\", \"radial\", or \"image\"");
            return;
        }

        for (const auto& [key, entry] : value.Object)
        {
            if (key == "linear" || key == "radial" || key == "image")
            {
                continue;
            }
            if (key == "angle")
            {
                ReadNumber(entry, fill.Angle);
            }
            else if (key == "center")
            {
                ReadVec2(entry, fill.Center);
            }
            else if (key == "radius")
            {
                ReadNumber(entry, fill.Radius);
            }
            else if (key == "tint")
            {
                ReadColor(entry, fill.Color);
            }
            else if (key == "tileScale")
            {
                ReadNumber(entry, fill.TileScale);
            }
            else if (key == "fit")
            {
                const std::pair<const char*, ImageFit> fits[] = {
                    { "stretch", ImageFit::Stretch }, { "contain", ImageFit::Contain }, { "cover", ImageFit::Cover },
                    { "center", ImageFit::Center },   { "tile", ImageFit::Tile },       { "nineSlice", ImageFit::NineSlice },
                };
                bool known = false;
                for (const auto& fit : fits)
                {
                    if (entry.IsString() && entry.String == fit.first)
                    {
                        fill.Fit = fit.second;
                        known = true;
                    }
                }
                if (!known)
                {
                    Warn(entry, "\"fit\" is one of stretch, contain, cover, center, tile, nineSlice");
                }
            }
            else if (key == "slice")
            {
                ReadInsets(entry, fill.Slice);
                fill.Fit = ImageFit::NineSlice;
            }
            else
            {
                Unknown(entry, key, "a background");
            }
        }
    }

    void ReadInsets(const JsonValue& value, Insets& out)
    {
        if (value.IsNumber())
        {
            out = Insets{ static_cast<float>(value.Number) };
            return;
        }
        if (value.IsArray() && value.Array.size() == 4)
        {
            float sides[4] = {};
            for (size_t index = 0; index < 4; ++index)
            {
                if (!ReadNumber(value.Array[index], sides[index]))
                {
                    return;
                }
            }
            out = Insets{ sides[0], sides[1], sides[2], sides[3] };
            return;
        }
        Warn(value, "expected a number, or [left, top, right, bottom]");
    }

    void ReadRadius(const JsonValue& value, CornerRadii& out)
    {
        if (value.IsNumber())
        {
            out = CornerRadii{ static_cast<float>(value.Number) };
            return;
        }
        if (value.IsArray() && value.Array.size() == 4)
        {
            float corners[4] = {};
            for (size_t index = 0; index < 4; ++index)
            {
                if (!ReadNumber(value.Array[index], corners[index]))
                {
                    return;
                }
            }
            out = CornerRadii{ corners[0], corners[1], corners[2], corners[3] };
            return;
        }
        Warn(value, "a radius is a number, or [topLeft, topRight, bottomRight, bottomLeft]");
    }

    void ReadShadow(const JsonValue& value, Shadow& shadow)
    {
        if (!value.IsObject())
        {
            Warn(value, "a shadow is an object: { \"color\", \"offset\", \"blur\", \"spread\", \"inset\" }");
            return;
        }
        for (const auto& [key, entry] : value.Object)
        {
            if (key == "color")
            {
                ReadColor(entry, shadow.Color);
            }
            else if (key == "offset")
            {
                ReadVec2(entry, shadow.Offset);
            }
            else if (key == "blur")
            {
                ReadNumber(entry, shadow.Blur);
            }
            else if (key == "spread")
            {
                ReadNumber(entry, shadow.Spread);
            }
            else if (key == "inset")
            {
                shadow.Inset = entry.IsBool() && entry.Bool;
            }
            else
            {
                Unknown(entry, key, "a shadow");
            }
        }
    }

    void ReadBox(const JsonValue& value, BoxStyle& box)
    {
        if (!value.IsObject())
        {
            Warn(value, "a look is an object: { \"background\", \"border\", \"radius\", ... }");
            return;
        }
        for (const auto& [key, entry] : value.Object)
        {
            if (key == "background")
            {
                ReadFill(entry, box.Background);
            }
            else if (key == "border")
            {
                if (entry.IsObject())
                {
                    if (const JsonValue* color = entry.Find("color"))
                    {
                        ReadColor(*color, box.BorderColor);
                    }
                    if (const JsonValue* width = entry.Find("width"))
                    {
                        ReadNumber(*width, box.BorderWidth);
                    }
                    else if (box.BorderWidth <= 0.0f)
                    {
                        box.BorderWidth = 1.0f;
                    }
                }
                else if (ReadColor(entry, box.BorderColor) && box.BorderWidth <= 0.0f)
                {
                    box.BorderWidth = 1.0f;
                }
            }
            else if (key == "borderColor")
            {
                ReadColor(entry, box.BorderColor);
            }
            else if (key == "borderWidth")
            {
                ReadNumber(entry, box.BorderWidth);
            }
            else if (key == "radius")
            {
                ReadRadius(entry, box.Radius);
            }
            else if (key == "shadows" || key == "shadow")
            {
                box.Shadows.clear();
                if (entry.IsArray())
                {
                    for (const JsonValue& one : entry.Array)
                    {
                        Shadow shadow;
                        ReadShadow(one, shadow);
                        box.Shadows.push_back(shadow);
                    }
                }
                else
                {
                    Shadow shadow;
                    ReadShadow(entry, shadow);
                    box.Shadows.push_back(shadow);
                }
            }
            else if (key == "glow")
            {
                // A shadow of a colour, all round: { "color", "blur", "spread" }.
                Shadow glow;
                glow.Offset = Vec2{};
                ReadShadow(entry, glow);
                box.Shadows.push_back(glow);
            }
            else if (key == "backdropBlur")
            {
                ReadNumber(entry, box.BackdropBlur);
            }
            else if (key == "opacity")
            {
                ReadNumber(entry, box.Opacity);
            }
            else if (key == "textColor")
            {
                ReadColor(entry, box.TextColor);
            }
            else if (key == "scale")
            {
                ReadNumber(entry, box.Scale);
            }
            else if (key == "offset")
            {
                ReadVec2(entry, box.Offset);
            }
            else
            {
                Unknown(entry, key, "a look");
            }
        }
    }

    void ReadStyle(const JsonValue& value, Style& style)
    {
        style = Style{};
        if (!value.IsObject())
        {
            Warn(value, "a style is an object");
            return;
        }

        // A style without state names is its normal look on its own.
        const JsonValue* normal = value.Find("normal");
        const bool shorthand = normal == nullptr;
        if (shorthand)
        {
            ReadBox(value, style.Normal);
            return;
        }
        ReadBox(*normal, style.Normal);

        const std::pair<const char*, std::optional<BoxStyle>*> states[] = {
            { "hovered", &style.Hovered },   { "pressed", &style.Pressed },   { "focused", &style.Focused },
            { "selected", &style.Selected }, { "disabled", &style.Disabled },
        };
        for (const auto& [key, entry] : value.Object)
        {
            if (key == "normal")
            {
                continue;
            }
            if (key == "transition")
            {
                ReadNumber(entry, style.Transition);
                continue;
            }
            if (key == "curve")
            {
                const std::pair<const char*, Easing> curves[] = {
                    { "linear", Easing::Linear }, { "smooth", Easing::Smooth }, { "in", Easing::In }, { "out", Easing::Out },
                };
                bool known = false;
                for (const auto& curve : curves)
                {
                    if (entry.IsString() && entry.String == curve.first)
                    {
                        style.Curve = curve.second;
                        known = true;
                    }
                }
                if (!known)
                {
                    Warn(entry, "\"curve\" is one of linear, smooth, in, out");
                }
                continue;
            }

            bool known = false;
            for (const auto& state : states)
            {
                if (key == state.first)
                {
                    // Only what differs from normal needs saying.
                    BoxStyle look = style.Normal;
                    ReadBox(entry, look);
                    *state.second = look;
                    known = true;
                }
            }
            if (!known)
            {
                Unknown(entry, key, "a style");
            }
        }
    }

    void ReadWidgets(const JsonValue& value, Theme& theme)
    {
        if (!value.IsObject())
        {
            Warn(value, "\"widgets\" is an object of widget names and styles");
            return;
        }
        Theme::WidgetStyles& styles = theme.Styles;
        const std::pair<const char*, Style*> names[] = {
            { "button", &styles.Button },
            { "accentButton", &styles.AccentButton },
            { "panel", &styles.Panel },
            { "window", &styles.Window },
            { "popup", &styles.Popup },
            { "textInput", &styles.TextInput },
            { "dropdown", &styles.Dropdown },
            { "listView", &styles.ListView },
            { "tooltip", &styles.Tooltip },
            { "checkboxBox", &styles.CheckboxBox },
            { "toggleTrack", &styles.ToggleTrack },
            { "toggleKnob", &styles.ToggleKnob },
            { "sliderTrack", &styles.SliderTrack },
            { "sliderFill", &styles.SliderFill },
            { "sliderThumb", &styles.SliderThumb },
            { "progressTrack", &styles.ProgressTrack },
            { "progressFill", &styles.ProgressFill },
            { "scrollbarThumb", &styles.ScrollbarThumb },
        };
        for (const auto& [key, entry] : value.Object)
        {
            bool known = false;
            for (const auto& name : names)
            {
                if (key == name.first)
                {
                    ReadStyle(entry, *name.second);
                    known = true;
                }
            }
            if (!known)
            {
                Unknown(entry, key, "\"widgets\"");
            }
        }
    }

    void ReadNamedStyles(const JsonValue& value, Theme& theme)
    {
        if (!value.IsObject())
        {
            Warn(value, "\"styles\" is an object of names and styles");
            return;
        }
        for (const auto& [key, entry] : value.Object)
        {
            Style style;
            ReadStyle(entry, style);
            bool replaced = false;
            for (auto& named : theme.NamedStyles)
            {
                if (named.first == key)
                {
                    named.second = style;
                    replaced = true;
                }
            }
            if (!replaced)
            {
                theme.NamedStyles.emplace_back(key, style);
            }
        }
    }

    App m_App;
    std::string m_Directory;
    std::vector<std::string> m_Warnings;
};

} // namespace

bool ReadThemeFile(const std::string& text, const std::string& directory, App app, Theme& inOutTheme,
                   std::string& outError, std::vector<std::string>& outWarnings)
{
    JsonValue root;
    if (!ParseJson(text, root, outError))
    {
        return false;
    }
    ThemeReader reader(app, directory);
    Theme theme = inOutTheme;
    reader.Read(root, theme);
    outWarnings = reader.GetWarnings();
    inOutTheme = theme;
    return true;
}

} // namespace opane::detail
