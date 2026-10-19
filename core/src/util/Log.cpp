#include "quiz/util/Log.h"

#include <cstdarg>
#include <cstdio>

#include "quiz/platform/Platform.h"

namespace quiz {

namespace {

void logFormatted(int level, const char* format, va_list args) {
    char buffer[512];
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    platform::writeLog(level, buffer);
}

}  // namespace

void logInfo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logFormatted(0, format, args);
    va_end(args);
}

void logWarn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logFormatted(1, format, args);
    va_end(args);
}

void logError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logFormatted(2, format, args);
    va_end(args);
}

}  // namespace quiz
