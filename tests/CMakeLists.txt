# OrangeEngine 测试入口。仅在顶层 ORANGE_ENGINE_BUILD_TESTS=ON 时被
# add_subdirectory 进来；调用方已经 enable_testing() 了。
#
# 为了避免引入完整的测试框架（Catch2 / GoogleTest 等），用最小可用
# 的 `<cassert>` + 独立 main() 模式：每个 test 自己是一个可执行
# target，进程 exit code 0 即通过。这与 OrangeRender 的测试约定对齐。

include_guard(DIRECTORY)

# orange_engine_add_test(<target_name> <source.cpp>)
#
# 注册一个测试可执行体并通过 add_test 暴露给 ctest。target name 同
# 时是 ctest 的 case 名，方便 `ctest -R` 过滤。
function(orange_engine_add_test target_name source)
    add_executable(${target_name} ${source})
    target_link_libraries(${target_name} PRIVATE OrangeEngine::orange_engine)
    orange_engine_set_compiler_options(${target_name})
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

orange_engine_add_test(core_result_test         core/ResultTest.cpp)
orange_engine_add_test(core_handle_test         core/HandleTest.cpp)
orange_engine_add_test(core_serialization_test  core/SerializationTest.cpp)
