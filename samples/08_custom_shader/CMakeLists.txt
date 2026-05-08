# 08_custom_shader sample —— Phase 3 收尾里程碑：验证 MaterialSystem::RegisterTemplate
# 扩展点。sample 自带 fresnel.vert.glsl + fresnel.frag.glsl，自带 glslangValidator
# 调用产出 .spv，自带 ShaderTemplateDesc 注册——不修改引擎一行。
#
# 与 OrangeRender 的 minimal_triangle sample 同模式：sample 自己 find_program
# glslangValidator + add_custom_command 编 shader → exe 同目录的
# `shaders/sample_08/` 下，让 main.cpp 的相对路径解析无歧义。

add_executable(08_custom_shader main.cpp)

target_link_libraries(08_custom_shader PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(08_custom_shader)

# 让 VS 调试器 working dir = exe 所在目录，让 "shaders/sample_08/fresnel.vert.spv"
# 这种相对路径在 F5 启动时也能解析（与其它 sample 一致）。
set_target_properties(08_custom_shader PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:08_custom_shader>"
)

# ---------------------------------------------------------------------------
# 自定义 shader 的编译归 sample 自己负责（与 docs/extension-points.md
# "shader 文件只接受 SPIR-V 路径，GLSL 由游戏侧自己预编" 一致）。
# 不复用引擎的 orange_engine_compile_builtin_shader——那是为内置 shader
# 设计的，自定义 shader 必须走自己的管线。
# ---------------------------------------------------------------------------
find_program(ORANGE_SAMPLE_08_GLSLANG
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)

if (NOT ORANGE_SAMPLE_08_GLSLANG)
    message(FATAL_ERROR "08_custom_shader: glslangValidator not found under VULKAN_SDK.")
endif ()

set(_sample_08_shader_out "$<TARGET_FILE_DIR:08_custom_shader>/shaders/sample_08")

add_custom_command(TARGET 08_custom_shader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_sample_08_shader_out}"
    COMMAND "${ORANGE_SAMPLE_08_GLSLANG}" -V
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fresnel.vert.glsl"
            -o "${_sample_08_shader_out}/fresnel.vert.spv"
    COMMAND "${ORANGE_SAMPLE_08_GLSLANG}" -V
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fresnel.frag.glsl"
            -o "${_sample_08_shader_out}/fresnel.frag.spv"
    VERBATIM
    COMMENT "Compiling 08_custom_shader fresnel shaders"
)
