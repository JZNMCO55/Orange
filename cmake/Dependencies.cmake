# Dependencies.cmake — Phase 1 / Task 03
#
# Single source of truth for resolving OrangeEngine's third-party packages
# via `find_package`. Included by the top-level CMakeLists.txt; it must
# leave behind a set of imported targets that are linked further down
# (PUBLIC vs PRIVATE per docs/design-plan.md "Header isolation" rules).
#
# Layout policy
# --------------
# * `OrangeRender::orange_render`  → PUBLIC link (engine code links it; only
#                                     `src/render/` may #include <orange/...>)
# * `EnTT::EnTT`                   → PUBLIC link (ECS registry types appear
#                                     in public engine API once Phase 2 / Task 03
#                                     defines `Orange::Engine::World`)
# * `glm::glm`                     → PUBLIC link (math types in public API,
#                                     also pulled in via OrangeRender)
# * `nlohmann_json::nlohmann_json` → PRIVATE link (Core::Serialization wraps
#                                     it; public headers must not expose
#                                     nlohmann::json types)
# * `box2d::box2d`                 → PRIVATE link (allowed only in
#                                     src/physics/box2d/)
# * `imgui::imgui`                 → PRIVATE link (editor / overlay only)
# * `stb` headers                  → PRIVATE include (header-only)
# * `miniaudio.h`                  → PRIVATE include (header-only;
#                                     allowed only in src/audio/miniaudio/)
# * `dragonBones`                  → PRIVATE link (allowed only in
#                                     src/animation/dragonbones/)
#
# Required-vs-soft policy (Phase 1 / Task 03 starting position)
# -------------------------------------------------------------
# To keep the bootstrap surface minimal as Phase 1 walks tasks one by one,
# packages are searched with REQUIRED only when the engine *currently*
# consumes them. Soft (QUIET) packages get a status line but do not block
# configure when not installed — each subsequent task that introduces
# consumption is responsible for bumping its dep from QUIET to REQUIRED
# in this file.
#
# REQUIRED today (Phase 1 / Task 06):  OrangeRender, glm, nlohmann_json,
#                                      glfw3
# QUIET-deferred:                      EnTT (-> Phase 2 / Task 04)
#                                      box2d (-> Phase 4 / Task 06)
#                                      imgui (-> Phase 6)
#                                      stb / miniaudio / DragonBones
#                                          (in-tree headers; not searched
#                                           with find_package)
#
# Optional gates: spdlog (ORANGE_ENGINE_WITH_SPDLOG, default OFF until
# Core::Log lands at Phase 1 / Task 04), tracy (ORANGE_ENGINE_WITH_TRACY,
# default OFF).

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Required: OrangeRender (consumed by Phase 1 / Task 02)
# ---------------------------------------------------------------------------
find_package(OrangeRender 0.1 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Required: glm (re-affirmed; comes transitively from OrangeRender too)
# ---------------------------------------------------------------------------
find_package(glm CONFIG REQUIRED)
set(ORANGE_ENGINE_GLM_TARGET "")
if (TARGET glm::glm)
    set(ORANGE_ENGINE_GLM_TARGET glm::glm)
elseif (TARGET glm::glm-header-only)
    set(ORANGE_ENGINE_GLM_TARGET glm::glm-header-only)
else ()
    message(FATAL_ERROR
        "glm: neither glm::glm nor glm::glm-header-only is defined after find_package.")
endif ()

# ---------------------------------------------------------------------------
# Soft: EnTT (becomes REQUIRED at Phase 2 / Task 04)
# ---------------------------------------------------------------------------
find_package(EnTT CONFIG QUIET)

# ---------------------------------------------------------------------------
# Required: nlohmann_json (Phase 1 / Task 05 — Core::Serialization)
# ---------------------------------------------------------------------------
find_package(nlohmann_json 3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Required: glfw3 (Phase 1 / Task 06 — Platform::Window)
#
# Today this target is also imported transitively by OrangeRender, so the
# explicit find_package is technically redundant. We declare it anyway:
# OrangeRender plans to privatise its glfw link interface (vendor/.../
# OrangeRenderConfig.cmake notes "Task 13-04 will privatise glfw / volk /
# VMA"), at which point the engine still needs its own resolution since
# `src/platform/glfw/Window.cpp` consumes <GLFW/glfw3.h> directly.
# ---------------------------------------------------------------------------
find_package(glfw3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Optional gates
# ---------------------------------------------------------------------------
# Default OFF for both. spdlog flips ON once Phase 1 / Task 04 (Core::Log)
# starts depending on it; tracy is opt-in for performance work.
option(ORANGE_ENGINE_WITH_SPDLOG "Link spdlog as the Core::Log backend" OFF)
option(ORANGE_ENGINE_WITH_TRACY  "Link Tracy as the profiler backend" OFF)

if (ORANGE_ENGINE_WITH_SPDLOG)
    find_package(spdlog CONFIG REQUIRED)
endif ()

if (ORANGE_ENGINE_WITH_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif ()

# ---------------------------------------------------------------------------
# Soft (deferred): box2d / imgui
# ---------------------------------------------------------------------------
find_package(box2d CONFIG QUIET)
find_package(imgui CONFIG QUIET)

# stb / miniaudio / DragonBones are consumed as in-tree headers/sources;
# they have no CMake config to find. The bootstrap script copies them to
# `${ORANGE_ENGINE_3RDPARTY_PREFIX}/include/` and the consuming source
# adds the include path locally.

# ---------------------------------------------------------------------------
# Diagnostic summary
# ---------------------------------------------------------------------------
message(STATUS "OrangeEngine dependencies:")
message(STATUS "  OrangeRender    : found (${OrangeRender_DIR})")
message(STATUS "  glm target      : ${ORANGE_ENGINE_GLM_TARGET}")
if (TARGET EnTT::EnTT)
    message(STATUS "  EnTT            : found (used from Phase 2 / Task 04)")
else ()
    message(STATUS "  EnTT            : not installed (becomes REQUIRED at Phase 2 / Task 04)")
endif ()
message(STATUS "  nlohmann_json   : found (Core::Serialization, Task 05)")
message(STATUS "  glfw3           : found (Platform::Window, Task 06)")
message(STATUS "  spdlog gate     : ${ORANGE_ENGINE_WITH_SPDLOG}")
message(STATUS "  tracy gate      : ${ORANGE_ENGINE_WITH_TRACY}")
if (TARGET box2d::box2d)
    message(STATUS "  box2d           : found (used from Phase 4 / Task 06)")
else ()
    message(STATUS "  box2d           : not installed (becomes REQUIRED at Phase 4 / Task 06)")
endif ()
if (TARGET imgui::imgui)
    message(STATUS "  imgui           : found (used from Phase 6)")
else ()
    message(STATUS "  imgui           : not installed (becomes REQUIRED at Phase 6)")
endif ()
