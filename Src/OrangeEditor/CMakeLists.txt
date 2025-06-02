cmake_minimum_required(VERSION 3.20)
project(OrangeEditor VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/3rdparty/include
)

# Source files
file(GLOB_RECURSE SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

# Create executable
add_executable(OrangeEditor ${SOURCES})

# Target properties
set_target_properties(OrangeEditor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)

# Link libraries
target_link_libraries(OrangeEditor
    PRIVATE
    OrangeEngine
)

# Install rules
install(TARGETS OrangeEditor
    RUNTIME DESTINATION bin
)

# Copy resource files to output directory
add_custom_command(TARGET OrangeEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources
    $<TARGET_FILE_DIR:OrangeEditor>/Resources
    COMMENT "Copying resource files to output directory"
)

# Compile definitions
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(OrangeEditor PUBLIC -DORANGE_RELEASE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(OrangeEditor PUBLIC -DORANGE_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Dist")
    target_compile_definitions(OrangeEditor PUBLIC -DORANGE_DIST)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${SOURCES}
)

if(MSVC)
    set_target_properties(OrangeEditor PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT OrangeEditor)
endif()