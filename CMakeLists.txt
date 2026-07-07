cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# OrangeEngine 语义版本号的唯一权威来源。发版时改这里；下面
# `configure_file` 生成的 `OrangeEngineVersion.h` 会把同一组数字带到
# C++ 端（整型 + 字符串宏），后续的 `OrangeEngineConfigVersion.cmake`
# 也通过 CMake 自动派生的 `${PROJECT_VERSION}` 变量回读这里的值。
# ---------------------------------------------------------------------------
# LANGUAGES 含 C：引擎本体与 OrangeEditor 全是 C++，但 OrangeEditor 的
# DCC import 链 vendor 了一个 C 源——MikkTSpace（vendor/mikktspace/mikktspace.c，
# 标准切线空间算法）。CMake 在未启用 C 时会**静默丢弃**加进 target 的 .c 源
# （不编、不报错，链接期才暴露缺符号），故在顶层就启用 C，覆盖 editor 与
# tests 两个会编译该 .c 的子目录（MSVC 的 cl.exe 同时是 C / C++ 编译器）。
project(OrangeEngine VERSION 0.1.0 LANGUAGES C CXX)

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
# OrangeEngine 库
#
# 链接形态遵循 `BUILD_SHARED_LIBS`：默认 OFF → STATIC（发布单 exe 全静态链）；
# `-DBUILD_SHARED_LIBS=ON` → SHARED（orange_engine.dll，editor.exe + game.dll
# 共享单份引擎，收敛 ImGui GImGui / Core::Log / EnTT 进程级单例，见 ADR-023）。
# ORANGE_ENGINE_API 导出宏由下方 generate_export_header 按此开关自动切换。
# ---------------------------------------------------------------------------
add_library(orange_engine
    src/EngineStub.cpp

    # 便利聚合头（prelude + 各子系统 <orange/engine/<name>.h>）自包含性检查 TU。
    src/PublicHeaderAggregateCheck.cpp

    # Core
    src/core/CoreHeaderCheck.cpp
    src/core/Log.cpp
    src/core/Memory.cpp
    src/core/Profiler.cpp
    src/core/Result.cpp

    # Core：Serialization + Config
    src/core/Config.cpp
    src/core/Guid.cpp
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
    src/asset/MeshAsset.cpp
    src/asset/MeshLoader.cpp
    src/asset/PrefabLoader.cpp
    src/asset/SpirvDiskLoader.cpp
    src/asset/TextureLoader.cpp
    src/asset/ShaderLoader.cpp

    # Scene：Entity / Transform / Hierarchy / Name / ISystem / World
    # （公共接口 + entt::registry 后端）+ SceneSerialization（顶层 schema
    # 调度 + entity 持久 ID 重映射）+ ComponentSerializers（各内置组件
    # 的 free-function Read / Write）
    src/scene/SceneHeaderCheck.cpp
    src/scene/World.cpp
    src/scene/SceneSerialization.cpp
    src/scene/ComponentSerializers.cpp
    src/scene/EntityGuid.cpp
    src/scene/PrefabInstantiation.cpp
    src/scene/PrefabOverride.cpp
    src/scene/WorldPartition.cpp
    src/scene/TransformSystem.cpp

    # Render：Camera / RenderableComponent / Pipeline 公共接口
    # + RenderScene 收集（World → drawable list）+ Pipeline 接通
    # OrangeRender RenderGraph + Material / MaterialInstance 公共接口
    # + BuiltinMaterials（toon / rim_light）
    # + PostProcessChain（HDR / Bloom / Tonemap / LUT 默认链描述符）
    # + MaterialSystem（自定义 shader 注入扩展点）
    # + BuiltinShadowShaders（shadow_caster shader pair 工厂）
    # + IblBaker（HDR equirect → cube resample / 后续续 BRDF LUT /
    #   irradiance / prefiltered specular 三种卷积烘焙路径）
    src/render/RenderHeaderCheck.cpp
    src/render/BuiltinMaterials.cpp
    src/render/BuiltinPostProcessChain.cpp
    src/render/BuiltinShadowShaders.cpp
    src/render/DebugDrawScene.cpp
    src/render/IblBaker.cpp
    src/render/MaterialInstance.cpp
    src/render/MaterialSystem.cpp
    src/render/Pipeline.cpp
    src/render/pipeline/PipelineBloom.cpp
    src/render/pipeline/PipelineCapture.cpp
    src/render/pipeline/PipelineDebugDraw.cpp
    src/render/pipeline/PipelineGodRays.cpp
    src/render/pipeline/PipelineHelpers.cpp
    src/render/pipeline/PipelineSetup.cpp
    src/render/pipeline/PipelineShadow.cpp
    src/render/pipeline/PipelineSsao.cpp
    src/render/pipeline/PipelineSsr.cpp
    src/render/pipeline/PipelineNormalPrepass.cpp
    src/render/pipeline/PipelineContactShadow.cpp
    src/render/pipeline/PipelineDof.cpp
    src/render/pipeline/PipelineTaa.cpp
    src/render/pipeline/PipelineColorGrade.cpp
    src/render/pipeline/PipelineMotionBlur.cpp
    src/render/pipeline/PipelineLens.cpp
    src/render/pipeline/PipelineSharpen.cpp
    src/render/pipeline/PipelineSky.cpp
    src/render/pipeline/PipelineImGui.cpp
    src/render/PostProcessChain.cpp
    src/render/PostProcessPasses.cpp
    src/render/RenderScene.cpp
    src/render/VfxSystem.cpp

    # Animation：IAnimator / AnimationStateMachine / AnimatorComponent /
    # AnimatorRegistry 公共接口（Phase 4 / Task 01）。Skeletal /
    # Procedural 后端实现见 src/animation/dragonbones / Task 03 / 04。
    src/animation/AnimationStateMachine.cpp
    src/animation/AnimationSystem.cpp
    src/animation/AnimatorRegistry.cpp
    src/animation/ClipAnimator.cpp
    src/animation/BlendSpaceAnimator.cpp
    src/animation/AnimationClipSerialization.cpp
    src/animation/AnimationClipLoader.cpp
    src/animation/ProceduralAnimator.cpp

    # Animation：DragonBones runtime 接入层（Phase 4 / Task 02 + 03）。
    # 这一组 .cpp 是 OrangeEngine 内唯一直接 include <dragonBones/...> 的
    # 位置；runtime 自身的 .cpp 编进 orange_engine_dragonbones OBJECT 库
    # 后随 orange_engine.lib 一同发布。
    src/animation/dragonbones/DragonBonesContext.cpp
    src/animation/dragonbones/HeadlessFactory.cpp
    src/animation/dragonbones/SkeletonLoader.cpp
    src/animation/dragonbones/SkeletalAnimator.cpp

    # Asset：SkeletonAsset 值类型 dtor / 移动 / span helper（公共面，纯
    # std 类型；无 dragonbones 依赖）。Loader 在 src/animation/dragonbones/。
    src/asset/SkeletonAsset.cpp

    # Input：ActionMap / InputContext + JSON loader（Phase 4 / Task 08）。
    # nlohmann::json 通过 Core::JsonReader 间接消费——本 .cpp 不直接 include
    # nlohmann，公共 input/ 头零暴露。
    src/input/InputContext.cpp

    # Audio：AudioEngine + SoundInstance（miniaudio 后端，Phase 4 / Task 09）。
    # AudioEngine.cpp 是唯一 #define MINIAUDIO_IMPLEMENTATION 的 TU；
    # miniaudio.h 仅在 src/audio/miniaudio/** 内出现（CLAUDE.md 不变量）。
    # SoundLoader 不消费 miniaudio，但按任务约定放在 src/audio/miniaudio/ 内
    # 与 audio 层共目录，便于日后做声音元数据探测。
    src/audio/miniaudio/AudioEngine.cpp
    src/audio/miniaudio/SoundLoader.cpp

    # Physics：PhysicsWorld / RigidBody / Collider 公共接口（Phase 4 / Task 05）
    # + Box2D 3.x 后端（Phase 4 / Task 06）。Box2D 头只在 src/physics/box2d/
    # 与 src/physics/PhysicsWorld.cpp 内出现，公共 include/ 零 b2 依赖。
    src/physics/PhysicsWorld.cpp
    src/physics/LayerVisibilitySync.cpp
    src/physics/box2d/Box2DBridge.cpp

    # Nav：2D 网格导航 + A* 寻路（NavGrid 数据 header-only；Pathfinding A* +
    # SimplifyPath 与从物理烘焙可行走性 NavGridBake 编进 lib）
    src/nav/Pathfinding.cpp
    src/nav/NavGridBake.cpp

    # Spline：样条 / 曲线子系统（Catmull-Rom / Linear 求值 + tangent + 弧长
    # 参数化匀速遍历）。纯数学，公共头只依赖 glm + std；Catmull-Rom 闭式公式
    # 自实现（不引 glm/gtx，避免污染消费者的 GLM_ENABLE_EXPERIMENTAL）。
    src/spline/Spline.cpp

    # Tilemap：2D 瓦片地图内容原语（TileMap 数据 + tileset header-only）+
    # 贪心碰撞矩形合并（MergeSolidTiles / TileRectToWorldAABB，classic greedy
    # meshing，降低 physics 静态体数量）+ 从 tilemap 烘焙 NavGrid
    # （BuildNavGridFromTileMap，一 tile 一 cell 对齐，实心 → 阻挡）。纯数据 +
    # 算法，公共头只依赖 glm + std + 引擎公共头（nav bake 单独头隔离 nav 依赖）。
    src/tilemap/TileMapCollision.cpp
    src/tilemap/TileMapNavBake.cpp

    # Noise：程序化生成基础（seeded PRNG + value/Perlin 噪声 + fBm 分形）。确定性
    # scalar 数学——供程序化关卡 / 地形高度场 / 摆位扰动，与 tilemap 协同。公共头
    # 只依赖标准库（纯 float，不引 glm）；Rng.h 为 header-only（不进 add_library），
    # value/Perlin/fBm 求值实现在 Noise.cpp。
    src/noise/Noise.cpp

    # Tween：代码驱动的缓动补间（tween / easing）运行时。标量从 from→to 在
    # duration 秒内按缓动曲线插值，每帧 Update(dt) 推进 + 回调消费当前值（对标
    # DOTween / iTween），支持 Once / Loop / PingPong 循环 + delay + 抗再入。公共头
    # 只依赖标准库 + Easing.h（纯 scalar，不引 glm）；Easing.h 为 header-only（31 条
    # easings.net 标准缓动，不进 add_library），TweenManager 运行时实现在 Tween.cpp。
    src/tween/Tween.cpp

    # Spatial：均匀网格空间哈希（uniform-grid spatial hash / broad-phase）—— 引擎首个
    # 空间加速结构。任意带 AABB 的 item（uint64 id）按 cellSize 网格索引，支持近邻规模
    # 的区域 / 点 / 半径查询，跨多格 item 登记进所有覆盖 cell、查询做 narrow-phase 精确
    # 过滤 + dedup。服务 AI 邻居查询 / 剔除 / 生成密度统计。公共头 Aabb2D + AabbOverlap
    # 为 header-only inline；SpatialHash 类运行时实现在 SpatialHash.cpp。只依赖 glm + std。
    src/spatial/SpatialHash.cpp

    # CameraRig：2D 相机跟随控制器（deadzone + 临界阻尼 SmoothDamp + 世界边界钳制）——
    # 纯数据 + 数学，不依赖 Render::Camera / World / GPU，只吃/吐 glm::vec2 世界坐标 + params，
    # 消费者把结果写进自己的相机（解耦，任何 2D/2.5D 游戏复用）。确定性、headless 可测。
    # CameraShake2D：基于 trauma 的相机抖动（juice），自包含 hash 值噪声、确定性可测。
    src/camerarig/CameraFollow.cpp
    src/camerarig/CameraShake.cpp

    # Fsm：通用逻辑状态机（gameplay / AI finite state machine）—— 可复用行为原语
    # （enemy patrol/chase/attack、UI 流程、关卡阶段等）。状态 + 转移（事件触发 /
    # guard 自动）+ 生命周期回调（onEnter/onUpdate/onExit），抗再入链式转移靠 pending
    # 队列。区别于 animation/AnimationStateMachine（那是动画剪辑专用）：本模块纯逻辑，
    # state/event 是用户自定义 int，与动画 / 渲染解耦。公共头只依赖标准库（不引 glm）。
    src/fsm/StateMachine.cpp

    # Save：SaveGameRegistry —— 游戏侧 component 注册容器，供 Save / Load
    # 主流程按 type-erased entry 派发。引擎不内置任何 component 注册，全
    # 部由游戏侧调用 Register<TComp> 手写 read/write 接入。
    # SaveGameSystem —— Save / Load 主流程：原子写入 (.tmp + fsync + rename)
    # + 16 字节 binary header + IEEE 802.3 CRC32 + 顶层 / 每组件 schema 校验。
    # SavePath —— 平台相关 user-data 根目录解析（Windows: SHGetKnownFolderPath
    # FOLDERID_RoamingAppData；其它平台返回 Unsupported）+ vendor/game/subfolder
    # 拼装 + mkdir -p。
    # AtomicWrite —— 内部共享原语：".tmp + fsync + rename" 三步原子写入，
    # 由 SaveGameSystem 与 SlotManager 共用。
    # SlotManager —— 多存档槽 + sidecar `.meta.json` 元数据；上层 UI 读档
    # 列表的入口。
    # AutosaveScheduler —— 帧驱动的间隔触发 + 节流 + 手动请求队列。
    src/save/AtomicWrite.cpp
    src/save/AutosaveScheduler.cpp
    src/save/SaveGameRegistry.cpp
    src/save/SaveGameSystem.cpp
    src/save/SavePath.cpp
    src/save/SlotManager.cpp
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
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/tonemap.vert.glsl"
    ORANGE_ENGINE_BUILTIN_TONEMAP_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/tonemap.frag.glsl"
    ORANGE_ENGINE_BUILTIN_TONEMAP_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/additive_billboard.vert.glsl"
    ORANGE_ENGINE_BUILTIN_PARTICLE_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/additive_billboard.frag.glsl"
    ORANGE_ENGINE_BUILTIN_PARTICLE_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/dissolve.vert.glsl"
    ORANGE_ENGINE_BUILTIN_DISSOLVE_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/dissolve.frag.glsl"
    ORANGE_ENGINE_BUILTIN_DISSOLVE_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/emissive.vert.glsl"
    ORANGE_ENGINE_BUILTIN_EMISSIVE_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/emissive.frag.glsl"
    ORANGE_ENGINE_BUILTIN_EMISSIVE_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/halo.vert.glsl"
    ORANGE_ENGINE_BUILTIN_HALO_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/halo.frag.glsl"
    ORANGE_ENGINE_BUILTIN_HALO_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/god_rays.frag.glsl"
    ORANGE_ENGINE_BUILTIN_GODRAYS_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ssao.frag.glsl"
    ORANGE_ENGINE_BUILTIN_SSAO_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ssao_apply.frag.glsl"
    ORANGE_ENGINE_BUILTIN_SSAO_APPLY_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/gtao.frag.glsl"
    ORANGE_ENGINE_BUILTIN_GTAO_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/contact_shadow.frag.glsl"
    ORANGE_ENGINE_BUILTIN_CONTACT_SHADOW_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/dof.frag.glsl"
    ORANGE_ENGINE_BUILTIN_DOF_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/dof_composite.frag.glsl"
    ORANGE_ENGINE_BUILTIN_DOF_COMPOSITE_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/taa.frag.glsl"
    ORANGE_ENGINE_BUILTIN_TAA_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/color_grade.frag.glsl"
    ORANGE_ENGINE_BUILTIN_COLOR_GRADE_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/motion_blur.frag.glsl"
    ORANGE_ENGINE_BUILTIN_MOTION_BLUR_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/lens.frag.glsl"
    ORANGE_ENGINE_BUILTIN_LENS_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/sharpen.frag.glsl"
    ORANGE_ENGINE_BUILTIN_SHARPEN_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ssr.frag.glsl"
    ORANGE_ENGINE_BUILTIN_SSR_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ssr_composite.frag.glsl"
    ORANGE_ENGINE_BUILTIN_SSR_COMPOSITE_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/normal_prepass.vert.glsl"
    ORANGE_ENGINE_BUILTIN_NORMAL_PREPASS_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/normal_prepass.frag.glsl"
    ORANGE_ENGINE_BUILTIN_NORMAL_PREPASS_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/pbr.vert.glsl"
    ORANGE_ENGINE_BUILTIN_PBR_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/pbr.frag.glsl"
    ORANGE_ENGINE_BUILTIN_PBR_FS_SPV)
