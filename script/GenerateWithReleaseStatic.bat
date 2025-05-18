@echo off
:: 下载并构建 Release 版本的三方库
call downloadDepndence.bat Release

:: 调用主构建脚本进行 Release 配置的构建
call generate.bat Release static