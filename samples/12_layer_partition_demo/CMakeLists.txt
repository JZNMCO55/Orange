# 12_layer_partition_demo —— LayerComponent + WorldPartition 演示
# （GAP-2026-05-17-scene-layer-component 验收 sample）。
#
# 场景：
#   * background layer：plane + sphere + DirectionalLight + Camera
#   * foreground layer：两个 dynamic box（受重力下落到 plane 上反弹）
#
# 自动行为：每 ~3 秒切换一次 foreground layer 的可见性 → viewport 内
# 前景两个 box 同步消失 / 出现；不可见时它们的 Box2D body 被 Disable，
# 物理不再 tick（落体冻结在 Disable 当帧的位置）。
#
# 操作：无（自动循环，便于截屏验收）。

add_executable(12_layer_partition_demo main.cpp)
target_link_libraries(12_layer_partition_demo PRIVATE OrangeEngine::orange_engine)
orange_engine_set_compiler_options(12_layer_partition_demo)

set_target_properties(12_layer_partition_demo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:12_layer_partition_demo>"
)
