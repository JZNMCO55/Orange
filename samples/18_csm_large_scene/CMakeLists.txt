# 18_csm_large_scene sample —— GAP-2026-05-27-cascaded-shadow-maps C1 + C2 + C3
# 视觉验收 fixture：拉长 ground 100×100 + 5 个 sphere caster 沿 +Z 摆 (3, 10, 22,
# 40, 70)，相机低角度看向 +Z。--no-csm flag 切回 C1 fallback (cascadeCount=1)
# 做 before/after 对比，戏剧性展示真 CSM (cascadeCount=3) 在大场景里的收益。
add_executable(18_csm_large_scene main.cpp)

target_link_libraries(18_csm_large_scene PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(18_csm_large_scene)
