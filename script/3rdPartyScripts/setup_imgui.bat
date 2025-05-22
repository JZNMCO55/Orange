@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set IMGUI_DIR=%THIRDPARTY_DIR%\imgui

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" (
    set BUILD_TYPE=Release
)

:: 设置安装目录
set INSTALL_DIR=%BASE_DIR%\build\%BUILD_TYPE%

echo [INFO] Setting up ImGui library...

:: 克隆或更新 ImGui 仓库
if exist "%IMGUI_DIR%" (
    echo [INFO] Detected existing ImGui repository, attempting to update...
    cd /d "%IMGUI_DIR%"
    git fetch
    
    :: 检查当前分支是否为docking
    for /f "tokens=*" %%i in ('git branch --show-current') do set CURRENT_BRANCH=%%i
    if not "!CURRENT_BRANCH!"=="docking" (
        echo [INFO] Switching to docking branch...
        git checkout docking
        if errorlevel 1 (
            echo [ERROR] Failed to switch to docking branch.
            exit /b 1
        )
    )
    
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update ImGui repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning ImGui repository...
    git clone git@github.com:JZNMCO55/imgui.git "%IMGUI_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone ImGui repository.
        exit /b 1
    )
    
    :: 切换到docking分支
    cd /d "%IMGUI_DIR%"
    git checkout docking
    if errorlevel 1 (
        echo [ERROR] Failed to switch to docking branch.
        exit /b 1
    )
)

:: 创建构建目录
set BUILD_DIR=%IMGUI_DIR%\build_%BUILD_TYPE%
if not exist "%BUILD_DIR%" (
    echo [INFO] Creating build directory: %BUILD_DIR%
    mkdir "%BUILD_DIR%"
)

:: 创建安装目录
if not exist "%INSTALL_DIR%" (
    echo [INFO] Creating install directory: %INSTALL_DIR%
    mkdir "%INSTALL_DIR%"
)

:: ImGui本身没有CMake构建系统，我们需要创建一个
echo [INFO] Creating CMakeLists.txt for ImGui...
cd /d "%IMGUI_DIR%"

:: 检查是否已存在CMakeLists.txt，如果存在则删除
if exist CMakeLists.txt (
    del CMakeLists.txt
)

