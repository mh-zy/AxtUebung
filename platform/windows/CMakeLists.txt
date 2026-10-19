# Windows 桌面端
add_executable(AxtQuiz
    WinMain.cpp
    PlatformWin.cpp
)

# comdlg32：系统「打开 / 保存」文件对话框（导入导出题备用）
target_link_libraries(AxtQuiz PRIVATE quiz_ui user32 gdi32 comdlg32)

# 可执行文件统一放在 <构建目录>/bin
set_target_properties(AxtQuiz PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)

# 把 assets（题库 + 语言包）复制到程序旁边
add_custom_command(TARGET AxtQuiz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/bin/assets
    COMMENT "复制 assets/（题库与语言包）到输出目录"
)
