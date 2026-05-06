# OrangeEngine 安装版图 smoke test。
#
# ctest 通过 `cmake -P install_smoke.cmake` 调用：把当前 build 树以
# `cmake --install` 安装到一个 scratch prefix，然后断言后续
# `find_package(OrangeEngine CONFIG REQUIRED)` 消费者会用到的每件
# 工件都落到 GNUInstallDirs 约定的位置：
#   - 静态归档落在 `lib/`
#   - 源树公共头（且只有头，.in 模板必须留下）落在
#     `include/orange/engine/...`
#   - configure_file 生成的版本头与 export 头与源树头同目录
#   - export set + per-config 胶水落在 `lib/cmake/OrangeEngine/`
#
# 由 ctest 包装层注入的 cache 变量：
#   ORANGE_ENGINE_BUILD_DIR    —— 用作 install 源的 build tree
#   ORANGE_ENGINE_INSTALL_DIR  —— scratch prefix；脚本会清空并重建
#   ORANGE_ENGINE_CONFIG       —— Debug / Release / RelWithDebInfo / MinSizeRel

if (NOT DEFINED ORANGE_ENGINE_BUILD_DIR
        OR NOT DEFINED ORANGE_ENGINE_INSTALL_DIR
        OR NOT DEFINED ORANGE_ENGINE_CONFIG)
    message(FATAL_ERROR
        "install_smoke.cmake: 必须以 -DORANGE_ENGINE_BUILD_DIR / "
        "-DORANGE_ENGINE_INSTALL_DIR / -DORANGE_ENGINE_CONFIG 调用。")
endif ()

file(REMOVE_RECURSE "${ORANGE_ENGINE_INSTALL_DIR}")
file(MAKE_DIRECTORY "${ORANGE_ENGINE_INSTALL_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        --install "${ORANGE_ENGINE_BUILD_DIR}"
        --config  "${ORANGE_ENGINE_CONFIG}"
        --prefix  "${ORANGE_ENGINE_INSTALL_DIR}"
    RESULT_VARIABLE _install_rc
    OUTPUT_VARIABLE _install_log
    ERROR_VARIABLE  _install_log
)
if (NOT _install_rc EQUAL 0)
    message(FATAL_ERROR
        "[install_smoke] cmake --install 失败 (rc=${_install_rc}):\n${_install_log}")
endif ()

# 辅助：断言相对路径在 install prefix 下存在。
function(_orange_engine_check_path rel_path)
    set(abs "${ORANGE_ENGINE_INSTALL_DIR}/${rel_path}")
    if (NOT EXISTS "${abs}")
        message(FATAL_ERROR "[install_smoke] missing: ${rel_path}")
    endif ()
endfunction()

# ---- 静态归档 -------------------------------------------------------------
# Windows 下走 lib/orange_engine.lib；MinGW 走 .a。两路都接受。
file(GLOB _orange_engine_archive
    "${ORANGE_ENGINE_INSTALL_DIR}/lib/orange_engine.lib"
    "${ORANGE_ENGINE_INSTALL_DIR}/lib/liborange_engine.a"
    "${ORANGE_ENGINE_INSTALL_DIR}/lib/orange_engine.a")
if (_orange_engine_archive STREQUAL "")
    message(FATAL_ERROR
        "[install_smoke] 没有在 ${ORANGE_ENGINE_INSTALL_DIR}/lib/ 下找到静态归档。")
endif ()

# ---- 公共头 ---------------------------------------------------------------
_orange_engine_check_path("include/orange/engine/core/Result.h")
_orange_engine_check_path("include/orange/engine/core/Serialization.h")
_orange_engine_check_path("include/orange/engine/core/SchemaVersion.h")
_orange_engine_check_path("include/orange/engine/platform/Window.h")
_orange_engine_check_path("include/orange/engine/platform/WindowEvent.h")
_orange_engine_check_path("include/orange/engine/app/AppHost.h")
_orange_engine_check_path("include/orange/engine/app/Layer.h")
_orange_engine_check_path("include/orange/engine/app/LayerStack.h")
_orange_engine_check_path("include/orange/engine/app/AppConfig.h")
_orange_engine_check_path("include/orange/engine/app/FrameContext.h")

# 生成出来的版本头 + export 头：必须与源树公共头同目录可被消费者
# include。
_orange_engine_check_path("include/orange/engine/OrangeEngineVersion.h")
_orange_engine_check_path("include/orange/engine/OrangeEngineExport.h")

# .in 模板绝对不允许泄漏到 install tree——只有 configure_file 出来
# 的产物才是消费者可见的。
if (EXISTS "${ORANGE_ENGINE_INSTALL_DIR}/include/orange/engine/OrangeEngineVersion.h.in")
    message(FATAL_ERROR
        "[install_smoke] OrangeEngineVersion.h.in 泄漏到了 install tree。")
endif ()

# ---- export set + Config 文件 --------------------------------------------
_orange_engine_check_path("lib/cmake/OrangeEngine/OrangeEngineTargets.cmake")
_orange_engine_check_path("lib/cmake/OrangeEngine/OrangeEngineConfig.cmake")
_orange_engine_check_path("lib/cmake/OrangeEngine/OrangeEngineConfigVersion.cmake")

# per-config 文件名为 OrangeEngineTargets-<config-lower>.cmake。
string(TOLOWER "${ORANGE_ENGINE_CONFIG}" _cfg_lower)
_orange_engine_check_path("lib/cmake/OrangeEngine/OrangeEngineTargets-${_cfg_lower}.cmake")

# Targets 文件必须把 orange_engine 命名空间到 OrangeEngine::orange_engine。
file(READ "${ORANGE_ENGINE_INSTALL_DIR}/lib/cmake/OrangeEngine/OrangeEngineTargets.cmake" _targets_text)
if (NOT _targets_text MATCHES "OrangeEngine::orange_engine")
    message(FATAL_ERROR
        "[install_smoke] OrangeEngineTargets.cmake 缺少 namespaced target。")
endif ()

# per-config 文件应当指向我们刚刚验证过的静态归档。
file(READ
    "${ORANGE_ENGINE_INSTALL_DIR}/lib/cmake/OrangeEngine/OrangeEngineTargets-${_cfg_lower}.cmake"
    _cfg_text)
if (NOT _cfg_text MATCHES "orange_engine")
    message(FATAL_ERROR
        "[install_smoke] OrangeEngineTargets-${_cfg_lower}.cmake 没有提到 orange_engine 归档。")
endif ()

message(STATUS "[install_smoke] OK — ${ORANGE_ENGINE_INSTALL_DIR}")
