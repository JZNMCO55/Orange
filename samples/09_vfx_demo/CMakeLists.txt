# 09_vfx_demo sample —— VFX 子系统的视觉里程碑。
#
# 一个发射器在屏幕中央喷出 fountain 风格的粒子流：
#   * 粒子带 alpha > 1 的 HDR 颜色，借既有 BuiltinPostProcessChain 的
#     bloom pass 自动获得发光晕；
#   * Pipeline 在主 pass 之后 / bloom 之前调一次 VfxSystem::DrawParticles
#     （由 Pipeline::SetVfxSystem 自动 wire 起来）。
#
# 后续在同 sample 里追加 dissolve / emissive mesh，做完整 VFX 视觉收尾。

add_executable(09_vfx_demo main.cpp)

target_link_libraries(09_vfx_demo PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(09_vfx_demo)
