/// @file logger.h
/// @brief Centralized logging with severity levels.
#pragma once

#include <string>

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

private:
    static void log(LogLevel level, const std::string& message);
    static const char* levelToString(LogLevel level);

    static LogLevel s_level;
};

} // namespace Vestige
