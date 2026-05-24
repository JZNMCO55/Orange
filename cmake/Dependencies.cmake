# Dependencies.cmake
#
# OrangeEngine 第三方依赖通过 `find_package` 解析的唯一入口。被顶层
# CMakeLists.txt include；执行完后会留下一组 imported target 给主
# CMakeLists 的 `target_link_libraries` 使用（PUBLIC vs PRIVATE 策略
# 见 docs/design-plan.md 中的 "Header isolation" 一节）。
#
# 链接策略
# --------
# * `OrangeRender::orange_render`  → PUBLIC（引擎代码链接它；只有
#                                     `src/render/` 允许 #include
#                                     <orange/...>）
# * `EnTT::EnTT`                   → PUBLIC（World 在公共 API 暴露
#                                     EnTT 类型）
# * `glm::glm`                     → PUBLIC（数学类型出现在公共 API，
#                                     OrangeRender 也会传递性带过来）
# * `nlohmann_json::nlohmann_json` → PRIVATE（Core::Serialization 包
#                                     裹它；公共头不允许暴露
#                                     nlohmann::json 类型）
# * `box2d::box2d`                 → PRIVATE（仅允许出现在
#                                     src/physics/box2d/）
# * `imgui::imgui`                 → PRIVATE（编辑器 / overlay 用）
# * `stb` 头                       → PRIVATE include（header-only）
# * `miniaudio.h`                  → PRIVATE include（header-only；只
#                                     允许出现在 src/audio/miniaudio/）
# * `dragonBones`                  → PRIVATE（仅允许出现在
#                                     src/animation/dragonbones/）
#
# REQUIRED vs soft（QUIET）策略
# -----------------------------
# 为了让骨架阶段的 bootstrap 表面尽量小，引擎当前真正消费哪个包，那
# 个包就以 REQUIRED 搜索；尚未消费的包以 QUIET 搜索——找到就给一行
# 状态信息，找不到也不阻塞 configure。某个包从 soft 升 REQUIRED，由
# 真正开始消费它的那次改动负责在本文件里改掉。
#
# 当前 REQUIRED：    OrangeRender、glm、nlohmann_json、glfw3、EnTT
# 当前 QUIET：       box2d（待物理模块上线）
#                    imgui（待编辑器 / overlay 上线）
#                    stb / miniaudio / DragonBones（in-tree 头，
#                        不参与 find_package）
#
# 可选开关：spdlog（ORANGE_ENGINE_WITH_SPDLOG，默认 OFF）、tracy
# （ORANGE_ENGINE_WITH_TRACY，默认 OFF）。

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Required：OrangeRender
# ---------------------------------------------------------------------------
find_package(OrangeRender 0.1 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Required：glm（OrangeRender 也会传递性带；这里再次显式声明）
# ---------------------------------------------------------------------------
find_package(glm CONFIG REQUIRED)
set(ORANGE_ENGINE_GLM_TARGET "")
if (TARGET glm::glm)
    set(ORANGE_ENGINE_GLM_TARGET glm::glm)
elseif (TARGET glm::glm-header-only)
    set(ORANGE_ENGINE_GLM_TARGET glm::glm-header-only)
else ()
    message(FATAL_ERROR
        "glm: find_package 之后既没有 glm::glm，也没有 glm::glm-header-only target。")
endif ()

# ---------------------------------------------------------------------------
# Required：EnTT（ECS registry，World 公共 API 暴露 entt::registry）
# ---------------------------------------------------------------------------
find_package(EnTT 3.13 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Required：nlohmann_json（Core::Serialization 使用）
# ---------------------------------------------------------------------------
find_package(nlohmann_json 3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Required：glfw3（Platform::Window 的 GLFW backend 使用）
#
# 当前这个 target 也会通过 OrangeRender 传递性导入，所以本文件里的
# find_package 在严格意义上算冗余。仍然显式声明的原因：OrangeRender
# 计划把 glfw / volk / VMA 等设为 PRIVATE 链接（参见 vendor 仓库的
# OrangeRenderConfig.cmake 里的备注）。一旦那次切换发生，引擎仍需自
# 己解析 glfw3，因为 `src/platform/glfw/Window.cpp` 直接 include 了
# <GLFW/glfw3.h>。
# ---------------------------------------------------------------------------
find_package(glfw3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# 可选开关
# ---------------------------------------------------------------------------
# 二者均默认 OFF。spdlog 在 Core::Log 真正切到它时改默认；tracy 用于
# 性能分析，按需打开。
option(ORANGE_ENGINE_WITH_SPDLOG "Link spdlog as the Core::Log backend" OFF)
option(ORANGE_ENGINE_WITH_TRACY  "Link Tracy as the profiler backend" OFF)
# Note: 历史的 `ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` cmake option 在 v1.3.0
# 已彻底移除。grid pass 真正迁出到编辑器（EditorGridAuxPassProvider 走
# Pipeline::SetAuxPassProvider hook 注入），dummy IBL ambient + scene
# clear color 默认值通过 Pipeline 公共 API 中性化（SetDummyIblAmbient /
# SetSceneClearColor）由消费方显式 override —— engine 默认值不带任何编
# 辑器审美。完整迁出记录见 vendor/Orange-Wiki/case-studies/orange-engine/
# milestones/editor/editor-v1.3.0-acceptance-checklist.md。

if (ORANGE_ENGINE_WITH_SPDLOG)
    find_package(spdlog CONFIG REQUIRED)
endif ()

if (ORANGE_ENGINE_WITH_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif ()

# ---------------------------------------------------------------------------
# Hard：box2d（Phase 4 / Task 06 起 Physics 模块强依赖；3.x ABI）
# ---------------------------------------------------------------------------
find_package(box2d CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Soft（延后）：imgui
# ---------------------------------------------------------------------------
find_package(imgui CONFIG QUIET)

# stb / miniaudio / DragonBones 以 in-tree 头 / 源码方式消费，没有可
# 以 find_package 的 config。bootstrap 脚本会把它们拷到
# `${ORANGE_ENGINE_3RDPARTY_PREFIX}/include/`，消费它们的源码自行加
# 本地 include 路径。

# miniaudio：单头库，安装在 ${prefix}/include/miniaudio/miniaudio.h。
# Audio 模块（Phase 4 / Task 09）走 find_path 在 CMAKE_PREFIX_PATH 上
# 解析。仅 src/audio/miniaudio/** PRIVATE 消费，公共头零暴露。
find_path(ORANGE_ENGINE_MINIAUDIO_INCLUDE_DIR
    NAMES miniaudio/miniaudio.h
    PATH_SUFFIXES include
)
if (NOT ORANGE_ENGINE_MINIAUDIO_INCLUDE_DIR)
    message(FATAL_ERROR
        "OrangeEngine: 在 CMAKE_PREFIX_PATH 下没找到 miniaudio/miniaudio.h。"
        " 请确认 3rdparty bootstrap 已经把 miniaudio 装到 install/include/。")
endif ()

# ---------------------------------------------------------------------------
# 状态摘要
# ---------------------------------------------------------------------------
message(STATUS "OrangeEngine dependencies:")
message(STATUS "  OrangeRender    : found (${OrangeRender_DIR})")
message(STATUS "  glm target      : ${ORANGE_ENGINE_GLM_TARGET}")
message(STATUS "  EnTT            : found")
message(STATUS "  nlohmann_json   : found")
message(STATUS "  glfw3           : found")
message(STATUS "  spdlog gate     : ${ORANGE_ENGINE_WITH_SPDLOG}")
message(STATUS "  tracy gate      : ${ORANGE_ENGINE_WITH_TRACY}")
message(STATUS "  box2d           : found (${box2d_DIR})")
message(STATUS "  DragonBones     : in-tree (vendor/DragonBones/DragonBones/src)")
message(STATUS "  miniaudio       : found (${ORANGE_ENGINE_MINIAUDIO_INCLUDE_DIR})")
if (TARGET imgui::imgui)
    message(STATUS "  imgui           : found")
else ()
    message(STATUS "  imgui           : not installed (deferred soft dep)")
endif ()
