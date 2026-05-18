# 提供 orange_engine_set_compiler_options(<target>)，用于给目标统一
# 应用项目级编译警告与语言一致性 flag。命名沿用 OrangeRender 的
# orange_set_* 习惯，但前缀分开（orange_engine_set_*），让两个库可以
# 在同一个 build 里并存而不冲突。

option(ORANGE_ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# AddressSanitizer 全局开关（MSVC 专用，目前仅用于 14_pbr_ibl --furnace
# segfault 续二分这种"抓堆破坏 / use-after-free / OOB"场景；非诊断模
# 式下永远 OFF）。启用时给所有 OrangeEngine 自家 build 出的 target 注入
# /fsanitize=address；OrangeRender 是 find_package 进来的预编译 .lib，
# 不被 instrument——但 process 级 asan_dynamic.dll 会接管 malloc/free，
# 非 instrumented 模块也能在 UAF 时被抓住（只是相邻 buffer 没 redzone，
# OOB 漏抓）。
#
# /RTC1 与 /fsanitize=address 互斥，MSVC 默认 Debug 配置注入 /RTC1，启
# 用时把所有配置的 CXX_FLAGS_* 里的 /RTC* 剥掉。link 阶段强制
# /INCREMENTAL:NO（ASan 不兼容增量链接）。
option(ORANGE_ENGINE_WITH_ASAN
    "Build with MSVC AddressSanitizer (/fsanitize=address) for diagnostics"
    OFF)

if (ORANGE_ENGINE_WITH_ASAN)
    if (NOT MSVC)
        message(FATAL_ERROR
            "ORANGE_ENGINE_WITH_ASAN 目前只支持 MSVC（/fsanitize=address）。"
            "在 clang / gcc 工具链上请关掉本 option 或换 -fsanitize=address 显式注入。")
    endif ()
    message(STATUS "OrangeEngine: AddressSanitizer enabled. "
                   "Stripping /RTC* from CXX_FLAGS_* and forcing /INCREMENTAL:NO. "
                   "std::vector/string annotation disabled to match prebuilt OrangeRender.lib.")
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO)
    # ASan 启用时 MSVC 默认给 std::vector / std::basic_string 加 redzone
    # 标注（annotate_vector=1 / annotate_string=1，msvc-link directive）。
    # OrangeRender 预编译 .lib 是 non-ASan build，标注为 0，混链时 LNK1319。
    # 这两个宏让本仓 obj 与 OR 一致；代价：丢 std::vector/string 的 OOB
    # redzone 抓取（heap UAF / heap OOB / stack OOB 仍照常工作）。
    add_compile_definitions(
        _DISABLE_VECTOR_ANNOTATION=1
        _DISABLE_STRING_ANNOTATION=1
    )
    foreach (_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        string(REGEX REPLACE "/RTC[1csu]+" ""
            CMAKE_CXX_FLAGS_${_cfg} "${CMAKE_CXX_FLAGS_${_cfg}}")
        string(REGEX REPLACE "/RTC[1csu]+" ""
            CMAKE_C_FLAGS_${_cfg} "${CMAKE_C_FLAGS_${_cfg}}")
    endforeach ()
endif ()

function(orange_engine_set_compiler_options target)
    if (MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /utf-8
            /permissive-
            /Zc:preprocessor
        )
        if (ORANGE_ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif ()
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        if (ORANGE_ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif ()
    endif ()
endfunction()
