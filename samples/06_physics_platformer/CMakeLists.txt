# 06_physics_platformer sample —— Phase 4 / Task 10。
# 一个 dynamic ball 受重力下落、撞到 static plane 反弹/滑动；DirectionalLight
# 投影 + 完整 PostProcessChain 沿用 sample 07 视觉基线。
add_executable(06_physics_platformer main.cpp)

target_link_libraries(06_physics_platformer PRIVATE OrangeEngine::orange_engine)
target_include_directories(06_physics_platformer PRIVATE
    "${CMAKE_SOURCE_DIR}/samples/common"
)

orange_engine_set_compiler_options(06_physics_platformer)
