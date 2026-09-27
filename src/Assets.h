// Asset resolution: turns a forgiving name like "click.wav" into a file on
// disk by searching an ordered list of roots. Not part of the public API; the
// public surface is AddAssetRoot and friends in opane.h.

#pragma once

#include <opane/opane.h>

#include <cstdint>
#include <string>
#include <vector>

namespace opane::detail
{

// Returns the first existing candidate, or an empty string. On failure every
// candidate that was tried is logged under the given category.
std::string ResolveAsset(const std::string& path, const char* category,
                         LogLevel failureLevel = LogLevel::Error);

// Resolves and reads a whole file. Returns false, having reported why, when
// the file cannot be found or read.
bool LoadAssetFile(const std::string& path, const char* category, std::vector<uint8_t>& outBytes,
                   std::string* outResolvedPath = nullptr, LogLevel failureLevel = LogLevel::Error);

} // namespace opane::detail
