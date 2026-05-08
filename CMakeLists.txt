cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# OrangeEngine 语义版本号的唯一权威来源。发版时改这里；下面
# `configure_file` 生成的 `OrangeEngineVersion.h` 会把同一组数字带到
# C++ 端（整型 + 字符串宏），后续的 `OrangeEngineConfigVersion.cmake`
# 也通过 CMake 自动派生的 `${PROJECT_VERSION}` 变量回读这里的值。
# ---------------------------------------------------------------------------
project(OrangeEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 统一的输出目录
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# 编译选项
# ---------------------------------------------------------------------------
include(cmake/CompilerOptions.cmake)

# ---------------------------------------------------------------------------
# 依赖
#
# 所有第三方 `find_package` 调用都集中在 `cmake/Dependencies.cmake`；
# 该文件同时记录 PUBLIC / PRIVATE 链接策略，是引擎依赖关系的唯一权
# 威来源。
# ---------------------------------------------------------------------------
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# OrangeEngine 静态库
# ---------------------------------------------------------------------------
add_library(orange_engine STATIC
    src/EngineStub.cpp

    # Core
    src/core/CoreHeaderCheck.cpp
    src/core/Log.cpp
    src/core/Result.cpp

    # Core：Serialization + Config
    src/core/Config.cpp
    src/core/SchemaVersion.cpp
    src/core/Serialization.cpp

    # Platform：Window（GLFW backend）
    src/platform/PlatformHeaderCheck.cpp
    src/platform/glfw/Window.cpp

    # App：Layer / LayerStack / FrameContext / AppConfig / AppHost
    src/app/AppHeaderCheck.cpp
    src/app/AppHost.cpp
    src/app/Layer.cpp
    src/app/LayerStack.cpp

    # Asset：AssetHandle / IAssetLoader / AssetRegistry + 三个内置
    # loader（Mesh / Texture / Shader，custom binary / .spv 同步加载）
    src/asset/AssetHeaderCheck.cpp
    src/asset/AssetRegistry.cpp
    src/asset/MeshLoader.cpp
    src/asset/TextureLoader.cpp
    src/asset/ShaderLoader.cpp

    # Scene：Entity / Transform / Hierarchy / ISystem / World（公共
    # 接口 + entt::registry 后端）
    src/scene/SceneHeaderCheck.cpp
    src/scene/World.cpp

    # Render：Camera / RenderableComponent / Pipeline 公共接口
    # + RenderScene 收集（World → drawable list）+ Pipeline 接通
    # OrangeRender RenderGraph + Material / MaterialInstance 公共接口
    # + BuiltinMaterials（toon / rim_light）
    # + PostProcessChain（HDR / Bloom / Tonemap / LUT 默认链描述符）
    # + MaterialSystem（自定义 shader 注入扩展点）
    # + BuiltinShadowShaders（shadow_caster shader pair 工厂）
    src/render/RenderHeaderCheck.cpp
    src/render/BuiltinMaterials.cpp
    src/render/BuiltinPostProcessChain.cpp
    src/render/BuiltinShadowShaders.cpp
    src/render/MaterialInstance.cpp
    src/render/MaterialSystem.cpp
    src/render/Pipeline.cpp
    src/render/PostProcessChain.cpp
    src/render/PostProcessPasses.cpp
    src/render/RenderScene.cpp
)

# ---------------------------------------------------------------------------
# 内置 shader 编译
#
# 把 src/render/builtin_shaders/*.glsl 用 glslangValidator 编成 SPIR-V，
# 落到 ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/orange_engine/。Pipeline
# 在 Initialize 时按 "shaders/orange_engine/<name>.spv" 的相对路径加
# 载——所有 sample / 测试可执行体（与 orange_engine 共用同一个 RUNTIME
# 输出目录）从工作目录起就能找到。
#
# glslangValidator 由 LunarG Vulkan SDK 提供；找不到时直接 FATAL_ERROR——
# Render 模块强依赖它，没有就连内置 pipeline 都构不出来。
# ---------------------------------------------------------------------------
find_program(ORANGE_ENGINE_GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)
if (NOT ORANGE_ENGINE_GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "OrangeEngine: 没在 VULKAN_SDK 下找到 glslangValidator。"
        "Render 模块需要它把内置 shader 编成 SPIR-V。请安装 LunarG "
        "Vulkan SDK 并设置环境变量 VULKAN_SDK。")
endif ()

# 多配置生成器（VS / Xcode）会自动在 RUNTIME_OUTPUT_DIRECTORY 后附加
# /Debug、/Release 等子目录；shader 落点要跟着 $<CONFIG>，否则
# 运行时按 "shaders/orange_engine/*.spv" 相对 .exe 目录解析时会扑空。
# 单配置生成器（Ninja / Make）下 $<CONFIG> 会展开为空，路径不变。
set(ORANGE_ENGINE_BUILTIN_SHADER_OUTPUT_DIR
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/orange_engine")

# shader 源里可以 #include "include/shadow_pcf.glsl.inc" 之类的辅助片
# 段；glslangValidator 通过 -I 接受 include search path（这里指向
# src/render/builtin_shaders/，让 .inc 头能被解析）。.inc 文件本身不
# 编译为独立 .spv——只是源代码片段。
set(ORANGE_ENGINE_BUILTIN_SHADER_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/builtin_shaders")

function(orange_engine_compile_builtin_shader src_relative out_var)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${src_relative}")
    get_filename_component(_full_name "${src_relative}" NAME)
    string(REGEX REPLACE "\\.glsl$" "" _stem "${_full_name}")          # minimal_mesh.vert.glsl → minimal_mesh.vert
    set(_out "${ORANGE_ENGINE_BUILTIN_SHADER_OUTPUT_DIR}/${_stem}.spv")
    add_custom_command(
        OUTPUT  "${_out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${ORANGE_ENGINE_BUILTIN_SHADER_OUTPUT_DIR}"
        COMMAND "${ORANGE_ENGINE_GLSLANG_VALIDATOR}" -V
                "-I${ORANGE_ENGINE_BUILTIN_SHADER_INCLUDE_DIR}"
                "${_src}" -o "${_out}"
        DEPENDS "${_src}"
        COMMENT "OrangeEngine: 编译内置 SPIR-V → ${_stem}.spv"
        VERBATIM
    )
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/textured_mesh.vert.glsl"
    ORANGE_ENGINE_BUILTIN_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/textured_mesh.frag.glsl"
    ORANGE_ENGINE_BUILTIN_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/toon.vert.glsl"
    ORANGE_ENGINE_BUILTIN_TOON_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/toon.frag.glsl"
    ORANGE_ENGINE_BUILTIN_TOON_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/rim_light.vert.glsl"
    ORANGE_ENGINE_BUILTIN_RIM_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/rim_light.frag.glsl"
    ORANGE_ENGINE_BUILTIN_RIM_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/shadow_caster.vert.glsl"
    ORANGE_ENGINE_BUILTIN_SHADOW_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/shadow_caster.frag.glsl"
    ORANGE_ENGINE_BUILTIN_SHADOW_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/fullscreen.vert.glsl"
    ORANGE_ENGINE_BUILTIN_FULLSCREEN_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/passthrough.frag.glsl"
    ORANGE_ENGINE_BUILTIN_PASSTHROUGH_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/bloom_downsample.frag.glsl"
    ORANGE_ENGINE_BUILTIN_BLOOM_DOWN_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/bloom_upsample.frag.glsl"
    ORANGE_ENGINE_BUILTIN_BLOOM_UP_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/passthrough_combine.frag.glsl"
    ORANGE_ENGINE_BUILTIN_PASSTHROUGH_COMBINE_FS_SPV)

add_custom_target(orange_engine_builtin_shaders ALL
    DEPENDS
        "${ORANGE_ENGINE_BUILTIN_VS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_FS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_TOON_VS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_TOON_FS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_RIM_VS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_RIM_FS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_SHADOW_VS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_SHADOW_FS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_FULLSCREEN_VS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_PASSTHROUGH_FS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_BLOOM_DOWN_FS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_BLOOM_UP_FS_SPV}"
        "${ORANGE_ENGINE_BUILTIN_PASSTHROUGH_COMBINE_FS_SPV}")

# 让 orange_engine 在 link 阶段保证 shaders 已经就位。
add_dependencies(orange_engine orange_engine_builtin_shaders)

# ---------------------------------------------------------------------------
# 生成 `orange/engine/OrangeEngineVersion.h`。
#
# 输出落在 build tree 的 `${CMAKE_BINARY_DIR}/generated/orange/engine/`，
# 让源码树保持干净（仓库里只保留 .in 模板）。生成目录以 PUBLIC 形式
# 加进 include 路径，使 orange_engine 的下游（samples、tests、安装
# 后通过 find_package 接入的工程）看到的版本头与其他公共头一样位于
# `<orange/engine/...>` 下。`@ONLY` 用来阻止 CMake 把模板里无关的
# `${...}` 当作变量替换。
# ---------------------------------------------------------------------------
set(ORANGE_ENGINE_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/orange/engine/OrangeEngineVersion.h.in"
    "${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}/orange/engine/OrangeEngineVersion.h"
    @ONLY
)

# ---------------------------------------------------------------------------
# `ORANGE_ENGINE_API` 可见性宏
#
# `generate_export_header` 写出的头会把 `ORANGE_ENGINE_API` 定义为：
#   - 当 `ORANGE_ENGINE_STATIC_DEFINE` 被定义时为空（静态库 build）；
#   - 当 CMake 自动注入的 `orange_engine_EXPORTS` 起作用时为
#     `__declspec(dllexport)`（即 SHARED 编译 orange_engine 自身）；
#   - 其它情况下为 `__declspec(dllimport)`（消费 SHARED build 的下游）。
#
# 当前 `BUILD_SHARED_LIBS` 默认 OFF、上面 `add_library` 直接写死
# STATIC，所以宏处处展开为空——对 ABI 没影响、对消费者也无感。这样
# 做的目的是先把宏槽位铺到所有公共类上，将来切到 SHARED build 只需
# 改 CMake 一行，不用再翻所有头文件。
#
# `STATIC_DEFINE` 通过 PUBLIC 与 `INSTALL_INTERFACE` 同时传播：让安
# 装后消费 static SDK 的下游也看到同样为空的宏；否则下游会默认走
# `dllimport` 然后链接出错。
# ---------------------------------------------------------------------------
include(GenerateExportHeader)
generate_export_header(orange_engine
    BASE_NAME            ORANGE_ENGINE
    EXPORT_MACRO_NAME    ORANGE_ENGINE_API
    NO_EXPORT_MACRO_NAME ORANGE_ENGINE_NO_EXPORT
    EXPORT_FILE_NAME     "${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}/orange/engine/OrangeEngineExport.h"
)
target_compile_definitions(orange_engine PUBLIC
    "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:ORANGE_ENGINE_STATIC_DEFINE>"
)

# ---------------------------------------------------------------------------
# Include 路径
#
# `include/` 是公共表面；`src/` 是私有实现。
# `${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}` 以 PUBLIC 暴露，让下游也
# 能在 `<orange/engine/...>` 下看到生成出来的版本头与 export 头。
# ---------------------------------------------------------------------------
target_include_directories(orange_engine
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ---------------------------------------------------------------------------
# 链接策略
#
# PUBLIC vs PRIVATE 详细策略写在 cmake/Dependencies.cmake。简述：
#   * OrangeRender / EnTT / glm 出现在公共 API 表面，PUBLIC 链接，
#     消费者继承；
#   * nlohmann_json 仅在 Core::Serialization 内部使用，必须不漏到公
#     共头——PRIVATE 链接；
#   * spdlog（可选）由 Core::Log 包裹；PUBLIC 传播让消费者愿意的话
#     可以复用同一个 logger 实例，与 OrangeRender 的策略对齐；
#   * Box2D / DragonBones / miniaudio / ImGui / stb 一律 PRIVATE——
#     越界禁令见 docs/extension-points.md。
# ---------------------------------------------------------------------------
target_link_libraries(orange_engine
    PUBLIC
        OrangeRender::orange_render
        ${ORANGE_ENGINE_GLM_TARGET}
        EnTT::EnTT
)

# nlohmann_json 在 Dependencies.cmake 里以 REQUIRED 解析，target 必然
# 存在。链接为 PRIVATE，避免类型从公共头泄漏出去。
target_link_libraries(orange_engine PRIVATE nlohmann_json::nlohmann_json)

# glfw3 包裹 Win32 的 window / input 抽取。PRIVATE，让 GLFWwindow*
# 不要逃出 OrangeEngine 的公共头；`Platform::Window` 对外只暴露
# opaque void* native handle。
target_link_libraries(orange_engine PRIVATE glfw)

if (ORANGE_ENGINE_WITH_SPDLOG)
    target_link_libraries(orange_engine PUBLIC spdlog::spdlog)
    target_compile_definitions(orange_engine PUBLIC ORANGE_ENGINE_WITH_SPDLOG=1)
endif ()

if (ORANGE_ENGINE_WITH_TRACY)
    target_link_libraries(orange_engine PRIVATE Tracy::TracyClient)
    target_compile_definitions(orange_engine PRIVATE ORANGE_ENGINE_WITH_TRACY=1)
endif ()

orange_engine_set_compiler_options(orange_engine)

# 头文件中含中文注释；MSVC 默认按系统代码页（中文 Windows 上常见
# code page 936 / GBK）读取源文件，会在 include 我们公共头时把多字
# 节序列误判为常量内换行（C2001 / C1075）。`/utf-8` 必须以 PUBLIC
# 形式传播给下游，让 `find_package(OrangeEngine)` 的消费者也强制按
# UTF-8 解码。BUILD_/INSTALL_INTERFACE 两侧同时生效。
target_compile_options(orange_engine PUBLIC
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

# Visual Studio 的 Solution Explorer 默认把所有源文件平铺到一个组里。
# source_group(TREE ...) 让它按源树原结构展开（src/core / src/platform/
# glfw / src/app …），减少在大型工程下找文件的摩擦。FILES 取自 target
# 已记录的 SOURCES，因此后续 add_library 增删都自动跟随，无需在两处
# 维护清单。
get_target_property(_orange_engine_sources orange_engine SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    PREFIX  "Source Files"
    FILES   ${_orange_engine_sources})

# 命名空间别名，让在同一个 build tree 内的 samples / tests 直接链
# `OrangeEngine::orange_engine`——和安装后通过 find_package(OrangeEngine)
# 看到的目标一致，避免接口偷偷漂移。
add_library(OrangeEngine::orange_engine ALIAS orange_engine)

# ---------------------------------------------------------------------------
# 安装规则
#
# 顶层 build 默认 ON，作为 `add_subdirectory()` 嵌入时默认 OFF——和
# OrangeRender 的策略一致：父工程嵌入引擎源码时，install 树由父工程
# 自己掌控。
#
# 安装版图按 GNUInstallDirs：
#   <prefix>/lib/                          —— orange_engine 静态归档
#   <prefix>/include/orange/engine/...     —— 公共头（源树 + 生成）
#   <prefix>/lib/cmake/OrangeEngine/       —— Targets / Config / 版本文件
#
# 公共依赖在 OrangeEngineConfig.cmake 通过 find_dependency 重新解析；
# 当前 STATIC 构建下，引擎链入的所有 PRIVATE 依赖（nlohmann_json /
# glfw3）也需要消费者侧解析得到，所以一并写进 config 模板。
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_INSTALL "Generate install rules for OrangeEngine" "${PROJECT_IS_TOP_LEVEL}")

if (ORANGE_ENGINE_INSTALL)
    include(GNUInstallDirs)
    set(ORANGE_ENGINE_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/OrangeEngine")

    install(TARGETS orange_engine
        EXPORT  OrangeEngineTargets
        ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    # 源树公共头：保留 `<orange/engine/...>` 的 layout。FILES_MATCHING
    # 模式把 .in 模板挡在外面，消费者只看到 configure 之后的产物。
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/orange"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
    )

    # 生成出来的版本头与 export 头：与源树头放同一个目录，让
    # `#include <orange/engine/OrangeEngineVersion.h>` /
    # `#include <orange/engine/OrangeEngineExport.h>` 在 in-tree 与
    # post-install 两种语境下解析一致。
    install(FILES
            "${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}/orange/engine/OrangeEngineVersion.h"
            "${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}/orange/engine/OrangeEngineExport.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/orange/engine"
    )

    install(EXPORT OrangeEngineTargets
        FILE        OrangeEngineTargets.cmake
        NAMESPACE   OrangeEngine::
        DESTINATION "${ORANGE_ENGINE_INSTALL_CMAKE_DIR}"
    )

    # -----------------------------------------------------------------------
    # OrangeEngineConfig.cmake + 版本文件
    #
    # configure_package_config_file 走 @PACKAGE_INIT@ 展开，让
    # `find_package(OrangeEngine CONFIG)` 不论消费者把 SDK 放在哪都能
    # 自洽解析。
    #
    # SameMinorVersion：消费者 `find_package(OrangeEngine 0.1 ...)`
    # 接受 0.1.x，但拒绝 0.2.x——pre-1.0 阶段 minor bump 仍允许 ABI 变
    # 动，这一档兼容性最稳妥。
    #
    # spdlog 这个可选依赖的"是否实际进入 link 接口"作为模板变量写到
    # 生成的 OrangeEngineConfig.cmake 里：消费者只在引擎确实链入时
    # 才被要求 find_dependency，避免因 soft dep 缺失误伤所有消费者。
    # EnTT 已经在 Dependencies.cmake 升为 REQUIRED，无需开关。
    # -----------------------------------------------------------------------
    set(ORANGE_ENGINE_PUBLIC_HAS_SPDLOG ${ORANGE_ENGINE_WITH_SPDLOG})

    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OrangeEngineConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEngineConfig.cmake"
        INSTALL_DESTINATION "${ORANGE_ENGINE_INSTALL_CMAKE_DIR}"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEngineConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEngineConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEngineConfigVersion.cmake"
        DESTINATION "${ORANGE_ENGINE_INSTALL_CMAKE_DIR}"
    )
endif ()

# ---------------------------------------------------------------------------
# 测试
#
# 默认 OFF：作为下游 SDK 被消费时，引擎自身的 unit / smoke test 没
# 有理由跟着 build。引擎自己迭代时打开它即可。tests 目录目前只是
# stub，待测试入口落地后再 add_subdirectory。
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_BUILD_TESTS "Build OrangeEngine unit / smoke tests" OFF)

if (ORANGE_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# ---------------------------------------------------------------------------
# Samples
#
# Sample 子工程随各阶段里程碑逐个加入；samples/CMakeLists.txt 内部
# 按顺序 add_subdirectory 每一个 sample。
# ---------------------------------------------------------------------------
add_subdirectory(samples)
