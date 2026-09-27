#include "Assets.h"

#include <opane/opane.h>

#include <SDL3/SDL.h>

#include <mutex>
#include <unordered_map>

namespace opane
{
namespace
{

// The subfolders searched under both the working directory and the
// executable's directory, in this order. A file that sits beside the program
// is found whether the program was started from its own folder or elsewhere.
const char* const g_DefaultRoots[] = {
    "",
    "assets/",
    "assets/images/",
    "assets/sounds/",
    "assets/music/",
    "assets/fonts/",
    "assets/shaders/",
    "assets/models/",
};

struct AssetState
{
    std::mutex Mutex;

    // User roots, searched before the defaults, most recently added first.
    std::vector<std::string> UserRoots;

    // Resolution results, keyed by the name the caller asked for. Cleared
    // whenever the roots change, since a new root can shadow an old answer.
    std::unordered_map<std::string, std::string> Cache;
};

AssetState& State()
{
    static AssetState state;
    return state;
}

bool Exists(const std::string& path)
{
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool IsAbsolute(const std::string& path)
{
    if (path.empty())
    {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\')
    {
        return true;
    }
    return path.size() > 1 && path[1] == ':';
}

std::string WithSeparator(std::string directory)
{
    if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
    {
        directory += '/';
    }
    return directory;
}

// Compares directories the way the file system does: separators are
// interchangeable, a trailing one is irrelevant, and on Windows case is too.
std::string NormalizedDirectory(const char* directory)
{
    std::string result = directory;
    for (char& c : result)
    {
        if (c == '\\')
        {
            c = '/';
        }
#ifdef _WIN32
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
#endif
    }
    while (!result.empty() && result.back() == '/')
    {
        result.pop_back();
    }
    return result;
}

bool IsWorkingDirectory(const char* directory)
{
    char* current = SDL_GetCurrentDirectory();
    if (current == nullptr)
    {
        return false;
    }
    const bool same = NormalizedDirectory(current) == NormalizedDirectory(directory);
    SDL_free(current);
    return same;
}

std::vector<std::string> Candidates(const std::string& path, const std::vector<std::string>& userRoots)
{
    std::vector<std::string> candidates;

    if (IsAbsolute(path))
    {
        candidates.push_back(path);
        return candidates;
    }

    for (const std::string& root : userRoots)
    {
        candidates.push_back(WithSeparator(root) + path);
    }

    for (const char* root : g_DefaultRoots)
    {
        candidates.push_back(std::string(root) + path);
    }

    // The executable's own folder, for a program started from somewhere else.
    // When it was started from its own folder these would repeat the
    // candidates above, so they are left out.
    const char* base = SDL_GetBasePath();
    if (base != nullptr && !IsWorkingDirectory(base))
    {
        for (const char* root : g_DefaultRoots)
        {
            candidates.push_back(std::string(base) + root + path);
        }
    }

    return candidates;
}

} // namespace

void AddAssetRoot(const std::string& directory)
{
    if (directory.empty())
    {
        return;
    }

    AssetState& state = State();
    std::lock_guard<std::mutex> lock(state.Mutex);
    state.UserRoots.insert(state.UserRoots.begin(), directory);
    state.Cache.clear();
}

void ClearAssetRoots()
{
    AssetState& state = State();
    std::lock_guard<std::mutex> lock(state.Mutex);
    state.UserRoots.clear();
    state.Cache.clear();
}

std::vector<std::string> GetAssetRoots()
{
    AssetState& state = State();
    std::lock_guard<std::mutex> lock(state.Mutex);

    std::vector<std::string> roots = state.UserRoots;
    for (const char* root : g_DefaultRoots)
    {
        roots.push_back(*root == '\0' ? std::string("./") : std::string("./") + root);
    }
    return roots;
}

std::string ResolveAssetPath(const std::string& path)
{
    return detail::ResolveAsset(path, "asset");
}

namespace detail
{

std::string ResolveAsset(const std::string& path, const char* category, LogLevel failureLevel)
{
    if (path.empty())
    {
        LogMessage(LogLevel::Error, category, "An empty path was given where a file was expected.");
        return std::string();
    }

    AssetState& state = State();

    std::vector<std::string> userRoots;
    {
        std::lock_guard<std::mutex> lock(state.Mutex);
        const auto cached = state.Cache.find(path);
        if (cached != state.Cache.end())
        {
            return cached->second;
        }
        userRoots = state.UserRoots;
    }

    const std::vector<std::string> candidates = Candidates(path, userRoots);

    std::vector<std::string> tried;
    for (const std::string& candidate : candidates)
    {
        bool repeated = false;
        for (const std::string& previous : tried)
        {
            if (previous == candidate)
            {
                repeated = true;
                break;
            }
        }
        if (repeated)
        {
            continue;
        }

        if (Exists(candidate))
        {
            std::lock_guard<std::mutex> lock(state.Mutex);
            state.Cache[path] = candidate;
            return candidate;
        }

        tried.push_back(candidate);
    }

    std::string list;
    for (const std::string& candidate : tried)
    {
        list += "\n    ";
        list += candidate;
    }
    LogMessage(failureLevel, category, "Could not find \"%s\". Tried:%s", path.c_str(), list.c_str());

    return std::string();
}

bool LoadAssetFile(const std::string& path, const char* category, std::vector<uint8_t>& outBytes,
                   std::string* outResolvedPath, LogLevel failureLevel)
{
    const std::string resolved = ResolveAsset(path, category, failureLevel);
    if (resolved.empty())
    {
        return false;
    }

    size_t size = 0;
    void* data = SDL_LoadFile(resolved.c_str(), &size);
    if (data == nullptr)
    {
        LogMessage(LogLevel::Error, category, "Found \"%s\" but could not read it: %s", resolved.c_str(),
                   SDL_GetError());
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    outBytes.assign(bytes, bytes + size);
    SDL_free(data);

    if (outResolvedPath != nullptr)
    {
        *outResolvedPath = resolved;
    }
    return true;
}

} // namespace detail
} // namespace opane
