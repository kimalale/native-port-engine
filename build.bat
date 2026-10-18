@echo off
setlocal
set PATH=C:\msys64\mingw32\bin;%PATH%

echo [Build] Compiling Universal Android-to-Windows Native Port Engine (32-bit x86 + ARM32 Bridge)...

gcc -O2 -g -Wall -Wno-unused-function -Wno-format-truncation -Wno-stringop-truncation ^
    src/main.c ^
    src/game_profile.c ^
    src/elf_loader.c ^
    src/arm32_bridge.c ^
    src/shims.c ^
    src/gl_wrap.c ^
    src/gl_buffers.c ^
    src/egl_wrap.c ^
    src/mesh_dump.c ^
    src/android_ndk.c ^
    src/fake_jni.c ^
    src/opensles.c ^
    src/hook_util.c ^
    src/minigore2_plugin.c ^
    src/watchdog.c ^
    src/log.c ^
    -o native-port-engine.exe ^
    -lmingw32 -lSDL2 -lopengl32 -lws2_32 -lwinmm -lm

if %ERRORLEVEL% NEQ 0 (
    echo [Build] FAILED with exit code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

if not exist SDL2.dll (
    if exist C:\msys64\mingw32\bin\SDL2.dll (
        copy /Y C:\msys64\mingw32\bin\SDL2.dll SDL2.dll >nul
    ) else if exist ..\mini-gore\SDL2.dll (
        copy /Y ..\mini-gore\SDL2.dll SDL2.dll >nul
    )
)

echo [Build] SUCCESS: native-port-engine.exe built!
