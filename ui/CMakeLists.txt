# ui：三个页面（首页 / 刷题 / 结果）
add_library(quiz_ui STATIC
    src/UiApp.cpp
    src/SettingsScreen.cpp
)

target_include_directories(quiz_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quiz_ui PUBLIC quiz_core quiz_render imgui)
