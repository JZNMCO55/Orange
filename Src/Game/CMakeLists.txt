# 定义 Game 项目
project(Game)

# 搜索 OrangeEngine 动态库
set(ORANGEENGINE_LIB_DIR ${CMAKE_BINARY_DIR}/lib)

# 指定源文件
file(GLOB_RECURSE GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 添加可执行文件
add_executable(${PROJECT_NAME} ${GAME_SOURCES})

# 链接 OrangeEngine 的头文件和库
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/Src/OrangeEngine)
target_link_directories(${PROJECT_NAME} PUBLIC ${ORANGEENGINE_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC OrangeEngine)
