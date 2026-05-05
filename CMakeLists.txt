cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# Phase 1 / Task 02 — single source of truth for the OrangeEngine semantic
# version. Bump here when cutting a release; the generated
# `OrangeEngineVersion.h` (see `configure_file` below) carries the same
# numbers into C++ code as integer + string macros, and Phase 1 / Task 11's
# `OrangeEngineConfigVersion.cmake` will read them back via the standard
# `${PROJECT_VERSION}` variables CMake derives from this declaration.
# ---------------------------------------------------------------------------
project(OrangeEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Unified output directories
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Compiler options
# ---------------------------------------------------------------------------
include(cmake/CompilerOptions.cmake)

# ---------------------------------------------------------------------------
# Dependencies
#
# Phase 1 / Task 03 — all third-party `find_package` calls live in
# `cmake/Dependencies.cmake`; the same file documents PUBLIC vs PRIVATE
# linkage and is the single source of truth for what the engine consumes.
# ---------------------------------------------------------------------------
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# OrangeEngine static library
# ---------------------------------------------------------------------------
add_library(orange_engine STATIC
    src/EngineStub.cpp

    # Phase 1 / Task 04 — Core
    src/core/CoreHeaderCheck.cpp
    src/core/Log.cpp
    src/core/Result.cpp

    # Phase 1 / Task 05 — Core::Serialization + Config
    src/core/Config.cpp
    src/core/SchemaVersion.cpp
    src/core/Serialization.cpp
)

# ---------------------------------------------------------------------------
# Phase 1 / Task 02 — generate `orange/engine/OrangeEngineVersion.h`.
#
# Lives in the build tree under `${CMAKE_BINARY_DIR}/generated/orange/engine/`
# so the source tree stays clean (the .in template is the only versioned
# file). The generated directory is added as a PUBLIC include path so
# consumers of orange_engine — samples, tests, and downstream find_package
# users — see the header at the same `<orange/engine/...>` path as
# everything else, and we add `@ONLY` to prevent CMake from interpreting
# any unrelated `${...}` substitutions inside the template body.
# ---------------------------------------------------------------------------
set(ORANGE_ENGINE_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/orange/engine/OrangeEngineVersion.h.in"
    "${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}/orange/engine/OrangeEngineVersion.h"
    @ONLY
)

# ---------------------------------------------------------------------------
# Phase 1 / Task 02 — `ORANGE_ENGINE_API` visibility macro.
#
# `generate_export_header` writes a header that defines `ORANGE_ENGINE_API`
# as:
#   - empty whenever `ORANGE_ENGINE_STATIC_DEFINE` is set (static-build path),
#   - `__declspec(dllexport)` when CMake's auto-defined
#     `orange_engine_EXPORTS` is in effect (compiling orange_engine itself
#     as a SHARED library), and
#   - `__declspec(dllimport)` otherwise (consumers of a SHARED build).
#
# Today `BUILD_SHARED_LIBS` defaults to OFF and `add_library(... STATIC ...)`
# is hard-coded above, so the macro stays empty everywhere — no ABI cost,
# no consumer-visible change. The point is to seed every public class with
# the macro slot now, so flipping to a SHARED build later is a one-line
# CMake change rather than a per-header sweep.
#
# `STATIC_DEFINE` is propagated PUBLIC and via `INSTALL_INTERFACE` so
# consumers of an installed static SDK see the macro as empty too — without
# this, a consumer compiling against an installed static-built orange_engine
# would default to `dllimport` and trip linker errors.
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
# Include paths
#
# `include/` is the public surface; `src/` is private. `${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}`
# ships with PUBLIC visibility so consumers see the generated version /
# export headers under `<orange/engine/...>`.
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
# Linkage
#
# PUBLIC vs PRIVATE policy is documented in cmake/Dependencies.cmake. The
# rules in brief:
#   * OrangeRender / EnTT / glm appear in public engine API surface, so
#     they are linked PUBLIC and consumers inherit them.
#   * nlohmann_json wraps Core::Serialization and must NOT leak through
#     the public headers — PRIVATE linkage.
#   * spdlog (optional) is wrapped by Core::Log; PUBLIC propagation lets
#     consumers reuse the same logger instance if they want, mirroring
#     OrangeRender's policy.
#   * Box2D / DragonBones / miniaudio / ImGui / stb stay PRIVATE — see
#     越界禁令 in docs/extension-points.md.
# ---------------------------------------------------------------------------
target_link_libraries(orange_engine
    PUBLIC
        OrangeRender::orange_render
        ${ORANGE_ENGINE_GLM_TARGET}
)

# Soft / deferred deps — link only when their imported targets actually
# exist. Phase 2 / Task 04 will bump EnTT to a hard requirement (and remove
# this conditional); Phase 1 / Task 05 will do the same for nlohmann_json.
if (TARGET EnTT::EnTT)
    target_link_libraries(orange_engine PUBLIC EnTT::EnTT)
endif ()

# Phase 1 / Task 05 — nlohmann_json is REQUIRED via Dependencies.cmake; the
# target is guaranteed to exist here. Linkage is PRIVATE so the type does
# not leak through OrangeEngine's public headers.
target_link_libraries(orange_engine PRIVATE nlohmann_json::nlohmann_json)

if (ORANGE_ENGINE_WITH_SPDLOG)
    target_link_libraries(orange_engine PUBLIC spdlog::spdlog)
    target_compile_definitions(orange_engine PUBLIC ORANGE_ENGINE_WITH_SPDLOG=1)
endif ()

if (ORANGE_ENGINE_WITH_TRACY)
    target_link_libraries(orange_engine PRIVATE Tracy::TracyClient)
    target_compile_definitions(orange_engine PRIVATE ORANGE_ENGINE_WITH_TRACY=1)
endif ()

orange_engine_set_compiler_options(orange_engine)

# Namespaced alias so consumers in the same build tree (samples, tests) link
# `OrangeEngine::orange_engine` — matches the post-install
# `find_package(OrangeEngine)` interface and prevents accidental drift.
add_library(OrangeEngine::orange_engine ALIAS orange_engine)

# ---------------------------------------------------------------------------
# Install rules
#
# Phase 1 / Task 02 declares the option only; Task 11 fills in the
# `install(TARGETS ...)` / `OrangeEngineConfig.cmake` machinery. Defaulting
# ON at the top level + OFF when consumed via `add_subdirectory()` matches
# OrangeRender's policy.
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_INSTALL "Generate install rules for OrangeEngine" "${PROJECT_IS_TOP_LEVEL}")

# ---------------------------------------------------------------------------
# Tests
#
# Default OFF: a downstream consumer of OrangeEngine does not need the
# in-tree unit / smoke tests; gating them keeps the consumer's build clean
# and skips test compile cost. Flip ON when iterating on the engine itself.
# Tests directory is a stub today; populated starting at Phase 1 / Task 10.
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_BUILD_TESTS "Build OrangeEngine unit / smoke tests" OFF)

if (ORANGE_ENGINE_BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests)  # Activated at Task 10
endif ()

# ---------------------------------------------------------------------------
# Samples
#
# Sample sub-projects are added module by module; the first one
# (`samples/01_minimal_window`) lands at Phase 1 / Task 09. Until then the
# directory exists but holds only a .gitkeep, so we do not add it.
# ---------------------------------------------------------------------------
# add_subdirectory(samples)  # Activated at Task 09
