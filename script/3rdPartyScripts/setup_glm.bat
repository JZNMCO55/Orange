@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set GLM_DIR=%THIRDPARTY_DIR%\glm

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" (
    set BUILD_TYPE=Release
)

:: 设置安装目录
set INSTALL_DIR=%BASE_DIR%\build\%BUILD_TYPE%

echo [INFO] Setting up GLM library...

:: 克隆或更新 GLM 仓库
if exist "%GLM_DIR%" (
    echo [INFO] Detected existing GLM repository, attempting to update...
    cd /d "%GLM_DIR%"
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update GLM repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning glm repository...
    git clone git@github.com:JZNMCO55/glm.git "%GLM_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone GLM repository.
        exit /b 1
    )
)

:: GLM是头文件库，但我们仍需要CMake配置以便集成
echo [INFO] Configuring GLM...
set BUILD_DIR=%GLM_DIR%\build_%BUILD_TYPE%
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
         -DGLM_TEST_ENABLE=OFF ^
         -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake generation failed.
    exit /b 1
)

:: 安装（主要是头文件）
echo [INFO] Installing GLM...
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed.
    exit /b 1
)

echo [INFO] GLM setup completed successfully.
echo [INFO] Include path: %INSTALL_DIR%\include
endlocal
