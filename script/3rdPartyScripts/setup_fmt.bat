@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set FMT_DIR=%THIRDPARTY_DIR%\fmt

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" (
    set BUILD_TYPE=Release
)

:: 设置安装目录（与主项目保持一致）
set INSTALL_DIR=%BASE_DIR%\build\%BUILD_TYPE%

echo [INFO] Setting up {fmt} library...

:: 克隆或更新 {fmt}
if exist "%FMT_DIR%" (
    echo [INFO] Detected existing fmt repository, attempting to update...
    cd /d "%FMT_DIR%"
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update fmt repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning fmt repository...
    git clone git@github.com:JZNMCO55/fmt.git "%FMT_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone fmt repository.
        exit /b 1
    )
)

:: 编译 {fmt}
echo [INFO] Compiling fmt...
set BUILD_DIR=%FMT_DIR%\build_%BUILD_TYPE%
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

:: CMake 配置（启用静态库、安装目标）
cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
         -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ^
         -DFMT_TEST=OFF ^
         -DFMT_DOC=OFF ^
         -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake generation failed.
    exit /b 1
)

:: 编译
cmake --build . --config %BUILD_TYPE% --target install
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

:: 安装（已通过 --target install 完成，此处可省略）
echo [INFO] Installing {fmt}...
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed.
    exit /b 1
)

echo [INFO] {fmt} setup completed successfully.
echo [INFO] Include path: %INSTALL_DIR%\include
echo [INFO] Library path: %INSTALL_DIR%\lib
endlocal