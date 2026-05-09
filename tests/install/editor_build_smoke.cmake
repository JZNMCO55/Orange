# OrangeEditor build smoke test —— Phase 6 / Task 06-01 验证。
#
# 与 config_smoke.cmake 同模式：
#   1. 把当前 build 安装到一个 scratch prefix；
#   2. 用 `tools/OrangeEditor` 这个独立 CMake 工程做消费者，仅靠
#      `find_package(OrangeEngine CONFIG REQUIRED)` 拿到引擎；
#   3. configure + build OrangeEditor，证明它能从零搭起来。
#
# 与 config_smoke 的区别仅是消费者 src 路径指向 `tools/OrangeEditor`
# 而非 `tests/install/config_smoke_consumer`。所有 install + parent
# prefix 转发逻辑沿用 config_smoke 的语义。
#
# 不 run 编辑器 —— CI 一般没有 GPU + display；仅证明 link / configure
# 链通即可。
#
# 由 ctest 包装层注入的 cache 变量（与 config_smoke.cmake 同名）：
#   ORANGE_ENGINE_BUILD_DIR           —— 安装源 build tree
#   ORANGE_ENGINE_INSTALL_DIR         —— scratch prefix
#   ORANGE_ENGINE_CONSUMER_SRC_DIR    —— tools/OrangeEditor 源目录
#   ORANGE_ENGINE_CONSUMER_BUILD_DIR  —— 编辑器 build tree
#   ORANGE_ENGINE_CONFIG              —— 构建配置
#   ORANGE_ENGINE_GENERATOR           —— 转发给消费者的 CMake generator
#   ORANGE_ENGINE_*_DIR              —— 父 build 已解析的依赖 config 路径

if (NOT DEFINED ORANGE_ENGINE_BUILD_DIR
        OR NOT DEFINED ORANGE_ENGINE_INSTALL_DIR
        OR NOT DEFINED ORANGE_ENGINE_CONSUMER_SRC_DIR
        OR NOT DEFINED ORANGE_ENGINE_CONSUMER_BUILD_DIR
        OR NOT DEFINED ORANGE_ENGINE_CONFIG
        OR NOT DEFINED ORANGE_ENGINE_GENERATOR)
    message(FATAL_ERROR
        "editor_build_smoke.cmake: 必须指定 ORANGE_ENGINE_BUILD_DIR / "
        "ORANGE_ENGINE_INSTALL_DIR / ORANGE_ENGINE_CONSUMER_SRC_DIR / "
        "ORANGE_ENGINE_CONSUMER_BUILD_DIR / ORANGE_ENGINE_CONFIG / "
        "ORANGE_ENGINE_GENERATOR。")
endif ()

# ---- Step 1：安装引擎 ----------------------------------------------------
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
        "[editor_build_smoke] cmake --install 失败 (rc=${_install_rc}):\n${_install_log}")
endif ()

# ---- Step 2：configure tools/OrangeEditor -------------------------------
file(REMOVE_RECURSE "${ORANGE_ENGINE_CONSUMER_BUILD_DIR}")
file(MAKE_DIRECTORY "${ORANGE_ENGINE_CONSUMER_BUILD_DIR}")

set(_consumer_args
    "-S" "${ORANGE_ENGINE_CONSUMER_SRC_DIR}"
    "-B" "${ORANGE_ENGINE_CONSUMER_BUILD_DIR}"
    "-G" "${ORANGE_ENGINE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${ORANGE_ENGINE_INSTALL_DIR};${ORANGE_ENGINE_PARENT_PREFIX_PATH}"
)

# 把父 build 解析到的依赖 config 路径整套转发给编辑器，避免它从零
# 搜索一遍。Vulkan 走 VULKAN_SDK 自解析。
foreach (_pair IN ITEMS
        "OrangeRender_DIR=${ORANGE_ENGINE_ORANGERENDER_DIR}"
        "glm_DIR=${ORANGE_ENGINE_GLM_DIR}"
        "EnTT_DIR=${ORANGE_ENGINE_ENTT_DIR}"
        "nlohmann_json_DIR=${ORANGE_ENGINE_NLOHMANN_JSON_DIR}"
        "glfw3_DIR=${ORANGE_ENGINE_GLFW3_DIR}"
        "box2d_DIR=${ORANGE_ENGINE_BOX2D_DIR}"
        "Vulkan_INCLUDE_DIR=${ORANGE_ENGINE_VULKAN_INCLUDE_DIR}"
        "Vulkan_LIBRARY=${ORANGE_ENGINE_VULKAN_LIBRARY}"
        "volk_DIR=${ORANGE_ENGINE_VOLK_DIR}"
        "VulkanMemoryAllocator_DIR=${ORANGE_ENGINE_VMA_DIR}")
    string(REGEX MATCH "^([^=]+)=(.*)$" _ "${_pair}")
    set(_var "${CMAKE_MATCH_1}")
    set(_val "${CMAKE_MATCH_2}")
    if (NOT _val STREQUAL "")
        list(APPEND _consumer_args "-D${_var}=${_val}")
    endif ()
endforeach ()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_consumer_args}
    RESULT_VARIABLE _cfg_rc
    OUTPUT_VARIABLE _cfg_log
    ERROR_VARIABLE  _cfg_log
)
if (NOT _cfg_rc EQUAL 0)
    message(FATAL_ERROR
        "[editor_build_smoke] OrangeEditor configure 失败 (rc=${_cfg_rc}):\n${_cfg_log}")
endif ()

# ---- Step 3：build OrangeEditor -----------------------------------------
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        --build  "${ORANGE_ENGINE_CONSUMER_BUILD_DIR}"
        --config "${ORANGE_ENGINE_CONFIG}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_log
    ERROR_VARIABLE  _build_log
)
if (NOT _build_rc EQUAL 0)
    message(FATAL_ERROR
        "[editor_build_smoke] OrangeEditor build 失败 (rc=${_build_rc}):\n${_build_log}")
endif ()

message(STATUS "[editor_build_smoke] OK — ${ORANGE_ENGINE_CONSUMER_BUILD_DIR}")
