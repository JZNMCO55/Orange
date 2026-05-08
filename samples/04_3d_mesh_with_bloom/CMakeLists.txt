# 04_3d_mesh_with_bloom sample —— Phase 3 / Task 06.05 的视觉收尾里程碑。
# 与 04_3d_mesh 同骨架：旋转立方体 + textured Material；额外挂
# `BuiltinPostProcessChain::CreateDefault()`（HDR + Bloom + Tonemap +
# LUT(no-op)）走完整后处理链。BloomPass.threshold 在 sample 里调到 0.5，
# 让 textured 的亮 cell 触发 bloom 累加；Tonemap 默认 exposure=1 把
# HDR + bloom 结果通过 ACES Narkowicz 映射写到 swap-chain（BGRA8Unorm）。

add_executable(04_3d_mesh_with_bloom main.cpp)

target_link_libraries(04_3d_mesh_with_bloom PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(04_3d_mesh_with_bloom)
