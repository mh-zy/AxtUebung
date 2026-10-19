# 单元测试：直接在电脑上运行（不需要设备，也不打开窗口）
add_executable(quiz_tests core_tests.cpp)
target_link_libraries(quiz_tests PRIVATE quiz_core quiz_svg)

# 和其它产物放在一起，方便直接运行或写进脚本
set_target_properties(quiz_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)

add_test(NAME core_tests COMMAND quiz_tests)
