// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file logger.cpp
/// @brief Logger implementation with ring-buffer capture for editor console panel.
#include "core/logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace Vestige
{

#ifdef VESTIGE_DEBUG
LogLevel Logger::s_level = LogLevel::Trace;
#else
LogLevel Logger::s_level = LogLevel::Info;
#endif

std::deque<LogEntry> Logger::s_entries;

static std::ofstream s_logFile;

// F9: serialises s_entries (deque), s_logFile (ofstream), and console stream
// writes across all log() callers. AsyncTextureLoader logs from a worker
// thread, so log() races with itself today without this mutex.
static std::mutex s_logMutex;

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

std::deque<LogEntry> Logger::getEntries()
{
    std::lock_guard<std::mutex> lock(s_logMutex);
    return s_entries;
}

void Logger::clearEntries()
{
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_entries.clear();
}

void Logger::openLogFile(const std::string& directory)
{
    namespace fs = std::filesystem;

    // Close any previously opened log file before opening a new one
    closeLogFile();

    try
    {
        fs::create_directories(directory);
    }
    catch (const fs::filesystem_error& e)
    {
        // Can't log to file, but still output to console
        std::cerr << "[Vestige][ERROR] Failed to create log directory '" << directory
                  << "': " << e.what() << '\n';
        return;
    }

    // Generate timestamped filename: vestige_YYYYMMDD_HHMMSS.log
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream filename;
    filename << directory << "/vestige_"
             << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".log";

    s_logFile.open(filename.str(), std::ios::out | std::ios::trunc);
    if (s_logFile.is_open())
    {
        info("Log file opened: " + filename.str());
    }
    else
    {
        std::cerr << "[Vestige][ERROR] Failed to open log file: " << filename.str() << '\n';
    }
}

void Logger::closeLogFile()
{
    if (s_logFile.is_open())
    {
        s_logFile.flush();
        s_logFile.close();
    }
}

void Logger::log(LogLevel level, const std::string& message)
{
    if (level < s_level)
    {
        return;
    }

    // Format the line
    std::string line = std::string("[Vestige][") + levelToString(level) + "] " + message;

    // F9: one lock covers the three pieces of shared state touched below —
    // the chosen console stream, s_logFile, and s_entries. AsyncTextureLoader
    // logs from a worker, so log() can race with itself; without this lock
    // concurrent deque::push_back is UB and stream writes can tear mid-line.
    std::lock_guard<std::mutex> lock(s_logMutex);

    // Console output
    std::ostream& stream = (level >= LogLevel::Error) ? std::cerr : std::cout;
    stream << line << '\n';

    // File output
    if (s_logFile.is_open())
    {
        s_logFile << line << '\n';
        // Flush on warnings/errors so they survive crashes
        if (level >= LogLevel::Warning)
        {
            s_logFile.flush();
        }
    }

    // Ring buffer for editor console panel (deque for O(1) front removal)
    if (s_entries.size() >= MAX_ENTRIES)
    {
        s_entries.pop_front();
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
