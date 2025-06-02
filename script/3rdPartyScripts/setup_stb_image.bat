@echo off
setlocal enabledelayedexpansion

:: 设置目录路径
set BASE_DIR=%~dp0..\..\..
set BASE_DIR=%BASE_DIR:~0,-1%
set THIRDPARTY_DIR=%BASE_DIR%\3rdparty
set STB_DIR=%THIRDPARTY_DIR%\stb

set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" (
    set BUILD_TYPE=Release
)

:: 设置安装目录
set INSTALL_DIR=%BASE_DIR%\build\%BUILD_TYPE%

echo [INFO] Setting up stb library...

:: 克隆或更新 stb 仓库
if exist "%STB_DIR%" (
    echo [INFO] Detected existing stb repository, attempting to update...
    cd /d "%STB_DIR%"
    git pull
    if errorlevel 1 (
        echo [ERROR] Failed to update stb repository.
        exit /b 1
    )
) else (
    echo [INFO] Cloning stb repository...
    git clone git@github.com:JZNMCO55/stb.git "%STB_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone stb repository.
        exit /b 1
    )
)

:: 创建安装目录
if not exist "%INSTALL_DIR%\include\stb" (
    echo [INFO] Creating install include directory: %INSTALL_DIR%\include\stb
    mkdir "%INSTALL_DIR%\include\stb"
)

:: 要复制的头文件列表 (只包含实际存在的头文件)
set HEADER_LIST=stb_image.h stb_image_write.h stb_image_resize2.h stb_truetype.h stb_rect_pack.h stb_perlin.h stb_sprintf.h stb_textedit.h stb_easy_font.h

:: 逐个拷贝头文件
for %%H in (%HEADER_LIST%) do (
    echo [INFO] Copying %%H...
    copy /Y "%STB_DIR%\%%H" "%INSTALL_DIR%\include\stb\%%H"
    if errorlevel 1 (
        echo [ERROR] Failed to copy %%H
        exit /b 1
    )
)

:: 从 deprecated 目录复制旧版本的 stb_image_resize.h (为了兼容性)
echo [INFO] Copying stb_image_resize.h from deprecated directory...
copy /Y "%STB_DIR%\deprecated\stb_image_resize.h" "%INSTALL_DIR%\include\stb\stb_image_resize.h"
if errorlevel 1 (
    echo [WARNING] Failed to copy stb_image_resize.h from deprecated directory
)

echo [INFO] stb headers setup completed successfully.
echo [INFO] Include path: %INSTALL_DIR%\include\stb
endlocal
