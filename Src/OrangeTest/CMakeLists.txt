# 设置项目名称
project(OrangeTest)

# 添加 Google Test 的路径
find_package(GTest REQUIRED)
set(GTEST_DIR ${CMAKE_SOURCE_DIR}/3rdparty/googletest)
set(GTEST_INCLUDE_DIR ${GTEST_DIR}/googletest/include)
set(GTEST_LIB_DIR ${GTEST_DIR}/build/lib/Release)

# 添加 Google Test 的头文件路径
include_directories(${GTEST_INCLUDE_DIR})

# 添加 Google Test 的库路径
link_directories(${GTEST_LIB_DIR})

file(GLOB_RECURSE ORANGETEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 添加测试源文件
add_executable(OrangeTest
${ORANGETEST_SOURCES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${ORANGETEST_SOURCES}
)

# 链接库 - 只需要链接OrangeEngine和GTest
target_link_libraries(OrangeTest
    GTest::GTest
    OrangeEngine
)

# 启用测试
enable_testing()
add_test(NAME OrangeTest COMMAND OrangeTest)

