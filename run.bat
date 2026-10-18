@echo off
setlocal
cd /d "%~dp0"

if not exist native-port-engine.exe (
    call build.bat
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
)

if not "%~1"=="" (
    native-port-engine.exe %*
    exit /b %ERRORLEVEL%
)

echo ================================================================
echo  Universal Android-to-Windows Native Port Engine
echo ================================================================
echo   [1] Play Minigore 2: Zombies       (NDK + GLES 2.0 + OpenSL ES, 4x MSAA, 16x Aniso)
echo   [2] Play Raging Thunder 2          (Polarbit Fuse JNI + GLES 1.1, 4x MSAA, 16x Aniso)
echo   [3] Scan All 3 Workspace Games     (Minigore 2, Raging Thunder 2, GT Ride)
echo   [4] Scan a Custom APK / Folder     (--scan ^<path^>)
echo   [5] Port a Custom APK to Windows   (--port ^<path^>)
echo ================================================================
set /p CHOICE="Select option [1-5]: "

if "%CHOICE%"=="1" (
    native-port-engine.exe --profile profiles\minigore2.ini
) else if "%CHOICE%"=="2" (
    native-port-engine.exe --profile profiles\raging_thunder2.ini
) else if "%CHOICE%"=="3" (
    native-port-engine.exe --scan ..\mini-gore
    echo.
    native-port-engine.exe --scan ..\raging-thunder
    echo.
    native-port-engine.exe --scan ..\gt-ride
) else if "%CHOICE%"=="4" (
    set /p TARGET_APK="Enter path to .apk or game folder to scan: "
    native-port-engine.exe --scan "%TARGET_APK%"
) else if "%CHOICE%"=="5" (
    set /p TARGET_APK="Enter path to .apk or game folder to port: "
    native-port-engine.exe --port "%TARGET_APK%"
) else (
    echo Invalid choice.
)
