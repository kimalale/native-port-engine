#ifndef GAME_PROFILE_H
#define GAME_PROFILE_H

#include "elf_loader.h"

typedef enum {
    BACKEND_AUTO = 0,
    BACKEND_NDK,            /* ANativeActivity / android_main + OpenSL ES + GLES2/1 */
    BACKEND_POLARBIT_FUSE,  /* Polarbit Fuse JNI (OnInit/OnCreate/OnEvent/AudioMix) */
    BACKEND_COCOS2DX,       /* Cocos2d-x JNI (Cocos2dxRenderer_nativeInit/Render)   */
    BACKEND_SDL2_ANDROID,   /* SDL2 Android (SDL_main / SDLActivity_nativeInit)     */
    BACKEND_UNITY,          /* Unity 3D (libunity.so + Managed/Assembly-CSharp.dll) */
    BACKEND_GENERIC_JNI     /* Generic JNI_OnLoad / RegisterNatives / GLSurfaceView */
} game_backend_t;

#define MAX_AUX_LIBS 24

typedef struct {
    char title[128];
    char package_name[128];

    /* Resolved filesystem paths */
    char game_root[512];
    char assets_dir[512];
    char so_path[512];
    char aux_so_paths[MAX_AUX_LIBS][512];
    int  aux_so_count;
    char apk_path[512];
    char save_dir[512];
    char mod_dir[512];
    char unity_dll_path[512];

    /* Architecture & Engine metadata */
    int  elf_machine;       /* 3 = EM_386 (x86), 40 = EM_ARM (ARM32) */
    int  has_unity_dll;

    /* Runtime & HQ rendering settings */
    game_backend_t backend;
    int width;
    int height;
    int fullscreen;
    int msaa;
    int anisotropy;
    int auto_mipmaps;       /* -1 = auto, 0 = off, 1 = on */
    int dump_meshes;
    int allow_missing_syms;
    int force_touch_sticks;

    /* Normalized virtual dual-stick coordinates [0..1] for generic touch-stick fallback */
    float move_stick_x;
    float move_stick_y;
    float move_stick_r;
    float aim_stick_x;
    float aim_stick_y;
    float aim_stick_r;
} game_profile_t;

/* Initialize profile with HQ defaults (1280x720, 4x MSAA, 16x Aniso) */
void profile_init_defaults(game_profile_t *prof);

/* Load overrides from a .ini profile file */
int  profile_load_ini(game_profile_t *prof, const char *ini_path);

/* Resolve a target path (folder, .apk, .so, or .ini) into concrete paths */
int  profile_resolve_target(game_profile_t *prof, const char *target_path);

/* Auto-detect execution backend from a loaded ELF module */
game_backend_t profile_detect_backend(const elf_module_t *mod);

/* Human-readable backend name */
const char *profile_backend_name(game_backend_t b);

/* Scan a target directory, .apk, or .so and print full portability verdict & symbol report */
int  profile_scan_target(const char *target_path);
int  profile_inspect_target(const char *target_path);

/* Scaffold and generate a complete standalone Windows native port directory from an APK or folder */
int  profile_port_target(const char *target_path, const char *out_dir_override);

#endif /* GAME_PROFILE_H */
