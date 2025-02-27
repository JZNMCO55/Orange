@echo off
setlocal

:: 获取脚本所在目录
set SCRIPT_DIR=%~dp0

:: 获取上一级目录（即项目根目录）
set PROJECT_DIR=%SCRIPT_DIR:~0,-7%

:: 获取构建配置（默认为 Release）
set CONFIGURATION=Release
if "%1"=="debug" set CONFIGURATION=Debug
if "%1"=="Release" set CONFIGURATION=Release

:: 设置静态库和动态库选项
set SHARED_LIBRARY=OFF
if "%2"=="shared" set SHARED_LIBRARY=ON
if "%2"=="static" set SHARED_LIBRARY=OFF

:: 设置构建目录和输出目录
set BUILD_DIR=%PROJECT_DIR%\build\%CONFIGURATION%
set OUTPUT_DIR=%PROJECT_DIR%\out\%CONFIGURATION%

:: 输出正在使用的构建配置
echo Using %CONFIGURATION% configuration.

:: 检查并删除已有的构建目录及缓存
if exist %BUILD_DIR%\CMakeCache.txt (
    echo Deleting existing CMake cache and build directory...
    del /f /q %BUILD_DIR%\CMakeCache.txt
)

:: 如果构建目录不存在，创建它
if not exist %BUILD_DIR% (
    echo Creating build directory...
    mkdir %BUILD_DIR%
)

:: 创建输出目录
echo Creating output directory...
mkdir %OUTPUT_DIR%

:: 进入构建目录
cd /d %BUILD_DIR%

:: 运行 CMake 配置，并指定构建目录和输出目录
echo Running CMake to configure the project...
cmake -G "Visual Studio 17 2022" -A x64 ^
    -DBUILD_SHARED_LIBS=%SHARED_LIBRARY% ^
    -DCMAKE_CONFIGURATION_TYPES=%CONFIGURATION% ^
    -DCMAKE_BINARY_DIR=%BUILD_DIR% ^
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=%OUTPUT_DIR%\lib ^
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=%OUTPUT_DIR%\lib ^
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=%OUTPUT_DIR%\bin ^
    %PROJECT_DIR%

echo Build complete!
endlocal
pause
