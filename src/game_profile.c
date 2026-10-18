#include "game_profile.h"
#include "shims.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void profile_init_defaults(game_profile_t *prof) {
    memset(prof, 0, sizeof(*prof));
    strcpy(prof->title, "Android Native Port");
    strcpy(prof->package_name, "com.android.nativeport");
    prof->backend            = BACKEND_AUTO;
    prof->width              = 1280;
    prof->height             = 720;
    prof->fullscreen         = 0;
    prof->msaa               = 4;
    prof->anisotropy         = 16;
    prof->auto_mipmaps       = -1; /* auto based on backend */
    prof->dump_meshes        = 0;
    prof->allow_missing_syms = 0;
    prof->force_touch_sticks = 0;

    prof->move_stick_x = 0.14f;
    prof->move_stick_y = 0.78f;
    prof->move_stick_r = 0.10f;
    prof->aim_stick_x  = 0.86f;
    prof->aim_stick_y  = 0.78f;
    prof->aim_stick_r  = 0.10f;
}

const char *profile_backend_name(game_backend_t b) {
    switch (b) {
        case BACKEND_NDK:           return "NDK (ANativeActivity / android_main)";
        case BACKEND_POLARBIT_FUSE: return "Polarbit Fuse JNI (OnInit/OnCreate/OnEvent)";
        case BACKEND_COCOS2DX:      return "Cocos2d-x JNI (Cocos2dxRenderer_nativeInit/Render)";
        case BACKEND_SDL2_ANDROID:  return "SDL2 Android (SDL_main / SDLActivity)";
        case BACKEND_UNITY:         return "Unity 3D (libunity.so + Managed/Assembly-CSharp.dll)";
        case BACKEND_GENERIC_JNI:   return "Generic JNI (JNI_OnLoad / GLSurfaceView.Renderer)";
        default:                    return "Auto-Detect";
    }
}

static const char *backend_short_key(game_backend_t b) {
    switch (b) {
        case BACKEND_NDK:           return "ndk";
        case BACKEND_POLARBIT_FUSE: return "polarbit";
        case BACKEND_COCOS2DX:      return "cocos2dx";
        case BACKEND_SDL2_ANDROID:  return "sdl2";
        case BACKEND_UNITY:         return "unity";
        case BACKEND_GENERIC_JNI:   return "jni";
        default:                    return "auto";
    }
}

