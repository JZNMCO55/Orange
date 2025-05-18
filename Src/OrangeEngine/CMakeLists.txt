# 设置项目名称
project(OrangeEngine)

# 搜索头文件和库路径
set(THIRDPARTY_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/include)
set(THIRDPARTY_LIB_DIR ${CMAKE_SOURCE_DIR} /3rdparty/lib)

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

Set(ORANGE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Layer1/
    ${CMAKE_CURRENT_SOURCE_DIR}/Layer1/Core/UnitTesting
)

# 指定头文件路径
target_include_directories(${PROJECT_NAME}
    PUBLIC   # 公开给依赖项目
    ${ORANGE_INCLUDE_DIR}
    PRIVATE  # 仅内部使用
    ${THIRDPARTY_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 根据构建类型选择链接的库
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(3rdLibs 

    )
else()
    set(3rdLibs 

        )
endif()

# 链接静态库
target_link_directories(${PROJECT_NAME} PRIVATE 
                        ${THIRDPARTY_LIB_DIR}
                        )
target_link_libraries(${PROJECT_NAME} PRIVATE
                        ${3rdLibs}
)

# 确保静态库完全封装符号
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET hidden          # GCC/Clang
    VISIBILITY_INLINES_HIDDEN ON
    INTERFACE_LINK_LIBRARIES ""  # 清空接口依赖
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/EXPORT:NONE")  # Windows特殊处理
endif()