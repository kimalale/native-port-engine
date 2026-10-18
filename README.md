# Universal Android-to-Windows Native Port Engine (`native-port-engine`)

A universal native execution & porting engine that takes an Android game `.apk` (or extracted folder / `.so`), scans its architecture, symbols, and framework personality, and turns it into a **standalone native Windows executable** with **HQ desktop rendering enhancements** (custom resolution, 4x–8x MSAA, 16x Anisotropic Filtering, automatic mipmaps, 3D mesh/texture dumping, and live HD texture modding) — **without running an Android OS or VM**.

---

## Quick Start: Scan & Port Any APK

### 1. Scan an APK to Check Portability (`--scan`)
Point `--scan` at any `.apk` file, game folder, or `.so` binary:
```powershell
.\native-port-engine.exe --scan "path\to\game.apk"
```
The scanner automatically:
1. Extracts the `.apk` if needed and inventories all ABI folders (`lib/x86`, `lib/armeabi-v7a`, `lib/armeabi`, `lib/arm64-v8a`) plus Unity `Managed/Assembly-CSharp.dll`.
2. Loads the primary `.so` ELF (`EM_386` x86 or `EM_ARM` ARM32) and any auxiliary libraries (`libfmodex.so`, `libmono.so`, etc.).
3. Detects the **Engine / Framework Personality**:
   - **Android NDK (`ANativeActivity` / `android_main`)**
   - **Polarbit Fuse JNI (`FuseOnInit` / `OnCreate` / `OnEvent` / `AudioMix`)**
   - **Cocos2d-x JNI (`Cocos2dxRenderer_nativeInit` / `nativeRender`)**
   - **SDL2-for-Android (`SDL_main` / `SDLActivity`)**
   - **Unity 3D (`libunity.so` + `Managed/Assembly-CSharp.dll`)**
   - **Generic `GLSurfaceView.Renderer` JNI (`JNI_OnLoad` / `RegisterNatives`)**
4. Verifies 100% of the game's imported symbols against the engine's 330+ native Windows Bionic/GLES/EGL/OpenSL/JNI shims and prints a clear **Portability Verdict**.

### 2. Generate a Standalone Windows Port Folder (`--port`)
Once `--scan` confirms portability, generate a self-contained Windows port folder (just like `../mini-gore`, `../raging-thunder`, and `../gt-ride`):
```powershell
.\native-port-engine.exe --port "path\to\game.apk"
# Or specify a custom output folder:
.\native-port-engine.exe --port "path\to\game.apk" --out "..\my-game-port"
```
This creates `<out_dir>` containing:
- `native-port-engine.exe` + `SDL2.dll`
- `apk/` (extracted assets & native libraries)
- `game_profile.ini` (auto-configured for the detected backend, 1280x720, 4x MSAA, 16x Anisotropic Filtering)
- `run_port.bat` (one-click Windows launcher)
- `dump_hq_assets.bat` (one-click 3D `.obj/.mtl` mesh & `.tga` texture exporter)
- `save_data/` & `mods/textures/` (drop replacement `.tga` textures here for live HD texture modding)
- `port_unity_windows.ps1` (if a Unity 3D APK is detected, scaffolds the AssetRipper + C# compatibility shim pipeline used for `../gt-ride`)

### 3. Run Directly Without Generating a Folder
```powershell
.\native-port-engine.exe "path\to\game.apk" --width 1920 --height 1080 --msaa 4 --aniso 16
```

---

## Verified Workspace Games

| Game | Target Path | Architecture & Framework | Relocation Coverage |
| :--- | :--- | :--- | :--- |
| **Minigore 2: Zombies** | `..\mini-gore` | `x86` (`EM_386`) — NDK (`android_main` + GLES 2.0 + OpenSL ES) | **298 / 298 (100.00%)** |
| **Raging Thunder 2** | `..\raging-thunder` | `x86` (`EM_386`) & `armeabi` (`EM_ARM`) — Polarbit Fuse JNI + GLES 1.1 | **55 / 55 x86 (100.00%)** / **62 / 62 ARM32 (100.00%)** |
| **Real Racing 3** | `..\real-racing-3-windows` | `armeabi-v7a` (`EM_ARM`) — Firemonkeys JNI (`onCreateJNI`, `onViewRenderJNI`) + GLES 2.0 + FMOD Ex | **100% Native Exec (60 FPS)** |
| **GT Ride** | `..\gt-ride` | `armeabi-v7a` (`EM_ARM`) — Unity 3D (`libunity.so` + `Assembly-CSharp.dll`) | **Unity Windows Native Pipeline** |

---

## Building from Source

Requires 32-bit MinGW-w64 GCC (`C:\msys64\mingw32\bin\gcc.exe`) and SDL2:
```powershell
.\build.bat
```
