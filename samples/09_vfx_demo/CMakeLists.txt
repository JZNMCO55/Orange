# 09_vfx_demo sample —— VFX 子系统的视觉里程碑。
#
# 一个发射器在屏幕中央喷出 fountain 风格的粒子流：
#   * 粒子带 alpha > 1 的 HDR 颜色，借既有 BuiltinPostProcessChain 的
#     bloom pass 自动获得发光晕；
#   * Pipeline 在主 pass 之后 / bloom 之前调一次 VfxSystem::DrawParticles
#     （由 Pipeline::SetVfxSystem 自动 wire 起来）。
#
# 旋转 dissolve cube + 静态 emissive cube 作为内置 shader 模板的视觉验
# 证；同 sample 还演示 Pipeline::InsertPass 扩展点（自定义 IRenderPass
# 写一条屏顶 cyan band 进 HDR target）。

add_executable(09_vfx_demo main.cpp)

target_link_libraries(09_vfx_demo PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(09_vfx_demo)

set_target_properties(09_vfx_demo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:09_vfx_demo>"
)

# ---------------------------------------------------------------------------
# 自定义 shader 编译归 sample 自己负责（与 docs/extension-points.md
# "shader 文件只接受 SPIR-V 路径，GLSL 由游戏侧自己预编" 一致）。复用
# 与 samples/08_custom_shader 相同的 sample-本地 glslangValidator + add_
# custom_command 模式。
# ---------------------------------------------------------------------------
find_program(ORANGE_SAMPLE_09_GLSLANG
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)

if (NOT ORANGE_SAMPLE_09_GLSLANG)
    message(FATAL_ERROR "09_vfx_demo: glslangValidator not found under VULKAN_SDK.")
endif ()

set(_sample_09_shader_out "$<TARGET_FILE_DIR:09_vfx_demo>/shaders/sample_09")

add_custom_command(TARGET 09_vfx_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_sample_09_shader_out}"
    COMMAND "${ORANGE_SAMPLE_09_GLSLANG}" -V
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tint_overlay.frag.glsl"
            -o "${_sample_09_shader_out}/tint_overlay.frag.spv"
    VERBATIM
    COMMENT "Compiling 09_vfx_demo tint_overlay shader"
)