# debug render views · normals 模板（DebugViewMode::Normals，world-normal-as-RGB）
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_normals.vert.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_NORMALS_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_normals.frag.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_NORMALS_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_unlit.vert.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_UNLIT_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_unlit.frag.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_UNLIT_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_overdraw.vert.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_OVERDRAW_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_overdraw.frag.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_OVERDRAW_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_wireframe.vert.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_WIREFRAME_VS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/debug_wireframe.frag.glsl"
    ORANGE_ENGINE_BUILTIN_DEBUG_WIREFRAME_FS_SPV)
# v1.2 T3 · unlit 模板（复用 pbr.vert SPV，仅新增 frag）
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/unlit.frag.glsl"
    ORANGE_ENGINE_BUILTIN_UNLIT_FS_SPV)
# v1.2 T4 · water_basic 模板（复用 pbr.vert SPV，仅新增 frag）
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/water_basic.frag.glsl"
    ORANGE_ENGINE_BUILTIN_WATER_BASIC_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ibl_equirect_to_cube.comp.glsl"
    ORANGE_ENGINE_BUILTIN_IBL_EQUIRECT_CS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ibl_brdf_lut.comp.glsl"
    ORANGE_ENGINE_BUILTIN_IBL_BRDF_LUT_CS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ibl_irradiance.comp.glsl"
    ORANGE_ENGINE_BUILTIN_IBL_IRRADIANCE_CS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/ibl_prefilter.frag.glsl"
    ORANGE_ENGINE_BUILTIN_IBL_PREFILTER_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/sky.frag.glsl"
    ORANGE_ENGINE_BUILTIN_SKY_FS_SPV)
