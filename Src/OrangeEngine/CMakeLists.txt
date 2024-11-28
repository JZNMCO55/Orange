# 定义 OrangeEngine 项目
project(OrangeEngine)

# 搜索头文件和库路径
set(THIRDPARTY_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/include)
set(THIRDPARTY_LIB_DIR ${CMAKE_SOURCE_DIR}/3rdparty/lib)

# 搜索源文件
file(GLOB_RECURSE ORANGEENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 搜索头文件
file(GLOB_RECURSE ORANGEENGINE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

# 创建动态库目标
add_library(${PROJECT_NAME} SHARED ${ORANGEENGINE_SOURCES} ${ORANGEENGINE_HEADERS} "Engine/Log.h" "Engine/Log.cpp")

# 指定头文件路径
target_include_directories(${PROJECT_NAME} PUBLIC ${THIRDPARTY_INCLUDE_DIR})

# 链接静态库
target_link_directories(${PROJECT_NAME} PUBLIC ${THIRDPARTY_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE)

target_compile_definitions(${PROJECT_NAME} PUBLIC -DPLATFORM_WINDOWS)

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