:: 重新创建CMakeLists.txt
echo cmake_minimum_required^(VERSION 3.10^) > CMakeLists.txt
echo project^(ImGui^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo # 添加查找依赖包 >> CMakeLists.txt
echo find_package^(Vulkan QUIET^) >> CMakeLists.txt
echo find_path^(GLFW_INCLUDE_DIR GLFW/glfw3.h HINTS ${CMAKE_INSTALL_PREFIX}/include^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo option^(IMGUI_VULKAN_SUPPORT "Build with Vulkan support" ON^) >> CMakeLists.txt
echo option^(IMGUI_DX12_SUPPORT "Build with DirectX12 support" ON^) >> CMakeLists.txt
echo option^(IMGUI_GLFW_SUPPORT "Build with GLFW support" ON^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo set^(IMGUI_SOURCES >> CMakeLists.txt
echo     imgui.cpp >> CMakeLists.txt
echo     imgui_demo.cpp >> CMakeLists.txt
echo     imgui_draw.cpp >> CMakeLists.txt
echo     imgui_tables.cpp >> CMakeLists.txt
echo     imgui_widgets.cpp >> CMakeLists.txt
echo ^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo set^(IMGUI_HEADERS >> CMakeLists.txt
echo     imgui.h >> CMakeLists.txt
echo     imconfig.h >> CMakeLists.txt
echo     imgui_internal.h >> CMakeLists.txt
echo     imstb_rectpack.h >> CMakeLists.txt
echo     imstb_textedit.h >> CMakeLists.txt
echo     imstb_truetype.h >> CMakeLists.txt
echo ^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo # Vulkan支持 >> CMakeLists.txt
echo if^(IMGUI_VULKAN_SUPPORT^) >> CMakeLists.txt
echo     if^(Vulkan_FOUND^) >> CMakeLists.txt
echo         list^(APPEND IMGUI_SOURCES backends/imgui_impl_vulkan.cpp^) >> CMakeLists.txt
echo         list^(APPEND IMGUI_HEADERS backends/imgui_impl_vulkan.h^) >> CMakeLists.txt
echo         message^(STATUS "Vulkan found - enabling ImGui Vulkan support"^) >> CMakeLists.txt
echo     else^(^) >> CMakeLists.txt
echo         message^(WARNING "Vulkan not found - disabling ImGui Vulkan support"^) >> CMakeLists.txt
echo         set^(IMGUI_VULKAN_SUPPORT OFF^) >> CMakeLists.txt
echo     endif^(^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo # DirectX12支持 >> CMakeLists.txt
echo if^(IMGUI_DX12_SUPPORT^) >> CMakeLists.txt
echo     if^(WIN32^) >> CMakeLists.txt
echo         list^(APPEND IMGUI_SOURCES backends/imgui_impl_dx12.cpp^) >> CMakeLists.txt
echo         list^(APPEND IMGUI_HEADERS backends/imgui_impl_dx12.h^) >> CMakeLists.txt
echo         message^(STATUS "Windows platform - enabling ImGui DirectX12 support"^) >> CMakeLists.txt
echo     else^(^) >> CMakeLists.txt
echo         message^(WARNING "Non-Windows platform - disabling ImGui DirectX12 support"^) >> CMakeLists.txt
echo         set^(IMGUI_DX12_SUPPORT OFF^) >> CMakeLists.txt
echo     endif^(^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo # GLFW支持 >> CMakeLists.txt
echo if^(IMGUI_GLFW_SUPPORT^) >> CMakeLists.txt
echo     if^(GLFW_INCLUDE_DIR^) >> CMakeLists.txt
echo         list^(APPEND IMGUI_SOURCES backends/imgui_impl_glfw.cpp^) >> CMakeLists.txt
echo         list^(APPEND IMGUI_HEADERS backends/imgui_impl_glfw.h^) >> CMakeLists.txt
echo         message^(STATUS "GLFW found - enabling ImGui GLFW support"^) >> CMakeLists.txt
echo     else^(^) >> CMakeLists.txt
echo         message^(WARNING "GLFW not found - disabling ImGui GLFW support"^) >> CMakeLists.txt
echo         set^(IMGUI_GLFW_SUPPORT OFF^) >> CMakeLists.txt
echo     endif^(^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo add_library^(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_HEADERS}^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo target_include_directories^(imgui >> CMakeLists.txt
echo     PUBLIC >> CMakeLists.txt
echo     ${CMAKE_CURRENT_SOURCE_DIR} >> CMakeLists.txt
echo     ${CMAKE_CURRENT_SOURCE_DIR}/backends >> CMakeLists.txt
echo ^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo # 添加依赖库的包含目录 >> CMakeLists.txt
echo if^(IMGUI_VULKAN_SUPPORT AND Vulkan_FOUND^) >> CMakeLists.txt
echo     target_include_directories^(imgui PRIVATE ${Vulkan_INCLUDE_DIRS}^) >> CMakeLists.txt
echo     target_link_libraries^(imgui PRIVATE ${Vulkan_LIBRARIES}^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo if^(IMGUI_GLFW_SUPPORT AND GLFW_INCLUDE_DIR^) >> CMakeLists.txt
echo     target_include_directories^(imgui PRIVATE ${GLFW_INCLUDE_DIR}^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo install^(TARGETS imgui >> CMakeLists.txt
echo     ARCHIVE DESTINATION lib >> CMakeLists.txt
echo     LIBRARY DESTINATION lib >> CMakeLists.txt
echo     RUNTIME DESTINATION bin >> CMakeLists.txt
echo ^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo install^(FILES ${IMGUI_HEADERS} DESTINATION include/imgui^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo # 安装后端头文件 >> CMakeLists.txt
echo if^(IMGUI_VULKAN_SUPPORT AND Vulkan_FOUND^) >> CMakeLists.txt
echo     install^(FILES backends/imgui_impl_vulkan.h DESTINATION include/imgui/backends^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo if^(IMGUI_DX12_SUPPORT AND WIN32^) >> CMakeLists.txt
echo     install^(FILES backends/imgui_impl_dx12.h DESTINATION include/imgui/backends^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt
echo. >> CMakeLists.txt
echo if^(IMGUI_GLFW_SUPPORT AND GLFW_INCLUDE_DIR^) >> CMakeLists.txt
echo     install^(FILES backends/imgui_impl_glfw.h DESTINATION include/imgui/backends^) >> CMakeLists.txt
echo endif^(^) >> CMakeLists.txt

:: 配置ImGui
cd /d "%BUILD_DIR%"

:: 确保GLFW和Vulkan已安装
echo [INFO] Checking for dependencies...
if not exist "%INSTALL_DIR%\include\GLFW\glfw3.h" (
    echo [WARNING] GLFW headers not found at %INSTALL_DIR%\include\GLFW
    echo [WARNING] Please run setup_glfw.bat first
)

:: CMake 配置
echo [INFO] Configuring ImGui...
cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
         -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ^
         -DCMAKE_PREFIX_PATH=%INSTALL_DIR% ^
         -DIMGUI_VULKAN_SUPPORT=ON ^
         -DIMGUI_DX12_SUPPORT=ON ^
         -DIMGUI_GLFW_SUPPORT=ON ^
         -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake generation failed.
    exit /b 1
)

:: 构建
echo [INFO] Building ImGui...
cmake --build . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

:: 安装
echo [INFO] Installing ImGui...
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed.
    exit /b 1
)

echo [INFO] ImGui setup completed successfully.
echo [INFO] Library path: %INSTALL_DIR%\lib
echo [INFO] Include path: %INSTALL_DIR%\include\imgui
endlocal