orange_engine_compile_builtin_shader(
    "src/render/builtin_shaders/procedural_sky.frag.glsl"
    ORANGE_ENGINE_BUILTIN_PROCEDURAL_SKY_FS_SPV)

# 全部内置 SPIR-V 产物路径汇成一个列表 —— 既喂 custom target 的 DEPENDS，
# 也喂下方 install(FILES ...)（GAP-2026-05-27-builtin-shaders-not-installed-
# for-consumers：外部 find_package 消费者过去拿不到这些 spv，链接过但运行期
# 加载 builtin material/shadow/vfx shader 扑空跑不起来）。每项含 $<CONFIG>
# 生成表达式，install(FILES) 支持 genex，install(DIRECTORY) 不支持，故走 FILES。
set(ORANGE_ENGINE_ALL_BUILTIN_SPV
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
    "${ORANGE_ENGINE_BUILTIN_PASSTHROUGH_COMBINE_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_TONEMAP_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_TONEMAP_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_PARTICLE_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_PARTICLE_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DISSOLVE_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DISSOLVE_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_EMISSIVE_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_EMISSIVE_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_HALO_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_HALO_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_GODRAYS_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_SSAO_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_SSAO_APPLY_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_GTAO_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_CONTACT_SHADOW_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DOF_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DOF_COMPOSITE_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_TAA_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_COLOR_GRADE_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_MOTION_BLUR_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_LENS_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_SHARPEN_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_SSR_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_SSR_COMPOSITE_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_NORMAL_PREPASS_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_NORMAL_PREPASS_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_PBR_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_PBR_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_NORMALS_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_NORMALS_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_UNLIT_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_UNLIT_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_OVERDRAW_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_OVERDRAW_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_WIREFRAME_VS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_DEBUG_WIREFRAME_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_UNLIT_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_WATER_BASIC_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_IBL_EQUIRECT_CS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_IBL_BRDF_LUT_CS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_IBL_IRRADIANCE_CS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_IBL_PREFILTER_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_SKY_FS_SPV}"
    "${ORANGE_ENGINE_BUILTIN_PROCEDURAL_SKY_FS_SPV}")

