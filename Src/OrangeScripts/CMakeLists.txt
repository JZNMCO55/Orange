Project(OrangeScript)

# 1. 定义C#项目变量（不创建新project）
set(CSHARP_PROJECT_NAME "OrangeScript")
set(CSHARP_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CSHARP_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Scripts")

# 2. 查找源文件
file(GLOB_RECURSE CSHARP_SOURCES
    "${CSHARP_PROJECT_DIR}/Source/*.cs"
)

# 3. 添加自定义构建目标
add_custom_target(${CSHARP_PROJECT_NAME} ALL
    COMMAND dotnet build "${CSHARP_PROJECT_DIR}/${CSHARP_PROJECT_NAME}.csproj" 
        -c Release 
        -o "${CSHARP_OUTPUT_DIR}"
    WORKING_DIRECTORY "${CSHARP_PROJECT_DIR}"
    DEPENDS ${CSHARP_SOURCES}
    SOURCES ${CSHARP_SOURCES}  # 让VS显示文件
    COMMENT "Building ${CSHARP_PROJECT_NAME}..."
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${CSHARP_SOURCES}
)