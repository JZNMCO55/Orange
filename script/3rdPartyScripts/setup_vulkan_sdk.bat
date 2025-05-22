@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set INSTALL_DIR=%BASE_DIR%\build\%1
if "%1"=="" set INSTALL_DIR=%BASE_DIR%\build\Release

echo [INFO] Setting up Vulkan SDK...

:: 检查环境变量VULKAN_SDK是否存在
if defined VULKAN_SDK (
    echo [INFO] Found Vulkan SDK at %VULKAN_SDK%
    
    :: 检查目录是否存在
    if exist "%VULKAN_SDK%" (
        echo [INFO] Vulkan SDK directory exists
    ) else (
        echo [ERROR] Vulkan SDK directory does not exist: %VULKAN_SDK%
        echo [ERROR] Please install Vulkan SDK or correct the VULKAN_SDK environment variable
        goto VulkanInstallInstructions
    )
    
    :: 检查关键文件是否存在
    if exist "%VULKAN_SDK%\Include\vulkan\vulkan.h" (
        echo [INFO] Vulkan headers found
    ) else (
        echo [ERROR] Vulkan headers not found at %VULKAN_SDK%\Include\vulkan\vulkan.h
        echo [ERROR] Your Vulkan SDK installation may be incomplete
        goto VulkanInstallInstructions
    )
    
    :: 创建符号链接或复制文件
    echo [INFO] Creating Vulkan include directory in %INSTALL_DIR%\include
    
    if not exist "%INSTALL_DIR%\include" (
        mkdir "%INSTALL_DIR%\include"
    )
    
    if not exist "%INSTALL_DIR%\include\vulkan" (
        mkdir "%INSTALL_DIR%\include\vulkan"
    )
    
    echo [INFO] Copying vulkan.h to %INSTALL_DIR%\include\vulkan
    copy "%VULKAN_SDK%\Include\vulkan\vulkan.h" "%INSTALL_DIR%\include\vulkan\" > nul
    
    if not exist "%INSTALL_DIR%\lib" (
        mkdir "%INSTALL_DIR%\lib"
    )
    
    echo [INFO] Copying Vulkan libraries to %INSTALL_DIR%\lib
    copy "%VULKAN_SDK%\Lib\vulkan-1.lib" "%INSTALL_DIR%\lib\" > nul
    
    :: 设置环境变量
    echo [INFO] Setting environment variables...
    setx VULKAN_SDK "%VULKAN_SDK%" > nul
    
    echo [INFO] Vulkan SDK setup completed successfully.
    echo [INFO] SDK Path: %VULKAN_SDK%
    echo [INFO] Include path: %INSTALL_DIR%\include\vulkan
    echo [INFO] Library path: %INSTALL_DIR%\lib
    
    goto End
) else (
    echo [ERROR] Vulkan SDK not found. VULKAN_SDK environment variable is not set.
    goto VulkanInstallInstructions
)

:VulkanInstallInstructions
echo.
echo [INFO] Please install Vulkan SDK from: https://vulkan.lunarg.com/sdk/home
echo [INFO] After installation, set the VULKAN_SDK environment variable to the installation directory.
echo [INFO] Example: setx VULKAN_SDK "C:\VulkanSDK\1.3.239.0"
echo [INFO] Then run this script again.
exit /b 1

:End
endlocal 