add_custom_target(orange_engine_builtin_shaders ALL
    DEPENDS ${ORANGE_ENGINE_ALL_BUILTIN_SPV})

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
        # vendor/stb：两份单头实现，BUILD_INTERFACE only，公共面不漏 stb 类型。
        #   * stb_image_write.h —— 仅 src/render 截图路径消费
        #   * stb_image.h       —— 仅 src/asset/TextureLoader.cpp 消费（.hdr
        #     Radiance equirect → RGBA32Float TextureAsset；其它后缀仍走自
        #     有 ORTX 容器）
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb>"
        # miniaudio：单头库，仅 src/audio/miniaudio/** PRIVATE 消费。路径
        # 由 Dependencies.cmake 的 find_path 解出来；BUILD_INTERFACE only，
        # install 端不发任何 miniaudio 头给消费者。
        "$<BUILD_INTERFACE:${ORANGE_ENGINE_MINIAUDIO_INCLUDE_DIR}>"
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
#   * Box2D / DragonBones / miniaudio / stb 一律 PRIVATE——
#     越界禁令见 docs/extension-points.md。
#   * ImGui 例外：自 GAP-2026-05-27 起 PUBLIC（消费者 Layer::OnImGui 用），
#     见下方 orange_engine_imgui 段 + ADR。
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

# Box2D 3.x：Physics 后端。PRIVATE，b2 类型只在 src/physics/box2d/ +
# src/physics/PhysicsWorld.cpp 内出现，公共头零暴露。
target_link_libraries(orange_engine PRIVATE box2d::box2d)

# Vulkan headers —— src/render/pipeline/PipelineImGui.cpp 直接 #include
# <vulkan/vulkan.h>（ImGui Vulkan backend 托管）。PRIVATE：仅 orange_engine
# 自身编译用 include path，不传播 vulkan 头给消费者公共面（消费者 OnImGui 只
# 用 <imgui.h> core）。NO_PROTOTYPES 全在 imgui lib 侧，本 TU 不直接引用 vk
# 符号（全走 loader 解析的函数指针），故无需链 vulkan-1.lib。
target_link_libraries(orange_engine PRIVATE Vulkan::Vulkan)

