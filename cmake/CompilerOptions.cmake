# 提供 orange_engine_set_compiler_options(<target>)，用于给目标统一
# 应用项目级编译警告与语言一致性 flag。命名沿用 OrangeRender 的
# orange_set_* 习惯，但前缀分开（orange_engine_set_*），让两个库可以
# 在同一个 build 里并存而不冲突。

option(ORANGE_ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

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
