# 19_postprocess_volume sample —— GAP-2026-05-27-postprocess-component-local-
# volume V2 视觉证据 fixture：1 Global 中性底 + 2 Local 盒（高对比 grading +
# 冷色温 grading），相机沿 -Z→+Z 推进穿过双盒。--position 4 个预设位置出
# "盒内 / 盒外 / 过渡带" 对照图，--motion 互动看 blendDistance 平滑过渡。
add_executable(19_postprocess_volume main.cpp)

target_link_libraries(19_postprocess_volume PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(19_postprocess_volume)
