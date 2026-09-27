// Theme files. Not installed and not part of the public API; App::LoadTheme is
// the way in.

#pragma once

#include <opane/opane.h>

#include <string>
#include <vector>

namespace opane::detail
{

// Reads a theme file's text over inOutTheme. Images and fonts it names are
// loaded through the app, found beside the file's directory first. False, with
// outError saying where, when the text is not JSON; a key it does not know is
// only a warning, so one typo does not throw the rest away.
bool ReadThemeFile(const std::string& text, const std::string& directory, App app, Theme& inOutTheme,
                   std::string& outError, std::vector<std::string>& outWarnings);

} // namespace opane::detail
