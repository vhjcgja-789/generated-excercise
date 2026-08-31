@echo off
rem ============================================================
rem  copy_runtime.bat [x86|x64] [Debug|Release|bin]
rem  Copy zlgcan.dll + kerneldlls of the given arch from the SDK
rem  folder to this project's output folder (default x86 / Debug).
rem  Usage:
rem    copy_runtime.bat          -> x86  -> Debug
rem    copy_runtime.bat x64      -> x64  -> Debug
rem    copy_runtime.bat x64 bin  -> x64  -> bin (VSCode/MinGW output)
rem    copy_runtime.bat x86 Release
rem ============================================================
rem NOTE: the SDK folder name contains parentheses "(20260414)".
rem Never expand a path containing parentheses inside an if-block at
rem parse time with %VAR% -- cmd re-scans the block for "(" and breaks.
rem Use delayed expansion (!VAR!) here, which expands at run time.
setlocal EnableDelayedExpansion
set ARCH=%1
if "%ARCH%"=="" set ARCH=x86
set CFG=%2
if "%CFG%"=="" set CFG=Debug

set "SDK=%~dp0..\zlgcan(20260414)\zlgcan_%ARCH%"

if /i "%CFG%"=="bin" (set "DST=%~dp0bin") else (set "DST=%~dp0%CFG%")

call :check_dir "%SDK%"
if errorlevel 1 (
    echo [ERROR] SDK folder not found: !SDK!
    exit /b 1
)

if not exist "%DST%" mkdir "%DST%"

copy /Y "%SDK%\zlgcan.dll" "%DST%\" >nul
if not exist "%DST%\kerneldlls" mkdir "%DST%\kerneldlls"
xcopy /E /I /Y "%SDK%\kerneldlls\*" "%DST%\kerneldlls\" >nul

echo [DONE] Copied %ARCH% runtime (zlgcan.dll + kerneldlls) to %DST%
endlocal
exit /b 0

:check_dir
if exist "%~1\zlgcan.dll" exit /b 0
exit /b 1
