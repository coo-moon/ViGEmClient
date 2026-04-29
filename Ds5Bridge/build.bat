@echo off
setlocal enabledelayedexpansion

echo === DS5 Bridge Build Script ===
echo.

set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat not found at %VCVARS%
    exit /b 1
)

call "%VCVARS%" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: vcvars64.bat failed
    exit /b 1
)

echo [1/4] Environment ready.

set "ROOT=%~dp0.."
set "INCLUDE_DIR=%ROOT%\include"
set "SRC_DIR=%ROOT%\src"
set "BUILD_DIR=%~dp0build"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [2/4] Compiling ViGEmClient.cpp...
cl /nologo /c /std:c++17 /MT /O2 /W3 ^
    /I"%INCLUDE_DIR%" ^
    "%SRC_DIR%\ViGEmClient.cpp" ^
    /Fo:"%BUILD_DIR%\ViGEmClient.obj"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: ViGEmClient compilation failed
    exit /b 1
)

echo [3/4] Creating ViGEmClient.lib...
lib /nologo /out:"%BUILD_DIR%\ViGEmClient.lib" "%BUILD_DIR%\ViGEmClient.obj"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: lib failed
    exit /b 1
)

echo [4/4] Compiling ds5_bridge.exe...
cl /nologo /std:c++17 /MT /O2 /W3 /EHsc ^
    /I"%INCLUDE_DIR%" ^
    "%~dp0main.cpp" ^
    "%BUILD_DIR%\ViGEmClient.lib" ^
    setupapi.lib hid.lib ^
    /Fe:"%BUILD_DIR%\ds5_bridge.exe"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Bridge compilation failed
    exit /b 1
)

echo.
echo === Build SUCCESS ===
echo Binary: %BUILD_DIR%\ds5_bridge.exe
exit /b 0
