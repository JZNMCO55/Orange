# textured_quad sample —— Phase 2 milestone 的视觉验证：ECS 实体驱
# 动 Pipeline，把一个程序式构造的 quad 上屏。fragment shader 当前从
# uv 程序式合成 checker 当贴图（OrangeRender 的 RHI 还没接 sampler
# descriptor，与 OrangeRender textured_mesh sample 同思路）。
add_executable(03_textured_quad main.cpp)

target_link_libraries(03_textured_quad PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(03_textured_quad)
