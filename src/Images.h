// Image decoding and mip-chain construction. Not part of the public API.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace opane::detail
{

// Four bytes per pixel, straight (not premultiplied) alpha.
struct DecodedImage
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Pixels;
};

// PNG, JPEG, BMP, TGA, GIF (first frame), PSD, HDR (tone-clamped), PIC, PNM.
bool DecodeImage(const uint8_t* bytes, size_t size, DecodedImage& outImage, std::string& outError);

// The magenta-and-black checkerboard a missing texture is replaced with. It is
// loud on purpose: a missing texture should be noticed, not mistaken for art.
DecodedImage MakeCheckerboard(int size = 64, int cellSize = 8);

// Every level from full size down to 1x1. Colour is averaged in linear light,
// weighted by alpha, and first dilated into fully transparent texels, so a
// straight-alpha image neither darkens as it shrinks nor grows a dark fringe
// where bilinear filtering meets its transparent border.
std::vector<std::vector<uint8_t>> BuildMipChain(const DecodedImage& image);

} // namespace opane::detail
