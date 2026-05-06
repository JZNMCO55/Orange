# `find_package(OrangeEngine CONFIG)` smoke test。
#
# ctest 通过 `cmake -P config_smoke.cmake` 调用。延续 install_smoke 的
# 终点继续推进：
#   1. 把当前 build 安装到一个 scratch prefix；
#   2. 用一个零依赖的 from-zero 消费者工程 cmake configure，仅靠
#      `find_package(OrangeEngine CONFIG REQUIRED)` 拿到引擎；
#   3. build 该消费者，证明 imported target 真的把所有 PUBLIC + 链入
#      的 PRIVATE 依赖都带通了——也就是说 OrangeEngineConfig.cmake.in
#      里每条 find_dependency 都解析得到、namespaced target 也能正确
#      传播 OrangeRender / glm / nlohmann_json / glfw3 这条链。
#
# 由 ctest 包装层注入的 cache 变量：
#   ORANGE_ENGINE_BUILD_DIR           —— 安装源 build tree
#   ORANGE_ENGINE_INSTALL_DIR         —— scratch prefix；脚本会清空重建
#   ORANGE_ENGINE_CONSUMER_SRC_DIR    —— 消费者骨架工程目录
#   ORANGE_ENGINE_CONSUMER_BUILD_DIR  —— 消费者 build tree
#   ORANGE_ENGINE_CONFIG              —— 构建配置
#   ORANGE_ENGINE_GENERATOR           —— 转发给消费者 cmake 的 generator
#   ORANGE_ENGINE_*_DIR              —— 父 build 已经解析到的各依赖
#                                       config 路径，原样转发到消费
#                                       者，避免它从零开始再找一遍。

if (NOT DEFINED ORANGE_ENGINE_BUILD_DIR
        OR NOT DEFINED ORANGE_ENGINE_INSTALL_DIR
        OR NOT DEFINED ORANGE_ENGINE_CONSUMER_SRC_DIR
        OR NOT DEFINED ORANGE_ENGINE_CONSUMER_BUILD_DIR
        OR NOT DEFINED ORANGE_ENGINE_CONFIG
        OR NOT DEFINED ORANGE_ENGINE_GENERATOR)
    message(FATAL_ERROR
        "config_smoke.cmake: 必须指定 ORANGE_ENGINE_BUILD_DIR / "
        "ORANGE_ENGINE_INSTALL_DIR / ORANGE_ENGINE_CONSUMER_SRC_DIR / "
        "ORANGE_ENGINE_CONSUMER_BUILD_DIR / ORANGE_ENGINE_CONFIG / "
        "ORANGE_ENGINE_GENERATOR。")
endif ()

# ---- Step 1：安装 -------------------------------------------------------
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
        "[config_smoke] cmake --install 失败 (rc=${_install_rc}):\n${_install_log}")
endif ()

# 在把 prefix 交给消费者之前，做一次最小自检：包文件必须真的就绪。
foreach (_required IN ITEMS
        "lib/cmake/OrangeEngine/OrangeEngineConfig.cmake"
        "lib/cmake/OrangeEngine/OrangeEngineConfigVersion.cmake")
    if (NOT EXISTS "${ORANGE_ENGINE_INSTALL_DIR}/${_required}")
        message(FATAL_ERROR
            "[config_smoke] install 后仍缺：${_required}")
    endif ()
endforeach ()

# ---- Step 2：configure 消费者 -------------------------------------------
# CMAKE_PREFIX_PATH 同时指向新装的 OrangeEngine SDK 与 OrangeRender
# SDK 所在的 prefix（OrangeEngine 的 find_dependency(OrangeRender) 需
# 要后者）；具体 <dep>_DIR 也按父 build 实际解析的路径转发，避免消
# 费者重新搜索时因 vcpkg overlay / 自定义 layout 而找不到。
file(REMOVE_RECURSE "${ORANGE_ENGINE_CONSUMER_BUILD_DIR}")
file(MAKE_DIRECTORY "${ORANGE_ENGINE_CONSUMER_BUILD_DIR}")

set(_consumer_args
    "-S" "${ORANGE_ENGINE_CONSUMER_SRC_DIR}"
    "-B" "${ORANGE_ENGINE_CONSUMER_BUILD_DIR}"
    "-G" "${ORANGE_ENGINE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${ORANGE_ENGINE_INSTALL_DIR};${ORANGE_ENGINE_PARENT_PREFIX_PATH}"
)

foreach (_pair IN ITEMS
        "OrangeRender_DIR=${ORANGE_ENGINE_ORANGERENDER_DIR}"
        "glm_DIR=${ORANGE_ENGINE_GLM_DIR}"
        "nlohmann_json_DIR=${ORANGE_ENGINE_NLOHMANN_JSON_DIR}"
        "glfw3_DIR=${ORANGE_ENGINE_GLFW3_DIR}"
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
        "[config_smoke] 消费者 configure 失败 (rc=${_cfg_rc}):\n${_cfg_log}")
endif ()

# ---- Step 3：build 消费者 -----------------------------------------------
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
        "[config_smoke] 消费者 build 失败 (rc=${_build_rc}):\n${_build_log}")
endif ()

message(STATUS "[config_smoke] OK — ${ORANGE_ENGINE_CONSUMER_BUILD_DIR}")
