@echo off

setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%SCRIPT_DIR%\build-windows"

set CLEAN=0
set RUN=0
set "QT_PREFIX="

:argloop
if "%~1"=="" goto args_done
if /i "%~1"=="/run"   set RUN=1   & shift & goto argloop
if /i "%~1"=="/clean" set CLEAN=1 & shift & goto argloop
if /i "%~1"=="/qt"    set "QT_PREFIX=%~2" & shift & shift & goto argloop
if /i "%~1"=="/h"     goto help
if /i "%~1"=="/?"     goto help
if /i "%~1"=="-h"     goto help
if /i "%~1"=="--help" goto help
echo build_windows.bat: unknown arg '%~1' 1>&2
exit /b 1

:args_done

if "%CLEAN%"=="1" if exist "%BUILD_DIR%" (
    echo build_windows.bat: wiping %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%"
)

:: Tool checks.
where cmake >nul 2>nul
if errorlevel 1 (
    echo build_windows.bat: cmake not on PATH. 1>&2
    echo   Install: winget install Kitware.CMake 1>&2
    exit /b 1
)

:: Qt prefix probing. Caller's /qt wins; else look for the documented
:: 6.8.0/msvc2022_64 path; else pick the highest 6.x\msvc2022_64 under C:\Qt.
if "%QT_PREFIX%"=="" (
    if exist "C:\Qt\6.8.0\msvc2022_64\bin\Qt6Core.dll" (
        set "QT_PREFIX=C:\Qt\6.8.0\msvc2022_64"
    )
)
if "%QT_PREFIX%"=="" if exist "C:\Qt" (
    for /d %%V in ("C:\Qt\6.*") do (
        if exist "%%V\msvc2022_64\bin\Qt6Core.dll" set "QT_PREFIX=%%V\msvc2022_64"
    )
)
if "%QT_PREFIX%"=="" (
    echo build_windows.bat: Qt6 ^(MSVC 2022 64-bit^) not found. 1>&2
    echo   Install Qt 6.x via https://www.qt.io/download-open-source 1>&2
    echo   and select "MSVC 2022 64-bit" under the Qt 6.x release. 1>&2
    echo   Or rerun with /qt ^<path-to-Qt-msvc2022_64^> 1>&2
    exit /b 1
)
echo build_windows.bat: using Qt prefix !QT_PREFIX!

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo build_windows.bat: configuring ^(Release^)...
cmake -G "Visual Studio 17 2022" -A x64 ^
      -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" ^
      -DCMAKE_PREFIX_PATH="!QT_PREFIX!"
if errorlevel 1 (
    echo build_windows.bat: cmake configure failed 1>&2
    exit /b 1
)

echo build_windows.bat: building...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo build_windows.bat: cmake build failed 1>&2
    exit /b 1
)

set "BRAIN_EXE=%BUILD_DIR%\Release\brain.exe"
if not exist "%BRAIN_EXE%" (
    echo build_windows.bat: build succeeded but %BRAIN_EXE% not found 1>&2
    exit /b 1
)

for %%F in ("%BRAIN_EXE%") do echo build_windows.bat: built %%~fF ^(%%~zF bytes^)

:: First-run convenience: copy the Qt runtime DLLs the EXE needs into
:: the output dir using windeployqt. Without this brain.exe pops a
:: "Qt6Core.dll not found" dialog when launched outside the Qt-env shell.
if exist "!QT_PREFIX!\bin\windeployqt.exe" (
    echo build_windows.bat: staging Qt runtime via windeployqt...
    "!QT_PREFIX!\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler ^
        "%BRAIN_EXE%" >nul
)

if "%RUN%"=="1" (
    echo build_windows.bat: launching brain.exe...
    "%BRAIN_EXE%"
)

endlocal
exit /b 0

:help
findstr "^::" "%~f0" | findstr /v "@echo" | findstr /v "build_windows.bat:" | for /f "tokens=*" %%L in ('more') do echo %%L
exit /b 0
