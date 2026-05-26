# 16_light_family_shadows sample —— 光源族 + 三类阴影同框演示
# (GAP-2026-05-26 G1+G2+G3)：一块地面 + 3 球 occluder + Directional /
# Spot / Point 三种光源全部 castsShadow，同时呈现平行硬阴影 / 透视锥光
# 阴影 / 全向 cubemap 阴影。
add_executable(16_light_family_shadows main.cpp)

target_link_libraries(16_light_family_shadows PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(16_light_family_shadows)
