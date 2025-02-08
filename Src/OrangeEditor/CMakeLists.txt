# 定义 OrangeEditor 项目
project(OrangeEditor)

# 搜索 OrangeEngine 动态库
set(ORANGEENGINE_LIB_DIR ${CMAKE_BINARY_DIR}/lib)
# 设置资源文件的源目录和目标目录
set(RESOURCE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/Resource)
set(RESOURCE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/Resource)

# 搜索源文件
file(GLOB_RECURSE OrangeEditor_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 搜索头文件
file(GLOB_RECURSE OrangeEditor_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

set(ALL_SOURCE_FILE
    ${OrangeEditor_HEADERS}
    ${OrangeEditor_SOURCES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${ALL_SOURCE_FILE}
)

# 添加可执行文件
add_executable(${PROJECT_NAME} ${OrangeEditor_SOURCES} ${OrangeEditor_HEADERS})

# 链接 OrangeEngine 的头文件和库
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_SOURCE_DIR}/Src/OrangeEngine
    ${CMAKE_CURRENT_SOURCE_DIR}/Src
)
target_link_directories(${PROJECT_NAME} PUBLIC ${ORANGEENGINE_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC OrangeEngine)

# 将 Game 设置为 Visual Studio 启动项目
set_target_properties(${PROJECT_NAME} PROPERTIES VS_STARTUP_PROJECT YES)