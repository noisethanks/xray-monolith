@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Usage: publish.bat [version] [output-directory]
rem Rebuild all four MT engines, then create a complete GAMMA-ready package.
where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: Windows PowerShell is required to publish.
    exit /b 1
)

set "BUILD_TARGET=Rebuild"
call "%~dp0build-mt.bat"
if errorlevel 1 exit /b 1

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\publish.ps1" -Version "%~1" -OutputDirectory "%~2"
exit /b %ERRORLEVEL%