# ---------------------------------------------------------------------------
# DragonBones C++ runtime（Phase 4 / Task 02）——in-tree 源码集成
#
# DragonBones 上游没有 CMake config / 没有 find_package 端点。标准做法
# 是把 vendor/DragonBones/DragonBones/src/dragonBones/ 这一支拉进自己
# 的 build。这里走 OBJECT 库：
#   * 源文件以 .obj 形式被 orange_engine.lib 吸收，消费者无需多链一
#     个 lib，install 路径也只发 orange_engine.lib；
#   * include 路径以 PUBLIC 暴露给本 OBJECT 库，再由 PRIVATE 链回
#     orange_engine——`#include <dragonBones/...>` 在
#     src/animation/dragonbones/DragonBonesContext.cpp 内就能解析，但
#     不传播到 orange_engine 的公共 INTERFACE，更不传播到 install 端。
#
# 警告策略：runtime 是第三方 2018 期代码，按 /W4 + /WX 会爆出大量
# old-style cast / signed-unsigned mismatch / unreferenced parameter——
# 这些不是我们维护范围。OBJECT 库单独压成 /W0（GCC/Clang 用 -w），并
# 显式不调 orange_engine_set_compiler_options（避免 /WX 把 runtime 编
# 译炸掉）。
# ---------------------------------------------------------------------------
file(GLOB ORANGE_ENGINE_DRAGONBONES_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/animation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/armature/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/event/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/factory/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/geom/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/model/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src/dragonBones/parser/*.cpp"
)
add_library(orange_engine_dragonbones OBJECT ${ORANGE_ENGINE_DRAGONBONES_SOURCES})
target_include_directories(orange_engine_dragonbones PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/DragonBones/src>"
    # rapidjson 是 DragonBones JSONDataParser 的强依赖——runtime 把
    # rapidjson/document.h 直接 include 在 parser/JSONDataParser.h 里，
    # 任何 include DragonBonesHeaders.h 的 TU（含 src/animation/
    # dragonbones/DragonBonesContext.cpp）都会被传染。上游约定把它放
    # 在 vendor/DragonBones/3rdParty/rapidjson/。
    # BUILD_INTERFACE 内 PUBLIC：路径继承到 orange_engine 自身的编译，
    # 但 install 端不继承（rapidjson 不发到 SDK，consumer 公共头从未引
    # 用 rapidjson 类型）。
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/DragonBones/3rdParty>"
)
set_target_properties(orange_engine_dragonbones PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD              20
    CXX_STANDARD_REQUIRED     ON
    CXX_EXTENSIONS            OFF
    FOLDER                    "Vendor"
)
if (MSVC)
    # /W0 关警告；/utf-8 与父工程一致避免源码代码页警告；显式 /wd... 名单
    # 仅在改 /W0 不够时才加。
    target_compile_options(orange_engine_dragonbones PRIVATE /W0 /utf-8)
else ()
    target_compile_options(orange_engine_dragonbones PRIVATE -w)
endif ()

# OBJECT 库 → PRIVATE 链回 orange_engine：
#   * 编出来的 .obj 拼到 orange_engine 静态归档里（消费者只需 .lib 一份）；
#   * runtime 头的 PUBLIC include 路径继承到 orange_engine 自身的编译，
#     允许 src/animation/dragonbones/*.cpp 解析 <dragonBones/...>；
#   * 因为是 PRIVATE 链接 + BUILD_INTERFACE-only include，consumer 看不
#     到 dragonBones 头，公共 API 表面不被污染。
target_link_libraries(orange_engine PRIVATE orange_engine_dragonbones)

if (ORANGE_ENGINE_WITH_SPDLOG)
    target_link_libraries(orange_engine PUBLIC spdlog::spdlog)
    target_compile_definitions(orange_engine PUBLIC ORANGE_ENGINE_WITH_SPDLOG=1)
endif ()

if (ORANGE_ENGINE_WITH_TRACY)
    target_link_libraries(orange_engine PRIVATE Tracy::TracyClient)
    target_compile_definitions(orange_engine PRIVATE ORANGE_ENGINE_WITH_TRACY=1)
endif ()

# ---------------------------------------------------------------------------
# C# 脚本运行时 host —— CoreCLR via nethost / hostfxr（ADR-017，B1.0 spike）
#
# 默认 OFF：未开启时 src/script/dotnet/ 不参与编译、不链任何 .NET 库，现有
# 构建零影响。开启后把 ScriptHost.cpp（唯一消费 CLR hosting 头的源）编进
# orange_engine，链静态 libnethost.lib（免拷 nethost.dll），并把 Host pack 的
# native 头目录**只**加到该源文件的 include 路径（不污染引擎其余 TU / 公共面）。
#
# native 目录定位：本机 .NET SDK 把 hosting 头 + libnethost.lib 放在
#   <dotnet-root>/packs/Microsoft.NETCore.App.Host.win-x64/<ver>/runtimes/win-x64/native/
# 取最高可用版本；可被 cache 变量 ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR 覆盖。
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_WITH_DOTNET "Embed CoreCLR (nethost/hostfxr) C# script host" OFF)

if (ORANGE_ENGINE_WITH_DOTNET)
    set(ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR "" CACHE PATH
        "Path to Microsoft.NETCore.App.Host.win-x64 <ver>/runtimes/win-x64/native（含 nethost.h / hostfxr.h / libnethost.lib）。留空则自动探测最高版本。")

    if (NOT ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR)
        # dotnet root：优先环境变量 DOTNET_ROOT，否则回退到默认安装路径。
        set(_orange_dotnet_root "$ENV{DOTNET_ROOT}")
        if (NOT _orange_dotnet_root)
            set(_orange_dotnet_root "C:/Program Files/dotnet")
        endif ()
        set(_orange_host_pack_dir
            "${_orange_dotnet_root}/packs/Microsoft.NETCore.App.Host.win-x64")
        if (NOT EXISTS "${_orange_host_pack_dir}")
            message(FATAL_ERROR
                "OrangeEngine: ORANGE_ENGINE_WITH_DOTNET=ON 但没找到 .NET Host pack："
                " ${_orange_host_pack_dir}。请装 .NET SDK，或显式设 "
                "ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR 指向 runtimes/win-x64/native/。")
        endif ()
        # 列出所有版本子目录，取版本号最高的一个（字典序对 8.0.19 / 9.0.8 够用，
        # 但用 VERSION 比较更稳妥）。
        file(GLOB _orange_host_versions
            RELATIVE "${_orange_host_pack_dir}" "${_orange_host_pack_dir}/*")
        set(_orange_best_version "")
        foreach (_v IN LISTS _orange_host_versions)
            if (IS_DIRECTORY "${_orange_host_pack_dir}/${_v}")
                if (_orange_best_version STREQUAL "" OR
                    "${_v}" VERSION_GREATER "${_orange_best_version}")
                    set(_orange_best_version "${_v}")
                endif ()
            endif ()
        endforeach ()
        if (_orange_best_version STREQUAL "")
            message(FATAL_ERROR
                "OrangeEngine: ${_orange_host_pack_dir} 下没有任何版本子目录。")
        endif ()
        set(ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR
            "${_orange_host_pack_dir}/${_orange_best_version}/runtimes/win-x64/native"
            CACHE PATH "" FORCE)
    endif ()

    # 校验 native 目录关键件齐全（头 + import 库 + DLL）。
    if (NOT EXISTS "${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}/nethost.h" OR
        NOT EXISTS "${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}/hostfxr.h" OR
        NOT EXISTS "${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}/nethost.lib" OR
        NOT EXISTS "${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}/nethost.dll")
        message(FATAL_ERROR
            "OrangeEngine: ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR "
            "(${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}) 缺 nethost.h / hostfxr.h / "
            "nethost.lib / nethost.dll 之一。请指向 .NET Host pack 的 runtimes/win-x64/native/。")
    endif ()
    message(STATUS "OrangeEngine: .NET host native dir = ${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}")
    # nethost.dll 的绝对路径暴露给 tests 子目录，供消费 exe 在 post-build 拷到
    # 自身目录（CLR host 运行期需要它在 DLL 搜索路径上）。
    set(ORANGE_ENGINE_DOTNET_NETHOST_DLL
        "${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}/nethost.dll"
        CACHE FILEPATH "" FORCE)

    # 把 host 源加进引擎库。CLR hosting 头只在 ScriptHost.cpp 这一个 TU 消费
    # ——include 路径用 set_source_files_properties 精确限定到它，不传播到其余
    # TU / 公共 INTERFACE（header isolation：nethost.h / hostfxr.h /
    # coreclr_delegates.h 仅 src/script/dotnet/** 可见）。
    #
    # ScriptBindings.cpp（脚本调引擎的 native 绑定函数表）+ ScriptRuntime.cpp
    # （建在 ScriptHost 上的高层门面）同属 src/script/dotnet/ 唯一 CLR 消费区，
    # 但二者**不**直接 include 任何 CLR / hostfxr 头（纯引擎公共面 + C ABI），
    # 故不需要 host native include 目录。
    #
    # ScriptSystem.cpp 在 src/script/（**非** dotnet/）—— 它只经 ScriptRuntime
    # 公共门面驱动脚本生命周期，零 CLR 头依赖，但依赖 ScriptRuntime 故同样
    # 门控在 WITH_DOTNET。
    target_sources(orange_engine PRIVATE
        src/script/dotnet/ScriptHost.cpp
        src/script/dotnet/ScriptBindings.cpp
        src/script/dotnet/ScriptRuntime.cpp
        src/script/ScriptSystem.cpp
        # ScriptGameModule 把 ScriptRuntime + ScriptSystem 包成内置 IGameModule
        # （ADR-021 第二宿主实现）。同样只用 ScriptRuntime 公共门面、零 CLR 头，
        # 但依赖 ScriptRuntime 故一并门控在 WITH_DOTNET。
        src/script/ScriptGameModule.cpp)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/script/dotnet/ScriptHost.cpp"
        PROPERTIES INCLUDE_DIRECTORIES "${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}")

    # 链 import 库 nethost.lib（+ 运行期 nethost.dll）。**不**用静态 libnethost.lib：
    # 后者带自己的 /MT 静态 release CRT，与引擎 / 测试默认的 /MDd（Debug）撞
    # _ITERATOR_DEBUG_LEVEL / RuntimeLibrary 不匹配（LNK2038）。import 库无 CRT
    # 对象，任意配置都干净。get_hostfxr_path 经它 + nethost.dll 解析，内部再
    # LoadLibrary 本机 hostfxr.dll（.NET runtime 安装提供）。PRIVATE：不暴露公共面。
    target_link_libraries(orange_engine PRIVATE
        "${ORANGE_ENGINE_DOTNET_HOST_NATIVE_DIR}/nethost.lib")
    target_compile_definitions(orange_engine PRIVATE ORANGE_ENGINE_WITH_DOTNET=1)
endif ()

# ---------------------------------------------------------------------------
# 内置 ImGui —— 消费者 debug-UI / overlay hook（GAP-2026-05-27-consumer-imgui-
# tuning-hook）
#
# 游戏侧无 play-in-editor，需要在自己进程里叠即时模式 ImGui 调参/调试任意效
# 果。引擎托管 ImGui context + GLFW/Vulkan backend（src/render/pipeline/
# PipelineImGui.cpp），消费者经 Layer::OnImGui() 提交 widget。
#
# **唯一 owner**：imgui 由本仓在这里统一 vendor（FetchContent，与编辑器
# 历史用的同一 docking tag）。编辑器 tools/OrangeEditor **复用这一份**
# （`OrangeEngine::imgui`），不再各自 vendor——否则两份静态 imgui 在编辑器
# exe 链接期撞重复符号（ODR）。
#
# **PUBLIC 暴露**：与其余第三方一律 PRIVATE 的策略不同（见 ADR），imgui 的
# include 目录 + lib 以 INTERFACE/PUBLIC 形式传播给下游消费者——因为消费者
# 的 OnImGui() body 要 `#include <imgui.h>` 直接调 ImGui API。这是反转
# 「ImGui 一律 PRIVATE」invariant 的显式决策，动机见上。`Layer::OnImGui()`
# 公共签名仍零 imgui 类型，header isolation invariant（公共头不漏第三方）
# 不破。
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(orange_engine_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
# BUILD_INTERFACE 指向 FetchContent 源树；INSTALL_INTERFACE 指向 install 后
# SDK 内的 imgui 头落点（见下方 install 段）。core + backends 两个目录都暴露：
# 消费者只用 <imgui.h>（core），编辑器还用 <imgui_impl_glfw.h> / <imgui_impl_vulkan.h>。
target_include_directories(orange_engine_imgui PUBLIC
    "$<BUILD_INTERFACE:${imgui_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>"
    "$<INSTALL_INTERFACE:include/orange_engine_imgui>"
    "$<INSTALL_INTERFACE:include/orange_engine_imgui/backends>"
)
# NO_PROTOTYPES：imgui_impl_vulkan.cpp 不链静态 vulkan-1.lib stub，改运行时
# 函数指针（PipelineImGui.cpp / 编辑器 main.cpp 经 LoadFunctions 喂入 loader）。
# PRIVATE 即可——头不因消费者侧是否定义此宏而 ABI 漂移。
target_compile_definitions(orange_engine_imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
# backend .cpp 编译期需要 Vulkan / GLFW 头（PRIVATE：NO_PROTOTYPES 下不链 vk
# 符号；glfw 仅 impl_glfw 编译期用）。
target_link_libraries(orange_engine_imgui PRIVATE Vulkan::Vulkan glfw)
set_target_properties(orange_engine_imgui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER                    "Vendor"
)
if (MSVC)
    target_compile_options(orange_engine_imgui PRIVATE /W0 /utf-8)
else ()
    target_compile_options(orange_engine_imgui PRIVATE -w)
endif ()
add_library(OrangeEngine::imgui ALIAS orange_engine_imgui)

# PUBLIC 链入 orange_engine：消费者（含 sample 17 / OrangeGames）拿
# OrangeEngine::orange_engine 即传递性获得 imgui 头 + lib。
target_link_libraries(orange_engine PUBLIC orange_engine_imgui)

# 消费者无需自设 C++20——把语言要求随公共接口传播（gap 2 连带项：
# 消费者 CMake 不必重复 set(CMAKE_CXX_STANDARD 20)）。BUILD/INSTALL 两侧生效。
target_compile_features(orange_engine PUBLIC cxx_std_20)

orange_engine_set_compiler_options(orange_engine)

# SHARED 构建下的两处必要处理（STATIC 默认路径宏为空、全静态单份，无需任一）：
# (1) C4251/C4275：导出的公共类含 std::vector / std::string / std::unique_ptr 等 STL
#     成员会触发 MSVC "需 dll-interface" 警告，叠加 /WX 直接失败。生态内 editor.exe /
#     *.dll 全用同一 MSVC + 同一 /MD 运行时（ADR-023），STL 布局跨边界一致，此警告为
#     受控场景误报；PUBLIC 抑制使引擎自身与 dllimport 消费者一致静默。
# (2) ImGui GImGui 跨 DLL：让 imgui 只活在 orange_engine.dll 内并导出（IMGUI_API=
#     dllexport），editor.exe / game.dll 经 dllimport 拿同一份 → 单一 GImGui / allocator，
#     消灭「exe 一份 + dll 一份 context 分裂」（ADR-023 选 SHARED 的首要动机）。
#     orange_engine_imgui 与 orange_engine 的 TU 都编进同一 dll → 都用 dllexport（PRIVATE）；
#     仅对外部消费者用 dllimport（INTERFACE）。orange_engine_imgui 不设 INTERFACE，避免
#     其 dllimport 经 PUBLIC 链回 orange_engine 自身与 PRIVATE dllexport 撞车。
if (BUILD_SHARED_LIBS)
    target_compile_options(orange_engine PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4251 /wd4275>)
    if (MSVC)
        target_compile_definitions(orange_engine_imgui PRIVATE "IMGUI_API=__declspec(dllexport)")
        target_compile_definitions(orange_engine
            PRIVATE   "IMGUI_API=__declspec(dllexport)"
            INTERFACE "IMGUI_API=__declspec(dllimport)")
    endif ()
endif ()

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

    # orange_engine_dragonbones 也加进 export：它是 OBJECT 库，没有
    # 独立 archive 产物（.obj 已被 orange_engine.lib 吸收），但 CMake
    # 要求被 export 的 target 链到的所有 target 都在同一 export set 里。
    # OBJECT 库进 install 不发任何文件，仅在 OrangeEngineTargets.cmake
    # 里登记 target name；消费者拿 OrangeEngine::orange_engine 时不会
    # 也无需直接看到这个 OBJECT target。
    # orange_engine_imgui 也进 export：orange_engine PUBLIC 链到它，CMake
    # 要求被 export 的 target 的所有 PUBLIC 依赖 target 同在一个 export set。
    # 消费者拿 OrangeEngine::orange_engine 即传递性链到 OrangeEngine::imgui +
    # 其 imgui 头（INSTALL_INTERFACE 指向下方安装的 include/orange_engine_imgui）。
    install(TARGETS orange_engine orange_engine_dragonbones orange_engine_imgui
        EXPORT  OrangeEngineTargets
        ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    # imgui 公共头 —— 消费者 OnImGui() body #include <imgui.h>；编辑器
    # standalone consumer 路径还用 backends 两个头。装到独立子目录
    # include/orange_engine_imgui/（与 orange_engine_imgui target 的
    # INSTALL_INTERFACE include 对齐），不混进 <orange/engine/...> layout。
    install(FILES
            "${imgui_SOURCE_DIR}/imgui.h"
            "${imgui_SOURCE_DIR}/imconfig.h"
            "${imgui_SOURCE_DIR}/imgui_internal.h"
            "${imgui_SOURCE_DIR}/imstb_textedit.h"
            "${imgui_SOURCE_DIR}/imstb_rectpack.h"
            "${imgui_SOURCE_DIR}/imstb_truetype.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/orange_engine_imgui"
    )
    install(FILES
            "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.h"
            "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/orange_engine_imgui/backends"
    )

    # 内置 SPIR-V shader —— GAP-2026-05-27-builtin-shaders-not-installed-for-
    # consumers。装到 <prefix>/share/OrangeEngine/shaders/orange_engine/，由
    # OrangeEngineConfig.cmake 暴露 OrangeEngine_SHADER_DIR + helper
    # orange_engine_copy_builtin_shaders(<target>)，让外部消费者在 POST_BUILD
    # 把它们拷到自己 .exe 旁（引擎运行时按 ".exe 相对 shaders/orange_engine/*.spv"
    # 加载）。in-tree samples / 编辑器无需 helper——它们与 orange_engine 共享同一
    # RUNTIME_OUTPUT_DIRECTORY，spv 早已在 .exe 旁。
    install(FILES ${ORANGE_ENGINE_ALL_BUILTIN_SPV}
        DESTINATION "${CMAKE_INSTALL_DATADIR}/OrangeEngine/shaders/orange_engine"
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

# tests / samples 中部分 TU 越界 include 引擎 src/ 内部头、调用未标 ORANGE_ENGINE_API
# 的符号（如 DragonBonesContext / ReadColliderDesc）。SHARED 构建下这些内部符号不从
# orange_engine.dll 导出 → 链接失败。SHARED 是 PIE DLL 宿主链路的产 DLL 配置，仓内
# test/sample 验证走默认 STATIC dev 配置，故 SHARED 下整体跳过。
if (ORANGE_ENGINE_BUILD_TESTS AND BUILD_SHARED_LIBS)
    message(STATUS "OrangeEngine: BUILD_SHARED_LIBS=ON —— tests 越界访问未从 DLL 导出的引擎内部符号，已跳过；测试请用默认 STATIC 构建跑。")
endif ()
if (ORANGE_ENGINE_BUILD_TESTS AND NOT BUILD_SHARED_LIBS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# ---------------------------------------------------------------------------
# Samples
#
# Sample 子工程随各阶段里程碑逐个加入；samples/CMakeLists.txt 内部
# 按顺序 add_subdirectory 每一个 sample。
#
# 默认 ON 仅对顶层 build——作为下游 SDK 被 add_subdirectory 嵌入时
# 默认 OFF，与 ORANGE_ENGINE_BUILD_TESTS / ORANGE_ENGINE_BUILD_EDITOR
# 同策略。日常迭代编辑器时可以显式 -DORANGE_ENGINE_BUILD_SAMPLES=OFF
# 跳过，加快增量构建。
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_BUILD_SAMPLES
    "Build samples/* end-to-end demos"
    "${PROJECT_IS_TOP_LEVEL}")

# 同 tests：samples 部分越界引擎内部符号，SHARED 下不可链接，故仅 STATIC 构建编。
if (ORANGE_ENGINE_BUILD_SAMPLES AND NOT BUILD_SHARED_LIBS)
    add_subdirectory(samples)
endif ()

# ---------------------------------------------------------------------------
# OrangeEditor（tools/OrangeEditor）
#
# 编辑器是双模 CMake 工程：可作为本仓子目录被 add_subdirectory 拉入同一
# sln（开发期主路径，免去两阶段 install），也可作为顶级独立工程通过
# `find_package(OrangeEngine CONFIG)` 消费已 install 的引擎（CI 验证路径，
# `editor_build_smoke` ctest 守门）。两条路径都要求编辑器只链
# `OrangeEngine::orange_engine` 公共 target——API 纪律靠 target_include_
# directories 的 PRIVATE 范围守，不靠"不在同一 sln"守。
#
# 默认 ON 仅对顶层 build——作为引擎下游 SDK 被 add_subdirectory 嵌入时
# 默认 OFF，与 ORANGE_ENGINE_BUILD_TESTS / ORANGE_ENGINE_INSTALL 同策略。
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_BUILD_EDITOR
    "Build tools/OrangeEditor as part of engine sln"
    "${PROJECT_IS_TOP_LEVEL}")

if (ORANGE_ENGINE_BUILD_EDITOR)
    add_subdirectory(tools/OrangeEditor)
    # 让生成的 OrangeEngine.sln 默认启动项是 OrangeEditor。VS_STARTUP_PROJECT
    # 的机制是把指定 target 在 sln 里 listed first，VS 在没历史 .suo 选择时
    # 默认选第一个；不写 ExtensibilityGlobals 的 StartupItem 字段。必须放在
    # add_subdirectory 之后（target 已存在）。注：本机 build/.vs/*.suo 用户
    # 设置优先级高于 sln 顺序，已手工选过别的 startup 后此处不会立即生效，
    # 删 build/.vs/ 重开 sln 可恢复默认。
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT OrangeEditor)
endif ()
