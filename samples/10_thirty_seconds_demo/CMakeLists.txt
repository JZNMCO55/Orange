# 10_thirty_seconds_demo sample —— 把已交付的全部子系统组装成一段
# ~30 秒的可玩 demo（启动 → JSON scene 异步加载 → 玩家走完一小段平
# 台 → 触发胜利）。引擎本身零改动；本 sample 是纯组合验证。
#
# 关卡内容（assets/scenes/thirty_seconds_demo.scene.json）：
#   * player：dynamic RigidBody + Box collider，左下出生
#   * ground + 3 个 platform：static RigidBody + Box collider，组成上跳路径
#   * goal：static sensor Collider + emissive material，被玩家接触触发胜利
#   * DirectionalLight 主光
# camera 不进 scene（schema v1 暂未把 Camera 当 component），sample 在
# C++ 端单独 attach。

add_executable(10_thirty_seconds_demo main.cpp)

target_link_libraries(10_thirty_seconds_demo PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(10_thirty_seconds_demo)

set_target_properties(10_thirty_seconds_demo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:10_thirty_seconds_demo>"
)

# scene 文件路径在 CMake 配置时编译进 sample —— 与 skeletal_animator_test
# 的 ORANGE_ENGINE_TEST_DATA_DIR 同模式，避免相对路径在不同 CWD 下解析
# 不一致。
target_compile_definitions(10_thirty_seconds_demo PRIVATE
    "ORANGE_SAMPLE_10_SCENE_PATH=\"${CMAKE_SOURCE_DIR}/assets/scenes/thirty_seconds_demo.scene.json\""
    "ORANGE_SAMPLE_10_ACTIONS_PATH=\"${CMAKE_SOURCE_DIR}/assets/configs/default.actions.json\""
)
