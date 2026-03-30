/// @file logger.cpp
/// @brief Logger implementation with ring-buffer capture for editor console panel.
#include "core/logger.h"

#include <iostream>

namespace Vestige
{

#ifdef VESTIGE_DEBUG
LogLevel Logger::s_level = LogLevel::Trace;
#else
LogLevel Logger::s_level = LogLevel::Info;
#endif

std::vector<LogEntry> Logger::s_entries;

void Logger::setLevel(LogLevel level)
{
    s_level = level;
}

LogLevel Logger::getLevel()
{
    return s_level;
}

void Logger::trace(const std::string& message)
{
    log(LogLevel::Trace, message);
}

void Logger::debug(const std::string& message)
{
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message)
{
    log(LogLevel::Info, message);
}

void Logger::warning(const std::string& message)
{
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message)
{
    log(LogLevel::Error, message);
}

void Logger::fatal(const std::string& message)
{
    log(LogLevel::Fatal, message);
}

const std::vector<LogEntry>& Logger::getEntries()
{
    return s_entries;
}

void Logger::clearEntries()
{
    s_entries.clear();
}

void Logger::log(LogLevel level, const std::string& message)
{
    if (level < s_level)
    {
        return;
    }

    // Console output
    std::ostream& stream = (level >= LogLevel::Error) ? std::cerr : std::cout;
    stream << "[Vestige][" << levelToString(level) << "] " << message << '\n';

    // Ring buffer for editor console panel
    if (s_entries.size() >= MAX_ENTRIES)
    {
        s_entries.erase(s_entries.begin());
    }
    s_entries.push_back({level, message});
}

const char* Logger::levelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
    }
    return "?????";
}

} // namespace Vestige
