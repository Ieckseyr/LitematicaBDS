@echo off
REM 一条命令构建：核心 + 单测 + LeviLamina 插件 + endstone wheel
REM 用法:
REM   build.cmd
REM   build.cmd --only core
REM   build.cmd --deploy-endstone D:\srv\plugins
setlocal
cd /d "%~dp0"
python "%~dp0build.py" %*
if errorlevel 1 (
    echo.
    echo [build] 失败
    pause
    exit /b 1
)
endlocal