static char *trim_ws(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static int file_exists(const char *path) {
    if (!path || !*path) return 0;
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static int dir_exists(const char *path) {
    if (!path || !*path) return 0;
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static void get_parent_dir(const char *path, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s", path);
    char *p1 = strrchr(out, '\\');
    char *p2 = strrchr(out, '/');
    char *p = (p1 > p2) ? p1 : p2;
    if (p) *p = '\0';
    else strcpy(out, ".");
}

static void normalize_slashes(char *s) {
    for (; *s; s++) {
        if (*s == '\\') *s = '/';
    }
}

static void resolve_rel_path(const char *base_dir, const char *rel, char *out, size_t out_sz) {
    if (!rel || !*rel) { out[0] = '\0'; return; }
    char combined[512];
    if ((isalpha((unsigned char)rel[0]) && rel[1] == ':') || rel[0] == '\\' || rel[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", rel);
    } else {
        snprintf(combined, sizeof(combined), "%s\\%s", base_dir, rel);
    }
    if (!GetFullPathNameA(combined, (DWORD)out_sz, out, NULL)) {
        snprintf(out, out_sz, "%s", combined);
    }
    normalize_slashes(out);
}

int profile_load_ini(game_profile_t *prof, const char *ini_path) {
    FILE *f = fopen(ini_path, "r");
    if (!f) {
        fprintf(stderr, "[Profile] Cannot open INI file: %s\n", ini_path);
        return -1;
    }

    char ini_dir[512];
    get_parent_dir(ini_path, ini_dir, sizeof(ini_dir));

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim_ws(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim_ws(s);
        char *val = trim_ws(eq + 1);

        if (_stricmp(key, "title") == 0)
            snprintf(prof->title, sizeof(prof->title), "%s", val);
        else if (_stricmp(key, "package") == 0 || _stricmp(key, "package_name") == 0)
            snprintf(prof->package_name, sizeof(prof->package_name), "%s", val);
        else if (_stricmp(key, "game_root") == 0)
            resolve_rel_path(ini_dir, val, prof->game_root, sizeof(prof->game_root));
        else if (_stricmp(key, "assets_dir") == 0)
            resolve_rel_path(ini_dir, val, prof->assets_dir, sizeof(prof->assets_dir));
        else if (_stricmp(key, "so_path") == 0)
            resolve_rel_path(ini_dir, val, prof->so_path, sizeof(prof->so_path));
        else if (_stricmp(key, "apk_path") == 0)
            resolve_rel_path(ini_dir, val, prof->apk_path, sizeof(prof->apk_path));
        else if (_stricmp(key, "save_dir") == 0)
            resolve_rel_path(ini_dir, val, prof->save_dir, sizeof(prof->save_dir));
        else if (_stricmp(key, "mod_dir") == 0)
            resolve_rel_path(ini_dir, val, prof->mod_dir, sizeof(prof->mod_dir));
        else if (_stricmp(key, "backend") == 0) {
            if (_stricmp(val, "ndk") == 0) prof->backend = BACKEND_NDK;
            else if (_stricmp(val, "polarbit") == 0 || _stricmp(val, "fuse") == 0) prof->backend = BACKEND_POLARBIT_FUSE;
            else if (_stricmp(val, "cocos2dx") == 0 || _stricmp(val, "cocos") == 0) prof->backend = BACKEND_COCOS2DX;
            else if (_stricmp(val, "sdl2") == 0 || _stricmp(val, "sdl") == 0) prof->backend = BACKEND_SDL2_ANDROID;
            else if (_stricmp(val, "unity") == 0) prof->backend = BACKEND_UNITY;
            else if (_stricmp(val, "jni") == 0) prof->backend = BACKEND_GENERIC_JNI;
            else prof->backend = BACKEND_AUTO;
        }
        else if (_stricmp(key, "width") == 0)              prof->width = atoi(val);
        else if (_stricmp(key, "height") == 0)             prof->height = atoi(val);
        else if (_stricmp(key, "fullscreen") == 0)         prof->fullscreen = atoi(val);
        else if (_stricmp(key, "msaa") == 0)               prof->msaa = atoi(val);
        else if (_stricmp(key, "anisotropy") == 0)         prof->anisotropy = atoi(val);
        else if (_stricmp(key, "auto_mipmaps") == 0)       prof->auto_mipmaps = atoi(val);
        else if (_stricmp(key, "dump_meshes") == 0)        prof->dump_meshes = atoi(val);
        else if (_stricmp(key, "allow_missing_syms") == 0) prof->allow_missing_syms = atoi(val);
        else if (_stricmp(key, "force_touch_sticks") == 0) prof->force_touch_sticks = atoi(val);
        else if (_stricmp(key, "move_stick_x") == 0)       prof->move_stick_x = (float)atof(val);
        else if (_stricmp(key, "move_stick_y") == 0)       prof->move_stick_y = (float)atof(val);
        else if (_stricmp(key, "move_stick_r") == 0)       prof->move_stick_r = (float)atof(val);
        else if (_stricmp(key, "aim_stick_x") == 0)        prof->aim_stick_x  = (float)atof(val);
        else if (_stricmp(key, "aim_stick_y") == 0)        prof->aim_stick_y  = (float)atof(val);
        else if (_stricmp(key, "aim_stick_r") == 0)        prof->aim_stick_r  = (float)atof(val);
    }
    fclose(f);
    if (prof->so_path[0]) {
        prof->elf_machine = elf_probe_machine(prof->so_path, NULL, 0);
        if (prof->aux_so_count == 0) {
            char dir[512];
            get_parent_dir(prof->so_path, dir, sizeof(dir));
            const char *b1 = strrchr(prof->so_path, '\\');
            const char *b2 = strrchr(prof->so_path, '/');
            const char *main_base = (b1 > b2) ? b1 + 1 : (b2 ? b2 + 1 : prof->so_path);
            char pattern[512];
            snprintf(pattern, sizeof(pattern), "%s\\*.so", dir);
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pattern, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (_stricmp(fd.cFileName, main_base) == 0) continue;
                    if (prof->aux_so_count < MAX_AUX_LIBS) {
                        snprintf(prof->aux_so_paths[prof->aux_so_count], 512, "%s\\%s", dir, fd.cFileName);
                        normalize_slashes(prof->aux_so_paths[prof->aux_so_count]);
                        prof->aux_so_count++;
                    }
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
    }
    return 0;
}

static int is_helper_so(const char *filename) {
    static const char *prefixes[] = {
        "libfmod", "libopenal", "libc++", "libgnustl", "libstlport",
        "libmono.so", "libgdvm.so", "libSDL2.so", "libapplovin",
        "libcrashlytics", "libtapjoy", "libgluads", "libarcore",
        "libfuelmetrics", "libnimble", "libfirebase", "libironsource",
        "libvungle", "libchartboost", "libadmob", "libfacebook",
        "libadjust", "libappsflyer", "libbugsnag", "libsentry", "libswappy"
    };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        if (_strnicmp(filename, prefixes[i], strlen(prefixes[i])) == 0) return 1;
    }
    return 0;
}

/* Scan a directory for *.so files, selecting the largest non-helper .so as the main game .so */
static int scan_so_dir(const char *dir, char *main_so, size_t main_sz,
                       char aux_sos[MAX_AUX_LIBS][512], int *aux_count) {
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.so", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int found = 0;
    uint64_t best_size = 0;
    char best_main[512] = {0};

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        uint64_t fsize = ((uint64_t)fd.nFileSizeHigh << 32) | (uint64_t)fd.nFileSizeLow;

        if (is_helper_so(fd.cFileName)) {
            if (aux_sos && aux_count && *aux_count < MAX_AUX_LIBS) {
                snprintf(aux_sos[*aux_count], 512, "%s", full);
                (*aux_count)++;
            }
        } else {
            if (!found || fsize > best_size) {
                if (found && best_main[0] && aux_sos && aux_count && *aux_count < MAX_AUX_LIBS) {
                    snprintf(aux_sos[*aux_count], 512, "%s", best_main);
                    (*aux_count)++;
                }
                snprintf(best_main, sizeof(best_main), "%s", full);
                best_size = fsize;
            } else if (aux_sos && aux_count && *aux_count < MAX_AUX_LIBS) {
                snprintf(aux_sos[*aux_count], 512, "%s", full);
                (*aux_count)++;
            }
            found++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    if (found && main_so) {
        snprintf(main_so, main_sz, "%s", best_main);
    } else if (!found && aux_count && *aux_count > 0 && main_so) {
        /* If only helper .so files were found, promote the first one */
        snprintf(main_so, main_sz, "%s", aux_sos[0]);
        found = 1;
    }
    return found;
}

/* Populate aux_so_paths from the same directory as main_so */
static void collect_sibling_aux_sos(const char *main_so_path,
                                    char aux_sos[MAX_AUX_LIBS][512], int *aux_count) {
    if (!main_so_path || !*main_so_path || !aux_sos || !aux_count) return;
    char dir[512];
    get_parent_dir(main_so_path, dir, sizeof(dir));
    const char *b1 = strrchr(main_so_path, '\\');
    const char *b2 = strrchr(main_so_path, '/');
    const char *main_base = (b1 > b2) ? b1 + 1 : (b2 ? b2 + 1 : main_so_path);

    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.so", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_stricmp(fd.cFileName, main_base) == 0) continue;
        if (*aux_count < MAX_AUX_LIBS) {
            snprintf(aux_sos[*aux_count], 512, "%s\\%s", dir, fd.cFileName);
            normalize_slashes(aux_sos[*aux_count]);
            (*aux_count)++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* Find first *.apk in a directory */
static int find_first_apk(const char *dir, char *apk_out, size_t apk_sz) {
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.apk", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            snprintf(apk_out, apk_sz, "%s\\%s", dir, fd.cFileName);
            ok = 1;
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

/* Extract an APK (ZIP archive) into dest_dir using Windows built-in tar.exe */
static int extract_apk_to_dir(const char *apk_path, const char *dest_dir) {
    CreateDirectoryA(dest_dir, NULL);
    char cmd[1536];
    snprintf(cmd, sizeof(cmd), "tar -xf \"%s\" -C \"%s\" >nul 2>&1", apk_path, dest_dir);
    printf("[Profile] Extracting APK '%s' -> '%s' ...\n", apk_path, dest_dir);
    int rc = system(cmd);
    return (rc == 0) ? 0 : -1;
}

int profile_resolve_target(game_profile_t *prof, const char *target_path) {
    if (!target_path || !*target_path) return -1;

    char full_target[512];
    if (!GetFullPathNameA(target_path, sizeof(full_target), full_target, NULL)) {
        snprintf(full_target, sizeof(full_target), "%s", target_path);
    }
    /* Strip trailing slash */
    size_t len = strlen(full_target);
    while (len > 3 && (full_target[len - 1] == '\\' || full_target[len - 1] == '/'))
        full_target[--len] = '\0';

    /* Case 1: .ini profile file */
    if (len > 4 && _stricmp(full_target + len - 4, ".ini") == 0 && file_exists(full_target)) {
        if (profile_load_ini(prof, full_target) != 0) return -1;
        if (prof->so_path[0] && file_exists(prof->so_path)) {
            if (!prof->game_root[0]) get_parent_dir(full_target, prof->game_root, sizeof(prof->game_root));
            if (!prof->assets_dir[0]) snprintf(prof->assets_dir, sizeof(prof->assets_dir), "%s\\assets", prof->game_root);
            if (!prof->save_dir[0])   snprintf(prof->save_dir, sizeof(prof->save_dir), "%s\\save_data", prof->game_root);
            if (!prof->mod_dir[0])    snprintf(prof->mod_dir, sizeof(prof->mod_dir), "%s\\mods\\textures", prof->game_root);
            if (prof->aux_so_count == 0) {
                collect_sibling_aux_sos(prof->so_path, prof->aux_so_paths, &prof->aux_so_count);
            }
            prof->elf_machine = elf_probe_machine(prof->so_path, NULL, 0);
            normalize_slashes(prof->game_root);
            normalize_slashes(prof->assets_dir);
            normalize_slashes(prof->so_path);
            normalize_slashes(prof->apk_path);
            normalize_slashes(prof->save_dir);
            normalize_slashes(prof->mod_dir);
            return 0;
        }
        if (prof->game_root[0]) {
            snprintf(full_target, sizeof(full_target), "%s", prof->game_root);
            len = strlen(full_target);
        }
    }

    /* Case 2: Direct .so file */
    if (len > 3 && _stricmp(full_target + len - 3, ".so") == 0 && file_exists(full_target)) {
        snprintf(prof->so_path, sizeof(prof->so_path), "%s", full_target);
        collect_sibling_aux_sos(prof->so_path, prof->aux_so_paths, &prof->aux_so_count);
        if (!prof->game_root[0]) {
            char parent1[512], parent2[512], parent3[512];
            get_parent_dir(full_target, parent1, sizeof(parent1));
            get_parent_dir(parent1, parent2, sizeof(parent2));
            get_parent_dir(parent2, parent3, sizeof(parent3));
            if (dir_exists(parent3)) snprintf(prof->game_root, sizeof(prof->game_root), "%s", parent3);
            else snprintf(prof->game_root, sizeof(prof->game_root), "%s", parent1);
        }
    }
    /* Case 3: Direct .apk file */
    else if (len > 4 && _stricmp(full_target + len - 4, ".apk") == 0 && file_exists(full_target)) {
        snprintf(prof->apk_path, sizeof(prof->apk_path), "%s", full_target);
        char parent[512];
        get_parent_dir(full_target, parent, sizeof(parent));
        snprintf(prof->game_root, sizeof(prof->game_root), "%s", parent);

        char test_lib[512], test_apk_lib[512];
        snprintf(test_lib, sizeof(test_lib), "%s\\lib", parent);
        snprintf(test_apk_lib, sizeof(test_apk_lib), "%s\\apk\\lib", parent);
        if (!dir_exists(test_lib) && !dir_exists(test_apk_lib)) {
            char extract_dir[512];
            snprintf(extract_dir, sizeof(extract_dir), "%s\\apk", parent);
            extract_apk_to_dir(full_target, extract_dir);
        }
    }
    /* Case 4: Game folder */
    else if (dir_exists(full_target)) {
        snprintf(prof->game_root, sizeof(prof->game_root), "%s", full_target);
        if (!prof->apk_path[0]) {
            find_first_apk(full_target, prof->apk_path, sizeof(prof->apk_path));
        }
        /* If folder has an APK but no lib/ or apk/lib/, extract it automatically */
        char test_lib[512], test_apk_lib[512];
        snprintf(test_lib, sizeof(test_lib), "%s\\lib", full_target);
        snprintf(test_apk_lib, sizeof(test_apk_lib), "%s\\apk\\lib", full_target);
        if (!dir_exists(test_lib) && !dir_exists(test_apk_lib) && prof->apk_path[0]) {
            char extract_dir[512];
            snprintf(extract_dir, sizeof(extract_dir), "%s\\apk", full_target);
            extract_apk_to_dir(prof->apk_path, extract_dir);
        }
    } else {
        fprintf(stderr, "[Profile] Target path does not exist: %s\n", target_path);
        return -1;
    }

    /* Locate assets directory */
    if (!prof->assets_dir[0]) {
        char cand1[512], cand2[512];
        snprintf(cand1, sizeof(cand1), "%s\\assets", prof->game_root);
        snprintf(cand2, sizeof(cand2), "%s\\apk\\assets", prof->game_root);
        if (dir_exists(cand1))      snprintf(prof->assets_dir, sizeof(prof->assets_dir), "%s", cand1);
        else if (dir_exists(cand2)) snprintf(prof->assets_dir, sizeof(prof->assets_dir), "%s", cand2);
        else                        snprintf(prof->assets_dir, sizeof(prof->assets_dir), "%s", prof->game_root);
    }

    /* Check for Unity Managed Assembly-CSharp.dll */
    const char *unity_rel_paths[] = {
        "assets\\bin\\Data\\Managed\\Assembly-CSharp.dll",
        "apk\\assets\\bin\\Data\\Managed\\Assembly-CSharp.dll"
    };
    for (int i = 0; i < 2; i++) {
        char ucand[512];
        snprintf(ucand, sizeof(ucand), "%s\\%s", prof->game_root, unity_rel_paths[i]);
        if (file_exists(ucand)) {
            prof->has_unity_dll = 1;
            snprintf(prof->unity_dll_path, sizeof(prof->unity_dll_path), "%s", ucand);
            normalize_slashes(prof->unity_dll_path);
            break;
        }
    }

    /* Locate save & mod directories */
    if (!prof->save_dir[0])
        snprintf(prof->save_dir, sizeof(prof->save_dir), "%s\\save_data", prof->game_root);
    if (!prof->mod_dir[0])
        snprintf(prof->mod_dir, sizeof(prof->mod_dir), "%s\\mods\\textures", prof->game_root);

    /* Locate primary .so: prefer x86 first, then fall back to armeabi-v7a / armeabi */
    if (!prof->so_path[0]) {
        const char *abi_search_order[] = {
            "lib\\x86",
            "apk\\lib\\x86",
            ".",
            "lib\\armeabi-v7a",
            "apk\\lib\\armeabi-v7a",
            "lib\\armeabi",
            "apk\\lib\\armeabi"
        };
        for (int i = 0; i < 7; i++) {
            char dir[512];
            snprintf(dir, sizeof(dir), "%s\\%s", prof->game_root, abi_search_order[i]);
            if (dir_exists(dir)) {
                prof->aux_so_count = 0;
                if (scan_so_dir(dir, prof->so_path, sizeof(prof->so_path),
                                prof->aux_so_paths, &prof->aux_so_count) > 0) {
                    break;
                }
            }
        }
    }

    if (prof->so_path[0]) {
        prof->elf_machine = elf_probe_machine(prof->so_path, NULL, 0);
    }

    /* Set default title from folder or .so name if still generic */
    if (strcmp(prof->title, "Android Native Port") == 0 && prof->so_path[0]) {
        const char *b1 = strrchr(prof->so_path, '\\');
        const char *b2 = strrchr(prof->so_path, '/');
        const char *base = (b1 > b2) ? b1 + 1 : (b2 ? b2 + 1 : prof->so_path);
        snprintf(prof->title, sizeof(prof->title), "Native Port - %s", base);
    }

    normalize_slashes(prof->game_root);
    normalize_slashes(prof->assets_dir);
    normalize_slashes(prof->so_path);
    normalize_slashes(prof->apk_path);
    normalize_slashes(prof->save_dir);
    normalize_slashes(prof->mod_dir);
    for (int i = 0; i < prof->aux_so_count; i++) {
        normalize_slashes(prof->aux_so_paths[i]);
    }

    return prof->so_path[0] ? 0 : -1;
}

game_backend_t profile_detect_backend(const elf_module_t *mod) {
    if (!mod) return BACKEND_NDK;
    if (strstr(mod->name, "libunity.so")) {
        return BACKEND_UNITY;
    }
    if (elf_sym(mod, "android_main") ||
        elf_sym(mod, "ANativeActivity_onCreate")) {
        return BACKEND_NDK;
    }
    if (elf_find_sym_match(mod, "Java_com_polarbit_", 1, NULL, 0) ||
        elf_find_sym_match(mod, "FuseOnInit", 0, NULL, 0) ||
        elf_find_sym_match(mod, "_OnEvent", 0, NULL, 0)) {
        return BACKEND_POLARBIT_FUSE;
    }
    if (elf_find_sym_match(mod, "Java_org_cocos2dx_lib_", 1, NULL, 0) ||
        elf_find_sym_match(mod, "Cocos2dxRenderer", 0, NULL, 0) ||
        elf_sym(mod, "cocos_android_app_init")) {
        return BACKEND_COCOS2DX;
    }
    if (elf_sym(mod, "SDL_main") ||
        elf_find_sym_match(mod, "Java_org_libsdl_app_SDLActivity_", 1, NULL, 0)) {
        return BACKEND_SDL2_ANDROID;
    }
    return BACKEND_GENERIC_JNI;
}

int profile_scan_target(const char *target_path) {
    printf("================================================================\n");
    printf(" Universal Native Port Engine — APK & Game Portability Scanner\n");
    printf("================================================================\n");
    printf("Target      : %s\n", target_path);

    game_profile_t prof;
    profile_init_defaults(&prof);
    int resolved = profile_resolve_target(&prof, target_path);

    printf("Game Root   : %s\n", prof.game_root[0] ? prof.game_root : "(none)");
    printf("Assets Dir  : %s\n", prof.assets_dir[0] ? prof.assets_dir : "(none)");
    printf("APK Archive : %s\n", prof.apk_path[0] ? prof.apk_path : "(none)");
    if (prof.has_unity_dll) {
        printf("Unity C# DLL: %s\n", prof.unity_dll_path);
    }

    /* Check architectures present in lib/ or apk/lib/ */
    const char *abi_dirs[][2] = {
        { "x86 (32-bit Native)",   "lib\\x86" },
        { "x86 (APK extracted)",   "apk\\lib\\x86" },
        { "armeabi-v7a (ARM32)",   "lib\\armeabi-v7a" },
        { "armeabi-v7a (APK)",     "apk\\lib\\armeabi-v7a" },
        { "armeabi (ARMv5/v6)",    "lib\\armeabi" },
        { "armeabi (APK)",         "apk\\lib\\armeabi" },
        { "arm64-v8a (ARM64)",     "lib\\arm64-v8a" },
        { "arm64-v8a (APK)",       "apk\\lib\\arm64-v8a" }
    };

    printf("\n--- Discovered ABI Directories ---\n");
    int any_abi = 0;
    for (size_t i = 0; i < sizeof(abi_dirs) / sizeof(abi_dirs[0]); i++) {
        char d[512];
        snprintf(d, sizeof(d), "%s\\%s", prof.game_root, abi_dirs[i][1]);
        if (dir_exists(d)) {
            char main_so[512] = {0};
            char aux_sos[MAX_AUX_LIBS][512];
            int aux_cnt = 0;
            int cnt = scan_so_dir(d, main_so, sizeof(main_so), aux_sos, &aux_cnt);
            printf("  [FOUND] %-22s -> %s (%d game .so, %d aux .so)\n",
                   abi_dirs[i][0], d, cnt, aux_cnt);
            any_abi = 1;
        }
    }
    if (!any_abi && prof.so_path[0]) {
        printf("  [FOUND] Direct .so binary     -> %s\n", prof.so_path);
    }

    if (resolved != 0 || !prof.so_path[0]) {
        printf("\n--- Portability Verdict ---\n");
        printf("  Status : NOT PORTABLE (No Android .so or Unity Managed DLL found in '%s')\n", target_path);
        printf("================================================================\n");
        return 1;
    }

    char arch_name[64] = "Unknown";
    int mach = elf_probe_machine(prof.so_path, arch_name, sizeof(arch_name));

    printf("\nPrimary .so : %s\n", prof.so_path);
    for (int i = 0; i < prof.aux_so_count; i++) {
        printf("Aux .so [%d] : %s\n", i, prof.aux_so_paths[i]);
    }
    printf("ELF Machine : %s (e_machine=%d)\n", arch_name, mach);

    if (mach != 3 && mach != 40) {
        printf("\n--- Portability Verdict ---\n");
        printf("  Status : UNSUPPORTED ELF ARCHITECTURE (%s, e_machine=%d)\n", arch_name, mach);
        printf("           Engine supports 32-bit x86 (EM_386=3) and 32-bit ARM (EM_ARM=40).\n");
        printf("================================================================\n");
        return 1;
    }

    size_t sym_count = 0;
    const host_symbol_t *syms = shims_get_table(&sym_count);

    /* Load auxiliary libraries first, then primary .so */
    static elf_module_t aux_mods[MAX_AUX_LIBS];
    for (int i = 0; i < prof.aux_so_count; i++) {
        if (elf_load(prof.aux_so_paths[i], &aux_mods[i]) == 0) {
            elf_register_module(&aux_mods[i]);
        }
    }

    static elf_module_t main_mod;
    if (elf_load(prof.so_path, &main_mod) != 0) {
        printf("ERROR: Failed to load ELF '%s'\n", prof.so_path);
        return 1;
    }
    elf_register_module(&main_mod);

    elf_print_summary(&main_mod);

    game_backend_t backend = profile_detect_backend(&main_mod);
    if (prof.has_unity_dll) backend = BACKEND_UNITY;

    printf("\n--- Shim & Relocation Coverage Analysis ---\n");
    int total_relocs = 0, resolved_relocs = 0, missing_relocs = 0;
    elf_analyze_relocations(&main_mod, syms, sym_count, &total_relocs, &resolved_relocs, &missing_relocs);
    double pct = (total_relocs > 0) ? (100.0 * (double)resolved_relocs / (double)total_relocs) : 100.0;
    printf("  Relocations Resolved : %d / %d (%.2f%%)\n", resolved_relocs, total_relocs, pct);

    printf("\n--- PORTABILITY VERDICT ---\n");
    printf("  Detected Framework : %s\n", profile_backend_name(backend));

    if (backend == BACKEND_UNITY) {
        printf("  Execution Mode     : UNITY 3D NATIVE WINDOWS PIPELINE (AssetRipper + C# Shims)\n");
        printf("  Verdict            : [YES — CAN PORT TO NATIVE WINDOWS EXECUTABLE]\n");
        printf("  How It Ports       : Extracts Unity assets & Managed/Assembly-CSharp.dll,\n");
        printf("                       injects Windows Unity compatibility shims, and builds\n");
        printf("                       a standalone native Windows .exe (same pipeline as GT Ride).\n");
    } else if (mach == 3) {
        printf("  Execution Mode     : DIRECT ZERO-EMULATION X86 NATIVE EXECUTION\n");
        if (missing_relocs == 0) {
            printf("  Verdict            : [YES — 100%% READY FOR NATIVE WINDOWS EXECUTION]\n");
        } else {
            printf("  Verdict            : [YES — PORTABLE WITH %d AUTO-STUBBED SYMBOLS]\n", missing_relocs);
        }
        printf("  HQ Enhancements    : 1280x720+ Resolution, 4x MSAA, 16x Anisotropic, Live Texture Mods\n");
    } else if (mach == 40) {
        printf("  Execution Mode     : ARM32 (armeabi-v7a) ELF LOADER + NATIVE WINDOWS SHIM BRIDGE\n");
        printf("  Verdict            : [YES — ARM32 BINARY LOADED & BRIDGED TO NATIVE WINDOWS SHIMS]\n");
        printf("  HQ Enhancements    : 1280x720+ Resolution, 4x MSAA, 16x Anisotropic, Live Texture Mods\n");
    }

    printf("\n--- Next Step Commands (PowerShell) ---\n");
    printf("  Generate Standalone Port : .\\native-port-engine.exe --port \"%s\"\n", target_path);
    if (backend != BACKEND_UNITY) {
        printf("  Run Directly Right Now   : .\\native-port-engine.exe \"%s\"%s\n",
               target_path, (missing_relocs > 0) ? " --allow-missing-syms" : "");
    }
    printf("================================================================\n");
    return 0;
}

int profile_inspect_target(const char *target_path) {
    return profile_scan_target(target_path);
}

/* Helper to derive a clean folder slug from a target path */
static void derive_game_slug(const char *target_path, char *slug_out, size_t slug_sz) {
    char temp[512];
    snprintf(temp, sizeof(temp), "%s", target_path);
    size_t l = strlen(temp);
    while (l > 0 && (temp[l - 1] == '\\' || temp[l - 1] == '/')) temp[--l] = '\0';

    const char *b1 = strrchr(temp, '\\');
    const char *b2 = strrchr(temp, '/');
    const char *base = (b1 > b2) ? b1 + 1 : (b2 ? b2 + 1 : temp);

    char raw[256];
    snprintf(raw, sizeof(raw), "%s", base);
    /* Strip .apk / .so / .ini extension */
    char *dot = strrchr(raw, '.');
    if (dot && (_stricmp(dot, ".apk") == 0 || _stricmp(dot, ".so") == 0 || _stricmp(dot, ".ini") == 0)) {
        *dot = '\0';
    }
    /* Sanitize characters and collapse repeated hyphens */
    size_t out_i = 0;
    for (char *p = raw; *p && out_i + 1 < slug_sz; p++) {
        char c = *p;
        if (c == ' ' || c == '+' || c == '_' || !isalnum((unsigned char)c)) c = '-';
        else c = (char)tolower((unsigned char)c);
        if (c == '-' && (out_i == 0 || slug_out[out_i - 1] == '-')) continue;
        slug_out[out_i++] = c;
    }
    while (out_i > 0 && slug_out[out_i - 1] == '-') out_i--;
    slug_out[out_i] = '\0';
}

int profile_port_target(const char *target_path, const char *out_dir_override) {
    printf("================================================================\n");
    printf(" Universal Native Port Engine — Automated Port Generator\n");
    printf("================================================================\n");

    game_profile_t src_prof;
    profile_init_defaults(&src_prof);
    if (profile_resolve_target(&src_prof, target_path) != 0) {
        fprintf(stderr, "[PortGen] ERROR: Could not resolve Android game target '%s'\n", target_path);
        return 1;
    }

    char slug[128];
    derive_game_slug(src_prof.apk_path[0] ? src_prof.apk_path : target_path, slug, sizeof(slug));

    char out_dir[512];
    if (out_dir_override && *out_dir_override) {
        if (!GetFullPathNameA(out_dir_override, sizeof(out_dir), out_dir, NULL)) {
            snprintf(out_dir, sizeof(out_dir), "%s", out_dir_override);
        }
    } else {
        char parent_of_root[512];
        get_parent_dir(src_prof.game_root, parent_of_root, sizeof(parent_of_root));
        snprintf(out_dir, sizeof(out_dir), "%s\\%s-port", parent_of_root, slug);
    }

    printf("[PortGen] Source Game Root : %s\n", src_prof.game_root);
    printf("[PortGen] Primary .so      : %s\n", src_prof.so_path);
    printf("[PortGen] Target Port Dir  : %s\n", out_dir);

    /* Create output port directory structure */
    CreateDirectoryA(out_dir, NULL);
    char out_save[512], out_mods[512], out_mods_tex[512], out_apk_dir[512];
    snprintf(out_save, sizeof(out_save), "%s\\save_data", out_dir);
    snprintf(out_mods, sizeof(out_mods), "%s\\mods", out_dir);
    snprintf(out_mods_tex, sizeof(out_mods_tex), "%s\\mods\\textures", out_dir);
    snprintf(out_apk_dir, sizeof(out_apk_dir), "%s\\apk", out_dir);
    CreateDirectoryA(out_save, NULL);
    CreateDirectoryA(out_mods, NULL);
    CreateDirectoryA(out_mods_tex, NULL);

    /* If source has an APK, copy it into out_dir and extract into out_dir\apk */
    char dest_apk_path[512] = {0};
    if (src_prof.apk_path[0] && file_exists(src_prof.apk_path)) {
        const char *b1 = strrchr(src_prof.apk_path, '\\');
        const char *b2 = strrchr(src_prof.apk_path, '/');
        const char *apk_base = (b1 > b2) ? b1 + 1 : (b2 ? b2 + 1 : src_prof.apk_path);
        snprintf(dest_apk_path, sizeof(dest_apk_path), "%s\\%s", out_dir, apk_base);
        if (_stricmp(src_prof.apk_path, dest_apk_path) != 0) {
            printf("[PortGen] Copying APK -> %s\n", dest_apk_path);
            CopyFileA(src_prof.apk_path, dest_apk_path, FALSE);
        }
        char check_lib[512];
        snprintf(check_lib, sizeof(check_lib), "%s\\lib", out_apk_dir);
        if (!dir_exists(check_lib)) {
            extract_apk_to_dir(dest_apk_path, out_apk_dir);
        }
    } else {
        /* Copy lib/ and assets/ from source folder using xcopy */
        char cmd[1536];
        char src_lib[512];
        snprintf(src_lib, sizeof(src_lib), "%s\\lib", src_prof.game_root);
        if (dir_exists(src_lib)) {
            snprintf(cmd, sizeof(cmd), "xcopy /E /I /Y \"%s\\lib\" \"%s\\lib\" >nul", src_prof.game_root, out_dir);
            system(cmd);
        }
        char src_assets[512];
        snprintf(src_assets, sizeof(src_assets), "%s\\assets", src_prof.game_root);
        if (dir_exists(src_assets)) {
            snprintf(cmd, sizeof(cmd), "xcopy /E /I /Y \"%s\\assets\" \"%s\\assets\" >nul", src_prof.game_root, out_dir);
            system(cmd);
        }
    }

    /* Copy native-port-engine.exe and SDL2.dll into the generated port folder */
    char exe_path[512] = {0};
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    char exe_dir[512];
    get_parent_dir(exe_path, exe_dir, sizeof(exe_dir));

    char dest_exe[512], src_sdl[512], dest_sdl[512];
    snprintf(dest_exe, sizeof(dest_exe), "%s\\native-port-engine.exe", out_dir);
    snprintf(src_sdl,  sizeof(src_sdl),  "%s\\SDL2.dll", exe_dir);
    snprintf(dest_sdl, sizeof(dest_sdl), "%s\\SDL2.dll", out_dir);
    CopyFileA(exe_path, dest_exe, FALSE);
    if (file_exists(src_sdl)) {
        CopyFileA(src_sdl, dest_sdl, FALSE);
    }

    /* Load primary .so to detect backend and missing symbol status */
    size_t sym_count = 0;
    const host_symbol_t *syms = shims_get_table(&sym_count);
    static elf_module_t aux_mods[MAX_AUX_LIBS];
    for (int i = 0; i < src_prof.aux_so_count; i++) {
        if (elf_load(src_prof.aux_so_paths[i], &aux_mods[i]) == 0) {
            elf_register_module(&aux_mods[i]);
        }
    }
    static elf_module_t main_mod;
    game_backend_t backend = BACKEND_AUTO;
    int missing_relocs = 0;
    if (elf_load(src_prof.so_path, &main_mod) == 0) {
        elf_register_module(&main_mod);
        backend = profile_detect_backend(&main_mod);
        int total_relocs = 0, resolved_relocs = 0;
        elf_analyze_relocations(&main_mod, syms, sym_count, &total_relocs, &resolved_relocs, &missing_relocs);
    }
    if (src_prof.has_unity_dll) backend = BACKEND_UNITY;

    /* Resolve paths inside the newly created port folder */
    game_profile_t port_prof;
    profile_init_defaults(&port_prof);
    profile_resolve_target(&port_prof, out_dir);

    /* Write game_profile.ini inside out_dir */
    char ini_path[512];
    snprintf(ini_path, sizeof(ini_path), "%s\\game_profile.ini", out_dir);
    FILE *fini = fopen(ini_path, "w");
    if (fini) {
        fprintf(fini, "# Auto-generated by Universal Native Port Engine\n");
        fprintf(fini, "title              = %s (%s)\n", slug, profile_backend_name(backend));
        fprintf(fini, "backend            = %s\n", backend_short_key(backend));
        fprintf(fini, "game_root          = .\n");
        if (port_prof.so_path[0]) {
            /* Write relative path if inside out_dir */
            fprintf(fini, "so_path            = %s\n", port_prof.so_path);
        }
        if (port_prof.assets_dir[0]) {
            fprintf(fini, "assets_dir         = %s\n", port_prof.assets_dir);
        }
        if (port_prof.apk_path[0]) {
            fprintf(fini, "apk_path           = %s\n", port_prof.apk_path);
        }
        fprintf(fini, "save_dir           = save_data\n");
        fprintf(fini, "mod_dir            = mods/textures\n");
        fprintf(fini, "width              = 1280\n");
        fprintf(fini, "height             = 720\n");
        fprintf(fini, "fullscreen         = 0\n");
        fprintf(fini, "msaa               = 4\n");
        fprintf(fini, "anisotropy         = 16\n");
        fprintf(fini, "auto_mipmaps       = 1\n");
        fprintf(fini, "allow_missing_syms = %d\n", (missing_relocs > 0 || src_prof.elf_machine == 40) ? 1 : 0);
        fclose(fini);
        printf("[PortGen] Wrote configuration profile -> %s\n", ini_path);
    }

    /* Write run_port.bat */
    char bat_path[512];
    snprintf(bat_path, sizeof(bat_path), "%s\\run_port.bat", out_dir);
    FILE *fbat = fopen(bat_path, "w");
    if (fbat) {
        fprintf(fbat, "@echo off\n");
        fprintf(fbat, "cd /d \"%%~dp0\"\n");
        fprintf(fbat, "echo Launching Standalone Windows Native Port (%s)...\n", slug);
        fprintf(fbat, "native-port-engine.exe --profile game_profile.ini %%*\n");
        fclose(fbat);
        printf("[PortGen] Wrote launcher script       -> %s\n", bat_path);
    }

    /* Write dump_hq_assets.bat */
    char dump_bat[512];
    snprintf(dump_bat, sizeof(dump_bat), "%s\\dump_hq_assets.bat", out_dir);
    FILE *fdump = fopen(dump_bat, "w");
    if (fdump) {
        fprintf(fdump, "@echo off\n");
        fprintf(fdump, "cd /d \"%%~dp0\"\n");
        fprintf(fdump, "echo Running with 3D Mesh (.obj/.mtl) and Texture (.tga) Exporter enabled...\n");
        fprintf(fdump, "native-port-engine.exe --profile game_profile.ini --dump-meshes %%*\n");
        fclose(fdump);
    }

    /* If Unity 3D game, also generate port_unity_windows.ps1 */
    if (backend == BACKEND_UNITY) {
        char ps1_path[512];
        snprintf(ps1_path, sizeof(ps1_path), "%s\\port_unity_windows.ps1", out_dir);
        FILE *fps1 = fopen(ps1_path, "w");
        if (fps1) {
            fprintf(fps1, "# Automated Unity APK -> Native Windows Executable Pipeline\n");
            fprintf(fps1, "$ErrorActionPreference = 'Stop'\n");
            fprintf(fps1, "$PortRoot = $PSScriptRoot\n");
            fprintf(fps1, "$ApkDir   = Join-Path $PortRoot 'apk'\n");
            fprintf(fps1, "$UnityRef = Resolve-Path (Join-Path $PortRoot '..\\gt-ride') -ErrorAction SilentlyContinue\n");
            fprintf(fps1, "Write-Host \"[UnityPort] Extracted APK Data: $ApkDir\\assets\\bin\\Data\"\n");
            fprintf(fps1, "Write-Host \"[UnityPort] Managed C# Assembly: $ApkDir\\assets\\bin\\Data\\Managed\\Assembly-CSharp.dll\"\n");
            fprintf(fps1, "if ($UnityRef) {\n");
            fprintf(fps1, "    $CompatDir = Join-Path $UnityRef 'GTRideWin\\Assets\\Scripts\\Compat'\n");
            fprintf(fps1, "    $NewCompat = Join-Path $PortRoot 'UnityWinProject\\Assets\\Scripts\\Compat'\n");
            fprintf(fps1, "    New-Item -ItemType Directory -Force -Path $NewCompat | Out-Null\n");
            fprintf(fps1, "    Copy-Item \"$CompatDir\\*.cs\" $NewCompat -Force\n");
            fprintf(fps1, "    Write-Host \"[UnityPort] Copied Unity Windows C# compatibility shims -> $NewCompat\"\n");
            fprintf(fps1, "}\n");
            fprintf(fps1, "Write-Host \"[UnityPort] Ready for AssetRipper export + Unity 2021.3 Windows Standalone build.\"\n");
            fclose(fps1);
            printf("[PortGen] Wrote Unity pipeline script -> %s\n", ps1_path);
        }
    }

    printf("\n================================================================\n");
    printf(" PORT GENERATION COMPLETE!\n");
    printf("   Output Directory : %s\n", out_dir);
    printf("   Backend          : %s\n", profile_backend_name(backend));
    printf("   To Play (PS)     : & \"%s\\run_port.bat\"\n", out_dir);
    printf("================================================================\n");
    return 0;
}
