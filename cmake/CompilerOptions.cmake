# Provides orange_engine_set_compiler_options(<target>) to apply project-wide
# compiler warnings and language conformance flags. Mirrors the convention
# established by OrangeRender's cmake/CompilerOptions.cmake (orange_set_*),
# kept as a separate prefix so the two libraries can coexist in one build.

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
