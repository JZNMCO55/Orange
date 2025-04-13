# 定义 OrangeEditor 项目
project(OrangeEditor)

# 搜索 OrangeEngine 动态库
set(ORANGEENGINE_LIB_DIR ${CMAKE_BINARY_DIR}/lib)

# 定义资源目录
set(RESOURCE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/Resource)
set(RESOURCE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/Resource)  # 构建目录下的资源
set(RUNTIME_RESOURCE_DIR $<TARGET_FILE_DIR:${PROJECT_NAME}>/../../Resource)  # 可执行文件目录下的资源


set(DLLS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin)

# 第三方库路径
set(THIRDPARTY_LIB_DIR ${CMAKE_SOURCE_DIR}/3rdparty/lib)

set(MONOLIB_SOURCE_DIR ${THIRDPARTY_LIB_DIR}/Mono/4.5)
set(MONOLIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/lib/mono/4.5)  # 构建目录下的资源
set(RUNTIME_MONOLIB_DIR $<TARGET_FILE_DIR:${PROJECT_NAME}>/../lib/mono/4.5)  # 可执行文件目录下的资源

# 定义需要复制的 DLL
file(GLOB_RECURSE THIRDPARTY_DLLS 
    "${THIRDPARTY_LIB_DIR}/*.dll"              # 常规第三方 DLL
    "${THIRDPARTY_LIB_DIR}/Mono/*.dll"         # Mono 相关 DLL  
    "${THIRDPARTY_LIB_DIR}/Vulkan/*.dll"       # Vulkan 相关 DLL
)

# 定义 C# DLL 路径
set(CSHARP_DLL "${CMAKE_BINARY_DIR}/Scripts/OrangeScript.dll")

# 搜索源文件和头文件
file(GLOB_RECURSE OrangeEditor_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE OrangeEditor_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

# 组织源代码树视图
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES 
    ${OrangeEditor_HEADERS}
    ${OrangeEditor_SOURCES}
)

# 创建可执行文件
add_executable(${PROJECT_NAME} 
    ${OrangeEditor_SOURCES}
    ${OrangeEditor_HEADERS}
)

# 链接配置
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_SOURCE_DIR}/Src/OrangeEngine
    ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

target_link_directories(${PROJECT_NAME} PUBLIC 
    ${ORANGEENGINE_LIB_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC 
    OrangeEngine
)

# 复制资源文件
# 以后合并构建目录和可执行文件目录
add_custom_command(
    # copy resource files
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${RESOURCE_SOURCE_DIR} 
    ${RESOURCE_OUTPUT_DIR} 
        COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${RESOURCE_SOURCE_DIR} 
    ${RUNTIME_RESOURCE_DIR}

    # make dlls directory
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${DLLS_OUTPUT_DIR}
    # copy dlls
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${THIRDPARTY_DLLS}
    $<TARGET_FILE_DIR:${PROJECT_NAME}> 
    
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${THIRDPARTY_DLLS}
    ${DLLS_OUTPUT_DIR}

    # copy csharp dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CSHARP_DLL}
    $<TARGET_FILE_DIR:${PROJECT_NAME}>

    # copy mono dlls
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${MONOLIB_SOURCE_DIR}
    ${MONOLIB_OUTPUT_DIR}

    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${MONOLIB_SOURCE_DIR}
    ${RUNTIME_MONOLIB_DIR}
)

# 设为启动项目
set_target_properties(${PROJECT_NAME} PROPERTIES 
    VS_STARTUP_PROJECT YES
)