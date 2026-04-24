// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file logger.h
/// @brief Centralized logging with severity levels and in-memory ring buffer for console panel.
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Log severity levels.
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/// @brief A single log entry captured in the ring buffer.
struct LogEntry
{
    LogLevel level;
    std::string message;
};

/// @brief Centralized logging system for the engine.
class Logger
{
public:
    /// @brief Sets the minimum log level. Messages below this level are ignored.
    /// @param level The minimum severity level to display.
    static void setLevel(LogLevel level);

    /// @brief Gets the current minimum log level.
    /// @return The current log level.
    static LogLevel getLevel();

    /// @brief Logs a trace message (most verbose, debug builds only).
    /// @param message The message to log.
    static void trace(const std::string& message);

    /// @brief Logs a debug message (debug builds only).
    /// @param message The message to log.
    static void debug(const std::string& message);

    /// @brief Logs an informational message.
    /// @param message The message to log.
    static void info(const std::string& message);

    /// @brief Logs a warning message.
    /// @param message The message to log.
    static void warning(const std::string& message);

    /// @brief Logs an error message.
    /// @param message The message to log.
    static void error(const std::string& message);

    /// @brief Logs a fatal error message.
    /// @param message The message to log.
    static void fatal(const std::string& message);

    /// @brief Returns a snapshot of recent log entries (up to MAX_ENTRIES).
    /// Returned by value so callers iterate a stable copy while worker threads
    /// may still be writing — see F9 (Logger thread-safety).
    static std::deque<LogEntry> getEntries();

    /// @brief Clears all buffered log entries.
    static void clearEntries();

    /// @brief Converts a log level to a short string (e.g. "INFO ").
    static const char* levelToString(LogLevel level);

    /// @brief Opens a log file in the given directory. Creates the directory if needed.
    /// Called once at engine startup. Subsequent log messages are written to this file.
    static void openLogFile(const std::string& directory);

    /// @brief Flushes and closes the log file (called at engine shutdown).
    static void closeLogFile();

private:
    static void log(LogLevel level, const std::string& message);

    static LogLevel s_level;
    static std::deque<LogEntry> s_entries;
    static constexpr size_t MAX_ENTRIES = 1000;
};

} // namespace Vestige
