# 设置项目名称
project(OrangeEngine)

# 设置 CMake 模块路径
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/install/${CMAKE_BUILD_TYPE}")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE}")

# 搜索头文件和库路径
set(THIRDPARTY_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty)
set(THIRDPARTY_LIB_DIR ${CMAKE_SOURCE_DIR}/3rdparty/lib)
set(BUILD_INSTALL_DIR ${CMAKE_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE})

# Shaderc 库路径配置 - 使用安装目录中的库
set(SHADERC_INCLUDE_DIR ${BUILD_INSTALL_DIR}/include)
set(SHADERC_LIB_DIR ${BUILD_INSTALL_DIR}/lib)

# 创建 Shaderc 导入目标 - 使用组合静态库
if(NOT TARGET shaderc::shaderc)
    add_library(shaderc::shaderc STATIC IMPORTED)
    set_target_properties(shaderc::shaderc PROPERTIES
        IMPORTED_LOCATION "${SHADERC_LIB_DIR}/shaderc_combined.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${SHADERC_INCLUDE_DIR}"
    )
endif()

# 搜索源文件
file(GLOB_RECURSE ORANGEENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 搜索头文件
file(GLOB_RECURSE ORANGEENGINE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    
set(ALL_SOURCE_FILE
    ${ORANGEENGINE_HEADERS}
    ${ORANGEENGINE_SOURCES}
)

add_library(${PROJECT_NAME} STATIC ${ORANGEENGINE_SOURCES} ${ORANGEENGINE_HEADERS}) 

# 编译版本
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DORANGE_RELEASE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DORANGE_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Dist")
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DORANGE_DIST)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${ALL_SOURCE_FILE}
)

# 使用相对路径定义include目录
set(ORANGE_INCLUDE_DIRS 
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Layer1"
    "${CMAKE_CURRENT_SOURCE_DIR}/Layer1/Core/UnitTesting"
)

# 查找依赖库
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(spdlog REQUIRED)
find_package(glfw3 3.5 REQUIRED)
# 检查Vulkan支持
find_package(Vulkan REQUIRED)

# 设置编译选项
if(Vulkan_FOUND)
    message(STATUS "找到Vulkan SDK: ${Vulkan_VERSION}")
else()
    message(WARNING "未找到Vulkan SDK，Vulkan后端将被禁用")
endif()

# 设置包含目录
target_include_directories(${PROJECT_NAME}
    # 公共头文件目录
    PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    
    # 私有头文件目录
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Layer1/Core/UnitTesting
    ${CMAKE_CURRENT_SOURCE_DIR}/Layer1
    ${THIRDPARTY_INCLUDE_DIR}/fmt/include
    ${THIRDPARTY_INCLUDE_DIR}/spdlog/include
    ${THIRDPARTY_INCLUDE_DIR}/glm/include
    ${THIRDPARTY_INCLUDE_DIR}/glfw/include
    ${Vulkan_INCLUDE_DIRS}
    ${BUILD_INSTALL_DIR}/include
)

target_link_directories(${PROJECT_NAME}
    PRIVATE
    ${BUILD_INSTALL_DIR}/lib
)

# 链接库 - 使用静态库，保持 PRIVATE 封装性
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    fmt::fmt
    spdlog::spdlog
    glfw
    shaderc::shaderc
    ${Vulkan_LIBRARIES}
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    EXPORT OrangeEngineTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# 导出依赖关系
install(EXPORT OrangeEngineTargets
    NAMESPACE OrangeEngine::
    FILE OrangeEngineTargets.cmake
    DESTINATION lib/cmake/OrangeEngine
)

# 安装头文件
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/${PROJECT_NAME}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    PATTERN "Layer1/*" PATTERN "Layer2/*"
)