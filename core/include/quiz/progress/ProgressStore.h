#pragma once

#include <string>

#include "quiz/model/Progress.h"

// ============================================================================
// 进度持久化（纯文本，格式与题库文件同源：可读、可手改、可被 git diff）
// ----------------------------------------------------------------------------
// 这里只负责「内存对象 ↔ 文本」的转换；真正的文件读写交给平台层
// （Windows 写 exe 旁的数据目录，Android 写应用内部存储），这样核心代码保持平台无关。
// ============================================================================
namespace quiz::progress {

/// 序列化成文本
std::string serialize(const BankProgress& progress);

/// 从文本还原；questionCount 用于把记录数对齐到当前题库题目数
BankProgress deserialize(const std::string& text, const std::string& bankId, int questionCount);

}  // namespace quiz::progress
