// opane_icon: turns an image into the icon files a program carries.
//
//   opane_icon <image> <output.ico | output.icns | output.png>
//
// The image can be anything stb_image reads (PNG, JPEG, BMP, TGA, GIF, PSD),
// ideally square and at least 256 pixels across (1024 for macOS). One that
// is not square is centred on a transparent square; one that is smaller than
// a size is scaled up to it, with a warning.
//
//   .ico   Windows: 16 to 256 pixels, each size resampled from the original,
//          so Explorer and the taskbar get a sharp picture at every DPI
//   .icns  macOS: 16 to 1024 pixels, the set an app bundle wants
//   .png   one 256-pixel square, which opane embeds for the window itself
//
// Every entry is stored as PNG, which Windows has read inside .ico files since
// Vista and macOS inside .icns files since 10.7.
//
// Run by opane_set_app_icon while a program builds. It is not part of the
// library and is not linked into anything.

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct Image
{
    int Size = 0; // square
    std::vector<unsigned char> Pixels; // RGBA, straight alpha
};

bool EndsWith(const std::string& text, const char* suffix)
{
    const size_t length = std::strlen(suffix);
    if (text.size() < length)
    {
        return false;
    }
    for (size_t i = 0; i < length; ++i)
    {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[text.size() - length + i])));
        if (a != suffix[i])
        {
            return false;
        }
    }
    return true;
}

// Centres the picture on a transparent square, so a wide logo keeps its
// proportions instead of being squashed.
Image LoadSquare(const char* path)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        std::fprintf(stderr, "opane_icon: could not read %s: %s\n", path, stbi_failure_reason());
        return Image{};
    }

    Image square;
    square.Size = std::max(width, height);
    square.Pixels.assign(static_cast<size_t>(square.Size) * square.Size * 4, 0);
    const int left = (square.Size - width) / 2;
    const int top = (square.Size - height) / 2;
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(&square.Pixels[(static_cast<size_t>(top + y) * square.Size + left) * 4],
                    &pixels[static_cast<size_t>(y) * width * 4], static_cast<size_t>(width) * 4);
    }
    stbi_image_free(pixels);
    return square;
}

// Resampled from the original each time, in linear light and with alpha
// weighting, so edges stay clean and small sizes do not go dark or fringed.
std::vector<unsigned char> EncodeAt(const Image& source, int size)
{
    std::vector<unsigned char> resized(static_cast<size_t>(size) * size * 4);
    if (size == source.Size)
    {
        resized = source.Pixels;
    }
    else
    {
        stbir_resize_uint8_srgb(source.Pixels.data(), source.Size, source.Size, 0, resized.data(), size, size, 0,
                                STBIR_RGBA);
    }

    std::vector<unsigned char> png;
    stbi_write_png_to_func(
        [](void* context, void* data, int length) {
            auto* out = static_cast<std::vector<unsigned char>*>(context);
            const auto* bytes = static_cast<const unsigned char*>(data);
            out->insert(out->end(), bytes, bytes + length);
        },
        &png, size, size, 4, resized.data(), size * 4);
    return png;
}

void PutLittle16(std::vector<unsigned char>& out, uint32_t value)
{
    out.push_back(static_cast<unsigned char>(value & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void PutLittle32(std::vector<unsigned char>& out, uint32_t value)
{
    PutLittle16(out, value & 0xFFFF);
    PutLittle16(out, value >> 16);
}

void PutBig32(std::vector<unsigned char>& out, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<unsigned char>((value >> shift) & 0xFF));
    }
}

// ICONDIR, one ICONDIRENTRY per size, then the images.
std::vector<unsigned char> MakeIco(const Image& source)
{
    const int sizes[] = { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 };
    std::vector<std::vector<unsigned char>> images;
    for (int size : sizes)
    {
        images.push_back(EncodeAt(source, size));
    }

    std::vector<unsigned char> out;
    PutLittle16(out, 0); // reserved
    PutLittle16(out, 1); // an icon, not a cursor
    PutLittle16(out, static_cast<uint32_t>(images.size()));

    uint32_t offset = 6 + 16 * static_cast<uint32_t>(images.size());
    for (size_t i = 0; i < images.size(); ++i)
    {
        const int size = sizes[i];
        out.push_back(static_cast<unsigned char>(size >= 256 ? 0 : size)); // 0 means 256
        out.push_back(static_cast<unsigned char>(size >= 256 ? 0 : size));
        out.push_back(0); // no palette
        out.push_back(0); // reserved
        PutLittle16(out, 1);  // colour planes
        PutLittle16(out, 32); // bits per pixel
        PutLittle32(out, static_cast<uint32_t>(images[i].size()));
        PutLittle32(out, offset);
        offset += static_cast<uint32_t>(images[i].size());
    }
    for (const std::vector<unsigned char>& image : images)
    {
        out.insert(out.end(), image.begin(), image.end());
    }
    return out;
}

// 'icns', the file's length, then a type, length, and PNG per size.
std::vector<unsigned char> MakeIcns(const Image& source)
{
    struct Entry
    {
        const char* Type;
        int Size;
    };
    const Entry entries[] = {
        { "icp4", 16 },  { "ic11", 32 },  { "icp5", 32 },  { "ic12", 64 },   { "ic07", 128 },
        { "ic13", 256 }, { "ic08", 256 }, { "ic14", 512 }, { "ic09", 512 }, { "ic10", 1024 },
    };

    std::vector<unsigned char> body;
    for (const Entry& entry : entries)
    {
        const std::vector<unsigned char> png = EncodeAt(source, entry.Size);
        body.insert(body.end(), entry.Type, entry.Type + 4);
        PutBig32(body, static_cast<uint32_t>(png.size() + 8));
        body.insert(body.end(), png.begin(), png.end());
    }

    std::vector<unsigned char> out = { 'i', 'c', 'n', 's' };
    PutBig32(out, static_cast<uint32_t>(body.size() + 8));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool WriteFile(const char* path, const std::vector<unsigned char>& bytes)
{
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr)
    {
        std::fprintf(stderr, "opane_icon: could not write %s\n", path);
        return false;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return written == bytes.size();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: opane_icon <image> <output.ico | output.icns | output.png>\n");
        return 2;
    }

    const Image source = LoadSquare(argv[1]);
    if (source.Size == 0)
    {
        return 1;
    }

    const std::string output = argv[2];
    std::vector<unsigned char> bytes;
    int largest = 0;
    if (EndsWith(output, ".ico"))
    {
        bytes = MakeIco(source);
        largest = 256;
    }
    else if (EndsWith(output, ".icns"))
    {
        bytes = MakeIcns(source);
        largest = 1024;
    }
    else if (EndsWith(output, ".png"))
    {
        bytes = EncodeAt(source, 256);
        largest = 256;
    }
    else
    {
        std::fprintf(stderr, "opane_icon: %s should end in .ico, .icns, or .png\n", argv[2]);
        return 2;
    }

    if (source.Size < largest)
    {
        std::printf("opane_icon: %s is %d pixels across and is scaled up to %d; it will look soft there. A "
                    "square image of %d pixels or more looks sharp at every size.\n",
                    argv[1], source.Size, largest, largest);
    }
    return WriteFile(argv[2], bytes) ? 0 : 1;
}
