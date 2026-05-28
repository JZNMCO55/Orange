# 17_imgui_overlay sample —— GAP-2026-05-27-consumer-imgui-tuning-hook 端到端
# 验收：仅经公共 API 的消费者叠 ImGui 调参面板，slider 实时改场景不重编。
# <imgui.h> 经 OrangeEngine::orange_engine 的 PUBLIC imgui 链接传递性可见。
add_executable(17_imgui_overlay main.cpp)

target_link_libraries(17_imgui_overlay PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(17_imgui_overlay)
