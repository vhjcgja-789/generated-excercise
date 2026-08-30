@echo off
rem ============================================================
rem  copy_runtime.bat [x86|x64] [Debug|Release]
rem  将 zlgcan(20260414) 中对应架构的 zlgcan.dll 与 kerneldlls
rem  复制到本工程输出目录（默认 x86 / Debug）
rem  用法:
rem    copy_runtime.bat          -> x86  -> Debug
rem    copy_runtime.bat x64      -> x64  -> Debug
rem    copy_runtime.bat x86 Release
rem ============================================================
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x86
set CFG=%2
if "%CFG%"=="" set CFG=Debug

set SRC=%~dp0..\zlgcan(20260414)\zlgcan_%ARCH%
set DST=%~dp0%CFG%

if not exist "%SRC%\zlgcan.dll" (
    echo [错误] 未找到 SDK 目录: %SRC%
    exit /b 1
)
if not exist "%DST%" mkdir "%DST%"

copy /Y "%SRC%\zlgcan.dll" "%DST%\" >nul
if not exist "%DST%\kerneldlls" mkdir "%DST%\kerneldlls"
xcopy /E /I /Y "%SRC%\kerneldlls\*" "%DST%\kerneldlls\" >nul

echo [完成] 已复制 %ARCH% 运行库(zlgcan.dll + kerneldlls) 到 %DST%
endlocal
