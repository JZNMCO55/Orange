# 定义 Game 项目
project(Game)

# 搜索 OrangeEngine 动态库
set(ORANGEENGINE_LIB_DIR ${CMAKE_BINARY_DIR}/lib)
# 设置资源文件的源目录和目标目录
set(RESOURCE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/Resource)
set(RESOURCE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/Resource)

# 指定源文件
file(GLOB_RECURSE GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 添加可执行文件
add_executable(${PROJECT_NAME} ${GAME_SOURCES})

# 链接 OrangeEngine 的头文件和库
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/Src/OrangeEngine)
target_link_directories(${PROJECT_NAME} PUBLIC ${ORANGEENGINE_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC OrangeEngine)

# 添加自定义命令以复制资源文件
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${RESOURCE_SOURCE_DIR} ${RESOURCE_OUTPUT_DIR}
)