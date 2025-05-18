@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set GTEST_DIR=%THIRDPARTY_DIR%\googletest

set BUILD_TYPE=%~1

:: 设置安装目录（与主项目保持一致）
set INSTALL_DIR=%BASE_DIR%\out\%BUILD_TYPE%

echo [INFO] Setting up Google Test...

:: 克隆或更新googletest
if exist "%GTEST_DIR%" (
    echo [INFO] Detected existing googletest repository, attempting to update...
    cd /d "%GTEST_DIR%"
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update googletest repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning googletest repository...
    git clone https://github.com/google/googletest.git "%GTEST_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone googletest repository.
        exit /b 1
    )
)

:: 编译googletest
echo [INFO] Compiling googletest...
set BUILD_DIR=%GTEST_DIR%\build_%BUILD_TYPE%
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

cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
         -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR% ^
         -DCMAKE_PREFIX_PATH=%INSTALL_DIR% ^
         -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake generation failed.
    exit /b 1
)

cmake --build . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    exit /b 1
)

:: 安装 Google Test
echo [INFO] Installing Google Test...
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed.
    exit /b 1
)

echo [INFO] Google Test setup completed.
endlocal