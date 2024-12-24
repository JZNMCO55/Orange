# Set Project Name
project(OrangeEngine)

# 搜索头文件和库路径
set(THIRDPARTY_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/include)
set(THIRDPARTY_LIB_DIR ${CMAKE_SOURCE_DIR}/3rdparty/lib)

# 搜索源文件
file(GLOB_RECURSE ORANGEENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 搜索头文件
file(GLOB_RECURSE ORANGEENGINE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

# 搜索依赖库
find_package(OpenGL REQUIRED)

# 添加库
if(BUILD_SHARED_LIBS)
    add_library(${PROJECT_NAME} SHARED ${ORANGEENGINE_SOURCES} ${ORANGEENGINE_HEADERS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DORANGE_EXPORTS)
else()         
    add_library(${PROJECT_NAME} STATIC ${ORANGEENGINE_SOURCES} ${ORANGEENGINE_HEADERS})
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DORANGE_STATIC)
endif()

set(ALL_SOURCE_FILE
    ${ORANGEENGINE_HEADERS}
    ${ORANGEENGINE_SOURCES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${ALL_SOURCE_FILE}
)

# 指定编译选项
target_compile_definitions(${PROJECT_NAME} PUBLIC -DPLATFORM_WINDOWS)
target_compile_definitions(${PROJECT_NAME} PUBLIC -DORANGE_ENABLE_ASSERTS)

# 指定头文件路径
target_include_directories(${PROJECT_NAME} PUBLIC
    ${THIRDPARTY_INCLUDE_DIR}
    ${THIRDPARTY_INCLUDE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenGL
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Event
)

# 链接静态库
target_link_directories(${PROJECT_NAME} PUBLIC ${THIRDPARTY_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE 
                        glfw3
                        glad
                        imgui
                        glm
                        ${OPENGL_LIBRARIES}
                     )
