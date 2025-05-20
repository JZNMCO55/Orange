@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set SPDLOG_DIR=%THIRDPARTY_DIR%\spdlog

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" (
    set BUILD_TYPE=Release
)

:: 设置安装目录
set INSTALL_DIR=%BASE_DIR%\build\%BUILD_TYPE%

echo [INFO] Setting up SPDLOG library...

:: 克隆或更新 SPDLOG 仓库
if exist "%SPDLOG_DIR%" (
    echo [INFO] Detected existing SPDLOG repository, attempting to update...
    cd /d "%SPDLOG_DIR%"
    git fetch
    git checkout fmt-11.2.0
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update SPDLOG repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning spdlog repository...
    git clone git@github.com:JZNMCO55/spdlog.git "%SPDLOG_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone SPDLOG repository.
        exit /b 1
    )
    cd /d "%SPDLOG_DIR%"
    git checkout fmt-11.2.0
)

:: 配置和构建
echo [INFO] Configuring SPDLOG...
set BUILD_DIR=%SPDLOG_DIR%\build_%BUILD_TYPE%
if not exist "%BUILD_DIR%" (
    echo [INFO] Creating build directory: %BUILD_DIR%
    mkdir "%BUILD_DIR%"
)

:: 创建安装目录
if not exist "%INSTALL_DIR%" (
    echo [INFO] Creating install directory: %INSTALL_DIR%
    mkdir "%INSTALL_DIR%"
)

cd /d "%BUILD_DIR%"

:: CMake 配置
cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
         -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ^
         -DSPDLOG_BUILD_EXAMPLE=OFF ^
         -DSPDLOG_BUILD_TESTS=OFF ^
         -DSPDLOG_INSTALL=ON ^
         -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake generation failed.
    exit /b 1
)

:: 构建和安装
echo [INFO] Building and installing SPDLOG...
cmake --build . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed.
    exit /b 1
)

echo [INFO] SPDLOG setup completed successfully.
echo [INFO] Include path: %INSTALL_DIR%\include
echo [INFO] Library path: %INSTALL_DIR%\lib
endlocal
