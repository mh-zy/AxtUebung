# SVG 光栅化器：纯 CPU，不依赖 ImGui 与平台层 —— 单列一个目标，单元测试可以直接链接它
add_library(quiz_svg STATIC
    src/SvgIcon.cpp
    src/SvgRaster.cpp
)

target_include_directories(quiz_svg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# render：ImGui 的软件渲染后端
add_library(quiz_render STATIC
    src/ImGuiRenderer.cpp
)

target_include_directories(quiz_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(quiz_render PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(quiz_render PUBLIC imgui quiz_core quiz_svg stb_image)
