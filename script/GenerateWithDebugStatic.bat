@echo off
:: 明确传递参数（避免换行符）
call downloadDepndence.bat Debug

:: 调用主构建脚本进行 Debug 配置的构建
call generate.bat Debug static