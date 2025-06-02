@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set ASSIMP_DIR=%THIRDPARTY_DIR%\assimp

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" (
    set BUILD_TYPE=Release
)

:: 设置安装目录
set INSTALL_DIR=%BASE_DIR%\build\%BUILD_TYPE%

echo [INFO] Setting up Assimp library...

:: 克隆或更新 Assimp 仓库
if exist "%ASSIMP_DIR%" (
    echo [INFO] Detected existing Assimp repository, attempting to update...
    cd /d "%ASSIMP_DIR%"
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update Assimp repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning assimp repository...
    git clone git@github.com:JZNMCO55/assimp.git "%ASSIMP_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone Assimp repository.
        exit /b 1
    )
)

:: 创建构建目录
echo [INFO] Configuring Assimp...
set BUILD_DIR=%ASSIMP_DIR%\build_%BUILD_TYPE%
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
         -DASSIMP_BUILD_TESTS=OFF ^
         -DASSIMP_BUILD_SAMPLES=OFF ^
         -DASSIMP_BUILD_ASSIMP_TOOLS=OFF ^
         -DASSIMP_INSTALL_PDB=OFF ^
         -DASSIMP_INJECT_DEBUG_POSTFIX=OFF ^
         -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake generation failed.
    exit /b 1
)

:: 编译
echo [INFO] Building Assimp...
cmake --build . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

:: 安装
echo [INFO] Installing Assimp...
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed.
    exit /b 1
)

echo [INFO] Assimp setup completed successfully.
echo [INFO] Include path: %INSTALL_DIR%\include
echo [INFO] Library path: %INSTALL_DIR%\lib
endlocal
