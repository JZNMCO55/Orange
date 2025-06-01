@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set SHADERC_DIR=%THIRDPARTY_DIR%\shaderc

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" (
    set BUILD_TYPE=Release
)

:: 设置安装目录
set INSTALL_DIR=%BASE_DIR%\build\%BUILD_TYPE%

echo [INFO] Setting up Shaderc library...

:: 克隆或更新 Shaderc 仓库
if exist "%SHADERC_DIR%" (
    echo [INFO] Detected existing Shaderc repository, attempting to update...
    cd /d "%SHADERC_DIR%"
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update Shaderc repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning shaderc repository...
    git clone git@github.com:JZNMCO55/shaderc.git "%SHADERC_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone Shaderc repository.
        exit /b 1
    )
)

:: 同步依赖（Shaderc需要glslang和SPIRV-Tools等依赖）
echo [INFO] Syncing Shaderc dependencies...
cd /d "%SHADERC_DIR%"
python utils/git-sync-deps
if errorlevel 1 (
    echo [ERROR] Failed to sync Shaderc dependencies.
    exit /b 1
)

:: 创建构建目录
echo [INFO] Configuring Shaderc...
set BUILD_DIR=%SHADERC_DIR%\build_%BUILD_TYPE%
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
         -DSHADERC_SKIP_TESTS=ON ^
         -DSHADERC_SKIP_EXAMPLES=ON ^
         -DSHADERC_SKIP_COPYRIGHT_CHECK=ON ^
         -DSHADERC_ENABLE_SHARED_CRT=ON ^
         -DSHADERC_ENABLE_WERROR_COMPILE=OFF ^
         -DSPIRV_SKIP_TESTS=ON ^
         -DSPIRV_SKIP_EXECUTABLES=ON ^
         -DENABLE_GLSLANG_BINARIES=OFF ^
         -DENABLE_SPVREMAPPER=OFF ^
         -DENABLE_CTEST=OFF ^
         -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake generation failed.
    exit /b 1
)

:: 编译
echo [INFO] Building Shaderc...
cmake --build . --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

:: 安装
echo [INFO] Installing Shaderc...
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] Installation failed.
    exit /b 1
)

echo [INFO] Shaderc setup completed successfully.
echo [INFO] Include path: %INSTALL_DIR%\include
echo [INFO] Library path: %INSTALL_DIR%\lib
echo [INFO] CMake config: %INSTALL_DIR%\lib\cmake\shaderc

endlocal
