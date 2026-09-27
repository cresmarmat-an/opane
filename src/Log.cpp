#include <opane/opane.h>

#include <cstdarg>
#include <cstdio>

namespace opane
{
namespace
{

LogHandler g_Handler;
LogLevel g_MinimumLevel = LogLevel::Info;

const char* LevelName(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:   return "trace";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error:   return "error";
    }
    return "?";
}

} // namespace

void SetLogHandler(LogHandler handler)
{
    g_Handler = std::move(handler);
}

void SetMinimumLogLevel(LogLevel level)
{
    g_MinimumLevel = level;
}

void LogMessage(LogLevel level, const char* category, const char* format, ...)
{
    if (static_cast<int>(level) < static_cast<int>(g_MinimumLevel))
    {
        return;
    }

    char message[1024];

    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (g_Handler)
    {
        g_Handler(level, category, message);
        return;
    }

    std::FILE* stream = (level == LogLevel::Error || level == LogLevel::Warning) ? stderr : stdout;
    std::fprintf(stream, "[opane:%s] %s: %s\n", category, LevelName(level), message);
    std::fflush(stream);
}

} // namespace opane
