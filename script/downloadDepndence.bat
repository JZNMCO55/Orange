@echo off
setlocal enabledelayedexpansion

set CONFIGURATION=Release
if "%1"=="Debug" set CONFIGURATION=Debug
if "%1"=="Release" set CONFIGURATION=Release

:: 设置根目录
set BASE_DIR=%~dp0..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty

echo [INFO] Root Directory: %BASE_DIR%
echo [INFO] Third Party Directory: %THIRDPARTY_DIR%

:: 检查并创建3rdparty目录（如果不存在）
if not exist "%THIRDPARTY_DIR%" (
    echo [INFO] Creating third-party library directory: %THIRDPARTY_DIR%
    mkdir "%THIRDPARTY_DIR%"
)

:: 定义三方库列表
set LIBRARIES=googletest fmt glm spdlog

:: 遍历每个库
for %%L in (%LIBRARIES%) do (
    set "LIB_DIR=%THIRDPARTY_DIR%\%%L"

    echo [INFO] Checking library: %%L
    echo [INFO] Library directory: !LIB_DIR!

    :: 如果库目录存在且已完成构建，则跳过
    if exist "!LIB_DIR!" (
        echo [INFO] Library %%L already installed. Skipping.
    ) else (
        echo [INFO] Library %%L not found. Starting setup...

        :: 确保库目录存在
        if not exist "%LIB_DIR%" mkdir "%LIB_DIR%"

        :: 调用库的下载和构建脚本（使用完整路径）
        call "%~dp03rdPartyScripts\setup_%%L.bat" "%CONFIGURATION%"
        if errorlevel 1 (
            echo [ERROR] Failed to setup library %%L.
            exit /b 1
        )

        :: 创建标记文件，表示库已构建完成
        echo Done > "!LIB_DIR!\.done"
        echo [INFO] Library %%L setup completed.
    )
)

echo [INFO] All third-party libraries are ready!
endlocal