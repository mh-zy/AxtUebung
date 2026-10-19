#pragma once

// 简单日志（最终由平台层决定输出到哪里：Windows 控制台 / Android logcat）
namespace quiz {

void logInfo(const char* format, ...);
void logWarn(const char* format, ...);
void logError(const char* format, ...);

}  // namespace quiz
