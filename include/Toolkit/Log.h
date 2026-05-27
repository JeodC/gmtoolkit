// SPDX-License-Identifier: MIT

#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace Gmtoolkit {

enum class LogLevel : int {
    Quiet = 0,
    Warn = 1,
    Message = 2,
    Debug = 3,
};

inline LogLevel& log_level() {
    static LogLevel lvl = LogLevel::Message;
    return lvl;
}

inline FILE*& log_sink() {
    static FILE* f = nullptr;
    return f;
}

// Format once to stderr and, if a sink is attached, again to disk. The va_copy is mandatory since
// vfprintf consumes its va_list and cannot be reused.
inline void log_print(const char* tag, LogLevel min_lvl, const char* fmt, va_list ap) {
    if ((int)log_level() < (int)min_lvl)
        return;
    va_list ap_copy;
    va_copy(ap_copy, ap);
    std::fprintf(stderr, "[%s] ", tag);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    if (FILE* sink = log_sink()) {
        std::fprintf(sink, "[%s] ", tag);
        std::vfprintf(sink, fmt, ap_copy);
        std::fputc('\n', sink);
        std::fflush(sink);
    }
    va_end(ap_copy);
}

inline void msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_print("MESSAGE", LogLevel::Message, fmt, ap);
    va_end(ap);
}

inline void warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_print("WARN", LogLevel::Warn, fmt, ap);
    va_end(ap);
}

inline void err(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_print("ERROR", LogLevel::Quiet, fmt, ap);
    va_end(ap);
}

inline void dbg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_print("DEBUG", LogLevel::Debug, fmt, ap);
    va_end(ap);
}

// Tag-less printf passthrough; for output we want verbatim in the log without the [LEVEL] prefix.
inline void tprint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    std::vfprintf(stderr, fmt, ap);
    if (FILE* sink = log_sink()) {
        std::vfprintf(sink, fmt, ap_copy);
        std::fflush(sink);
    }
    va_end(ap_copy);
    va_end(ap);
}

void start_output_tee(const std::string& log_path);
void stop_output_tee();
void set_drag_drop_mode();
void pause_if_drag_drop();

} // namespace Gmtoolkit
