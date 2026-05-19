# 15_debug_draw_minimal sample —— v0.9 DebugDrawScene 公共面端到端验证
# 一个旋转 cube + 包围 wireframe aabb + sphere + 3 条坐标轴 line。
add_executable(15_debug_draw_minimal main.cpp)

target_link_libraries(15_debug_draw_minimal PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(15_debug_draw_minimal)
