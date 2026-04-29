@echo off
setlocal enabledelayedexpansion

set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

call "%VCVARS%" >nul 2>&1

set "ROOT=%~dp0.."
set "INCLUDE_DIR=%ROOT%\include"
set "SRC_DIR=%ROOT%\src"
set "BUILD_DIR=%~dp0build"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo Compiling test_trigger.exe...
cl /nologo /std:c++17 /MT /O2 /W3 /EHsc ^
    "%~dp0test_trigger.cpp" ^
    setupapi.lib hid.lib ^
    /Fe:"test_trigger.exe"

if %ERRORLEVEL% EQU 0 (
    echo BUILD OK
) else (
    echo BUILD FAILED
)
