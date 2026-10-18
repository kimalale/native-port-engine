#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "elf_loader.h"
#include "arm32_bridge.h"
#include "shims.h"
#include "gl_wrap.h"
#include "egl_wrap.h"
#include "mesh_dump.h"
#include "android_ndk.h"
#include "fake_jni.h"
#include "opensles.h"
#include "watchdog.h"
#include "log.h"
#include "game_profile.h"
#include "minigore2_plugin.h"

/* ================================================================== */
/* Unified x86 / ARM32 Function Invoker                               */
/* ================================================================== */
static uint32_t invoke_mod_fn(const elf_module_t *mod, void *fn, int argc, const uint32_t *args) {
    if (!fn) return 0;
    uint32_t a[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) a[i] = args[i];
    if (mod && mod->machine == 40) {
        return arm32_call((uintptr_t)fn, argc, a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    typedef uint32_t (*fn0_t)(void);
    typedef uint32_t (*fn1_t)(uint32_t);
    typedef uint32_t (*fn2_t)(uint32_t, uint32_t);
    typedef uint32_t (*fn3_t)(uint32_t, uint32_t, uint32_t);
    typedef uint32_t (*fn4_t)(uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (*fn5_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (*fn6_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (*fn7_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    typedef uint32_t (*fn8_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    switch (argc) {
        case 0: return ((fn0_t)fn)();
        case 1: return ((fn1_t)fn)(a[0]);
        case 2: return ((fn2_t)fn)(a[0], a[1]);
        case 3: return ((fn3_t)fn)(a[0], a[1], a[2]);
        case 4: return ((fn4_t)fn)(a[0], a[1], a[2], a[3]);
        case 5: return ((fn5_t)fn)(a[0], a[1], a[2], a[3], a[4]);
        case 6: return ((fn6_t)fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
        case 7: return ((fn7_t)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        default: return ((fn8_t)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
}

/* ================================================================== */
/* Windows SEH Stack Guard & Crash Reporter                           */
/* ================================================================== */
static elf_module_t *g_primary_mod = NULL;

/* Linux x86 GCC functions with >4KB stack frames subtract from ESP and
 * touch the bottom without calling Windows' _chkstk, skipping the 4KB
 * guard page. Catch those faults and commit the intermediate pages. */
static LONG WINAPI stack_commit_handler(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault_addr = ep->ExceptionRecord->ExceptionInformation[1];
        MEMORY_BASIC_INFORMATION mbi, own_stack;
        if (VirtualQuery((void *)fault_addr, &mbi, sizeof(mbi)) &&
            VirtualQuery(&own_stack, &own_stack, sizeof(own_stack)) &&
            mbi.AllocationBase == own_stack.AllocationBase &&
            mbi.State == MEM_RESERVE) {
            void *page = (void *)(fault_addr & ~(ULONG_PTR)0xFFF);
            VirtualAlloc(page, 64 * 1024, MEM_COMMIT, PAGE_READWRITE);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if ((code & 0xC0000000UL) != 0xC0000000UL) return EXCEPTION_CONTINUE_SEARCH;

    uint32_t eip = ep->ContextRecord->Eip;
    fprintf(stderr, "\n=== NATIVE PORT ENGINE CRASH ===\n");
    fprintf(stderr, "Exception 0x%08lX at EIP=0x%08X (ESP=0x%08lX EBP=0x%08lX)\n",
            code, eip, ep->ContextRecord->Esp, ep->ContextRecord->Ebp);
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        fprintf(stderr, "Fault %s address 0x%08lX\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "WRITING" : "READING",
                ep->ExceptionRecord->ExceptionInformation[1]);
    }
    if (g_primary_mod && eip >= (uint32_t)g_primary_mod->base &&
        eip < (uint32_t)g_primary_mod->base + g_primary_mod->size) {
        char desc[128];
        watchdog_describe_address(eip, desc, sizeof(desc));
        fprintf(stderr, "Inside %s: %s\n", g_primary_mod->name, desc);
    }
    {
        char actx[320];
        arm32_describe_context(actx, sizeof(actx));
        if (actx[0]) fprintf(stderr, "ARM32: %s\n", actx);
    }
    log_flush();
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ================================================================== */
/* Shared SDL2 Window / Input Helpers                                 */
/* ================================================================== */
static SDL_Window *create_engine_window(const game_profile_t *prof) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    if (prof->msaa > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, prof->msaa);
    }

    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (prof->fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    SDL_Window *window = SDL_CreateWindow(
        prof->title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        prof->width, prof->height,
        win_flags
    );
    if (!window && prof->msaa > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow(
            prof->title,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            prof->width, prof->height,
            win_flags
        );
    }
    return window;
}

static void toggle_window_fullscreen(SDL_Window *win, int *is_fullscreen) {
    if (!win) return;
    *is_fullscreen = !(*is_fullscreen);
    SDL_SetWindowFullscreen(win, *is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static SDL_GameController *open_first_controller(void) {
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController *pad = SDL_GameControllerOpen(i);
            if (pad) {
                printf("[Input] Opened controller: %s\n", SDL_GameControllerName(pad));
                return pad;
            }
        }
    }
    return NULL;
}

static float normalize_axis(Sint16 v, float deadzone) {
    float f = (float)v / 32767.0f;
    if (f < -1.0f) f = -1.0f;
    if (f >  1.0f) f =  1.0f;
    if (fabsf(f) < deadzone) return 0.0f;
    float sign = (f < 0.0f) ? -1.0f : 1.0f;
    return sign * ((fabsf(f) - deadzone) / (1.0f - deadzone));
}

static void push_single_touch(int action, int pointer_id, float x, float y) {
    android_app_push_touch(action, 1, &pointer_id, &x, &y);
}

/* ================================================================== */
/* Backend 1: Android NDK (ANativeActivity / android_main)            */
/* ================================================================== */
typedef void (*android_main_fn)(struct android_app *app);

typedef struct {
    elf_module_t       *mod;
    android_main_fn     entry;
    struct android_app *app;
} ndk_thread_args_t;

static DWORD WINAPI ndk_game_thread(LPVOID param) {
    ndk_thread_args_t *args = (ndk_thread_args_t *)param;

    /* Commit entire thread stack upfront so large Linux stack frames never fault */
    MEMORY_BASIC_INFORMATION mbi = {0};
    char *p = (char *)&mbi;
    if (VirtualQuery(&p, &mbi, sizeof(mbi)) && mbi.AllocationBase) {
        SIZE_T size_to_commit = (SIZE_T)(p - (char *)mbi.AllocationBase);
        VirtualAlloc(mbi.AllocationBase, size_to_commit, MEM_COMMIT, PAGE_READWRITE);
    }

    printf("[NDK] Starting android_main at %p...\n", (void *)args->entry);
    fflush(stdout);
    uint32_t call_args[1] = { (uint32_t)(uintptr_t)args->app };
    invoke_mod_fn(args->mod, (void *)args->entry, 1, call_args);
    printf("[NDK] android_main returned.\n");
    fflush(stdout);
    return 0;
}

static int run_ndk_backend(elf_module_t *mod, const game_profile_t *prof, SDL_Window *window) {
    android_main_fn entry = (android_main_fn)elf_sym(mod, "android_main");
    if (!entry) {
        fprintf(stderr, "[NDK] ERROR: 'android_main' symbol not found in %s\n", mod->name);
        return 1;
    }

    int win_w = prof->width, win_h = prof->height;
    SDL_GetWindowSize(window, &win_w, &win_h);

    /* Install optional game-specific plugins (e.g. Minigore 2 hooks) */
    if (mod->machine == 3) {
        minigore2_plugin_install(mod);
    }

    struct android_app *app = android_app_get();
    watchdog_start(mod);
    watchdog_register_thread(GetCurrentThread(), "main");

    /* Queue initial Android lifecycle commands BEFORE starting android_main thread */
    android_app_push_cmd(APP_CMD_START);
    android_app_push_cmd(APP_CMD_INIT_WINDOW);
    android_app_push_cmd(APP_CMD_RESUME);
    android_app_push_cmd(APP_CMD_GAINED_FOCUS);

    ndk_thread_args_t targs = { mod, entry, app };
    HANDLE hThread = CreateThread(NULL, 16 * 1024 * 1024, ndk_game_thread, &targs, 0, NULL);
    if (!hThread) {
        fprintf(stderr, "[NDK] ERROR: Failed to create game thread\n");
        return 1;
    }
    watchdog_register_thread(hThread, "game");

    SDL_GameController *pad = open_first_controller();

    int running       = 1;
    int is_fullscreen = prof->fullscreen;
    int mouse_down    = 0;
    int mouse_x       = win_w / 2;
    int mouse_y       = win_h / 2;

    /* Virtual touch dual-stick state (pointer 0 = move, pointer 1 = aim, pointer 2 = UI tap) */
    int vmove_active = 0;
    int vaim_active  = 0;
    int vui_active   = 0;
    float cursor_x   = win_w * 0.5f;
    float cursor_y   = win_h * 0.5f;
    int show_cursor  = 0;

    while (running) {
        if (WaitForSingleObject(hThread, 0) == WAIT_OBJECT_0 ||
            android_app_is_destroy_requested()) {
            printf("[NDK] Game thread exited.\n");
            break;
        }

        SDL_GetWindowSize(window, &win_w, &win_h);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = 0;
                    break;

                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        win_w = ev.window.data1;
                        win_h = ev.window.data2;
                        android_app_set_window_size(win_w, win_h);
                        android_app_push_cmd(APP_CMD_WINDOW_RESIZED);
                    } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                        watchdog_set_focused(1);
                        android_app_push_cmd(APP_CMD_GAINED_FOCUS);
                    } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        watchdog_set_focused(0);
                        android_app_push_cmd(APP_CMD_LOST_FOCUS);
                    }
                    break;

                case SDL_CONTROLLERDEVICEADDED:
                    if (!pad) pad = open_first_controller();
                    break;

                case SDL_CONTROLLERDEVICEREMOVED:
                    if (pad && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                        SDL_GameControllerClose(pad);
                        pad = open_first_controller();
                    }
                    break;

                case SDL_KEYDOWN:
                    if (ev.key.repeat) break;
                    if (ev.key.keysym.sym == SDLK_F11 ||
                        (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))) {
                        toggle_window_fullscreen(window, &is_fullscreen);
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        android_app_push_key_event(AKEY_EVENT_ACTION_DOWN, AKEYCODE_BACK);
                        android_app_push_key_event(AKEY_EVENT_ACTION_UP,   AKEYCODE_BACK);
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        mouse_down = 1;
                        mouse_x = ev.button.x;
                        mouse_y = ev.button.y;
                        show_cursor = 0;
                        if (!minigore2_in_level() || prof->force_touch_sticks) {
                            push_single_touch(AMOTION_EVENT_ACTION_DOWN, 2, (float)mouse_x, (float)mouse_y);
                        }
                    }
                    break;

                case SDL_MOUSEMOTION:
                    mouse_x = ev.motion.x;
                    mouse_y = ev.motion.y;
                    if (mouse_down && (!minigore2_in_level() || prof->force_touch_sticks)) {
                        push_single_touch(AMOTION_EVENT_ACTION_MOVE, 2, (float)mouse_x, (float)mouse_y);
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        mouse_down = 0;
                        mouse_x = ev.button.x;
                        mouse_y = ev.button.y;
                        if (!minigore2_in_level() || prof->force_touch_sticks) {
                            push_single_touch(AMOTION_EVENT_ACTION_UP, 2, (float)mouse_x, (float)mouse_y);
                        }
                    }
                    break;

                case SDL_CONTROLLERBUTTONDOWN:
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
                        ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        android_app_push_key_event(AKEY_EVENT_ACTION_DOWN, AKEYCODE_BACK);
                        android_app_push_key_event(AKEY_EVENT_ACTION_UP,   AKEYCODE_BACK);
                    } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        if (!minigore2_in_level()) {
                            vui_active = 1;
                            push_single_touch(AMOTION_EVENT_ACTION_DOWN, 2, cursor_x, cursor_y);
                        }
                    } else if (!minigore2_in_level()) {
                        int dx = 0, dy = 0;
                        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)  dx = -1;
                        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) dx =  1;
                        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)    dy = -1;
                        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)  dy =  1;
                        if (dx != 0 || dy != 0) {
                            float nx, ny;
                            if (minigore2_ui_nav_find(cursor_x, cursor_y, dx, dy, &nx, &ny)) {
                                cursor_x = nx;
                                cursor_y = ny;
                                show_cursor = 1;
                            }
                        }
                    }
                    break;

                case SDL_CONTROLLERBUTTONUP:
                    if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A && vui_active) {
                        vui_active = 0;
                        push_single_touch(AMOTION_EVENT_ACTION_UP, 2, cursor_x, cursor_y);
                    }
                    break;
            }
        }

        /* Gather movement & aim vectors from Keyboard + Mouse + Gamepad */
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        float move_x = 0.0f, move_y = 0.0f;
        float aim_x  = 0.0f, aim_y  = 0.0f;

        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  move_x -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move_x += 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    move_y -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  move_y += 1.0f;

        if (pad) {
            float lx = normalize_axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), 0.18f);
            float ly = normalize_axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY), 0.18f);
            float rx = normalize_axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX), 0.18f);
            float ry = normalize_axis(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY), 0.18f);

            if (!minigore2_in_level() && (fabsf(lx) > 0.05f || fabsf(ly) > 0.05f)) {
                cursor_x += lx * 10.0f;
                cursor_y += ly * 10.0f;
                if (cursor_x < 0.0f) cursor_x = 0.0f;
                if (cursor_y < 0.0f) cursor_y = 0.0f;
                if (cursor_x > (float)win_w) cursor_x = (float)win_w;
                if (cursor_y > (float)win_h) cursor_y = (float)win_h;
                show_cursor = 1;
            } else {
                if (fabsf(lx) > 0.01f || fabsf(ly) > 0.01f) { move_x = lx; move_y = ly; }
                if (fabsf(rx) > 0.01f || fabsf(ry) > 0.01f) { aim_x  = rx; aim_y  = ry; }
            }
        }

        float mlen = sqrtf(move_x * move_x + move_y * move_y);
        if (mlen > 1.0f) { move_x /= mlen; move_y /= mlen; }

        /* Mouse aim during gameplay */
        if (mouse_down && minigore2_in_level()) {
            float px = win_w * 0.5f, py = win_h * 0.5f;
            minigore2_player_screen_pos(&px, &py);
            float dx = (float)mouse_x - px;
            float dy = (float)mouse_y - py;
            float alen = sqrtf(dx * dx + dy * dy);
            if (alen > 4.0f) {
                aim_x = dx / alen;
                aim_y = dy / alen;
            }
        }

        /* Apply controls: either direct stick vector hook or virtual touch sticks */
        if (!prof->force_touch_sticks && minigore2_set_sticks(move_x, -move_y, aim_x, -aim_y)) {
            /* Direct engine vector injection succeeded */
        } else {
            /* Universal virtual touch dual-stick fallback */
            float mcx = prof->move_stick_x * win_w;
            float mcy = prof->move_stick_y * win_h;
            float mr  = prof->move_stick_r * win_h;
            if (fabsf(move_x) > 0.01f || fabsf(move_y) > 0.01f) {
                int act = vmove_active ? AMOTION_EVENT_ACTION_MOVE : AMOTION_EVENT_ACTION_DOWN;
                vmove_active = 1;
                push_single_touch(act, 0, mcx + move_x * mr, mcy + move_y * mr);
            } else if (vmove_active) {
                vmove_active = 0;
                push_single_touch(AMOTION_EVENT_ACTION_UP, 0, mcx, mcy);
            }

            float acx = prof->aim_stick_x * win_w;
            float acy = prof->aim_stick_y * win_h;
            float ar  = prof->aim_stick_r * win_h;
            if (fabsf(aim_x) > 0.01f || fabsf(aim_y) > 0.01f) {
                int act = vaim_active ? AMOTION_EVENT_ACTION_MOVE : AMOTION_EVENT_ACTION_DOWN;
                vaim_active = 1;
                push_single_touch(act, 1, acx + aim_x * ar, acy + aim_y * ar);
            } else if (vaim_active) {
                vaim_active = 0;
                push_single_touch(AMOTION_EVENT_ACTION_UP, 1, acx, acy);
            }
        }

        egl_overlay_set_cursor(cursor_x, cursor_y, show_cursor && !minigore2_in_level(), vui_active);
        SDL_Delay(4);
    }

    opensles_shutdown();
    egl_wrap_shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/* ================================================================== */
/* Backend 2 & 3: Polarbit Fuse & Generic JNI Game Loop               */
/* ================================================================== */
#define KEY_BACK        4
#define KEY_DPAD_UP     19
#define KEY_DPAD_DOWN   20
#define KEY_DPAD_LEFT   21
#define KEY_DPAD_RIGHT  22
#define KEY_DPAD_CENTER 23
#define KEY_BUTTON_X    99
#define KEY_BUTTON_Y    100
#define KEY_BUTTON_L1   102
#define KEY_BUTTON_R1   103

static SDL_Window *g_jni_window = NULL;

static void jni_swap_buffers_cb(void) {
    if (g_jni_window) {
        SDL_GL_SwapWindow(g_jni_window);
    }
}

static void *resolve_jni_entry(elf_module_t *mod, const char *exact_name,
                               const char *substr_match, const char *reg_name) {
    uintptr_t fn = elf_sym(mod, exact_name);
    if (fn) return (void *)fn;
    if (substr_match) {
        fn = elf_find_sym_match(mod, substr_match, 0, NULL, 0);
        if (fn) return (void *)fn;
    }
    if (reg_name) {
        void *rfn = fake_jni_find_registered_native(reg_name);
        if (rfn) return rfn;
    }
    return NULL;
}

static void call_jni_event(elf_module_t *mod, void *fn_event, jint type, jint p1, jint p2, jint p3, jint p4, int *out_rc) {
    uint32_t args[7] = {
        (uint32_t)(uintptr_t)&fake_jni, 0,
        (uint32_t)type, (uint32_t)p1, (uint32_t)p2, (uint32_t)p3, (uint32_t)p4
    };
    int rc = (int)invoke_mod_fn(mod, fn_event, 7, args);
    if (out_rc) *out_rc = rc;
}

static int run_jni_backend(elf_module_t *mod, const game_profile_t *prof,
                           SDL_Window *window, SDL_GLContext gl_ctx) {
    int surface_w = prof->width, surface_h = prof->height;
    SDL_GL_GetDrawableSize(window, &surface_w, &surface_h);

    void *fn_onload = (void *)elf_sym(mod, "JNI_OnLoad");
    if (fn_onload) {
        printf("[JNI] Calling JNI_OnLoad(%p)...\n", fn_onload);
        uint32_t ol_args[2] = { (uint32_t)(uintptr_t)&fake_jvm, 0 };
        jint ver = (jint)invoke_mod_fn(mod, fn_onload, 2, ol_args);
        printf("[JNI] JNI_OnLoad returned 0x%x\n", ver);
    }

    void *fn_init    = resolve_jni_entry(mod, "Java_com_polarbit_fuse_MainTask_FuseOnInit", "FuseOnInit", "FuseOnInit");
    void *fn_create  = resolve_jni_entry(mod, "Java_com_polarbit_fuse_Jni_OnCreate", "_OnCreate", "OnCreate");
    void *fn_event   = resolve_jni_entry(mod, "Java_com_polarbit_fuse_Jni_OnEvent", "_OnEvent", "OnEvent");
    void *fn_destroy = resolve_jni_entry(mod, "Java_com_polarbit_fuse_Jni_OnDestroy", "_OnDestroy", "OnDestroy");
    AudioMix_fn fn_audio = (AudioMix_fn)resolve_jni_entry(mod, "Java_com_polarbit_fuse_Jni_AudioMix", "AudioMix", "AudioMix");

    /* Generic GLSurfaceView.Renderer or EA Firemint/Mint3D JNI fallback if OnEvent is not present */
    void *fn_on_create_jni = resolve_jni_entry(mod, "onCreateJNI", "_onCreateJNI", "onCreateJNI");
    void *fn_on_start_jni  = resolve_jni_entry(mod, "onStartJNI",  "_onStartJNI",  "onStartJNI");
    void *fn_on_resume_jni = resolve_jni_entry(mod, "onResumeJNI", "_onResumeJNI", "onResumeJNI");

    void *fn_on_surface_created = resolve_jni_entry(mod, "onSurfaceCreated", "onSurfaceCreated", "onSurfaceCreated");
    if (!fn_on_surface_created)
        fn_on_surface_created = resolve_jni_entry(mod, "onViewCreatedJNI", "_onViewCreatedJNI", "onViewCreatedJNI");

    void *fn_on_surface_changed = resolve_jni_entry(mod, "onSurfaceChanged", "onSurfaceChanged", "onSurfaceChanged");
    if (!fn_on_surface_changed)
        fn_on_surface_changed = resolve_jni_entry(mod, "onViewChangedJNI", "_onViewChangedJNI", "onViewChangedJNI");

    void *fn_on_draw_frame = resolve_jni_entry(mod, "onDrawFrame", "onDrawFrame", "onDrawFrame");
    if (!fn_on_draw_frame)
        fn_on_draw_frame = resolve_jni_entry(mod, "onViewRenderJNI", "_onViewRenderJNI", "onViewRenderJNI");

    if (!fn_event && !fn_on_draw_frame) {
        fprintf(stderr, "[JNI] ERROR: Could not resolve JNI render/event entry point in %s\n", mod->name);
        return 1;
    }

    opensles_init();
    if (fn_audio && mod->machine == 3) {
        audio_set_jni_mixer(fn_audio);
    }

    static int activity_placeholder, maintask_placeholder, sensor_placeholder;
    static int utils_placeholder, audio_placeholder, egl_placeholder;

    printf("[JNI] Bootstrapping JNI engine...\n");
    if (fn_event) {
        call_jni_event(mod, fn_event, 1, 1, 0, 0, 0x10000, NULL);
        call_jni_event(mod, fn_event, 4, 0, 0, 0, 0, NULL);

        if (fn_init) {
            uint32_t init_args[8] = {
                (uint32_t)(uintptr_t)&fake_jni, 0,
                (uint32_t)(uintptr_t)&activity_placeholder,
                (uint32_t)(uintptr_t)&maintask_placeholder,
                (uint32_t)(uintptr_t)&sensor_placeholder,
                (uint32_t)(uintptr_t)&utils_placeholder,
                (uint32_t)(uintptr_t)&audio_placeholder,
                (uint32_t)(uintptr_t)&egl_placeholder
            };
            invoke_mod_fn(mod, fn_init, 8, init_args);
        }

        call_jni_event(mod, fn_event, 3, 0, surface_w, surface_h, 0, NULL);

        if (fn_create) {
            char save_slash[520];
            snprintf(save_slash, sizeof(save_slash), "%s/", prof->save_dir);
            CreateDirectoryA(prof->save_dir, NULL);
            jstring j_apk = fake_jni->NewStringUTF(&fake_jni, prof->apk_path);
            jstring j_sav = fake_jni->NewStringUTF(&fake_jni, save_slash);
            printf("[JNI] Calling OnCreate(apk='%s', save='%s')...\n", prof->apk_path, save_slash);
            uint32_t cr_args[4] = {
                (uint32_t)(uintptr_t)&fake_jni, 0,
                (uint32_t)(uintptr_t)j_apk, (uint32_t)(uintptr_t)j_sav
            };
            int rc = (int)invoke_mod_fn(mod, fn_create, 4, cr_args);
            printf("[JNI] OnCreate returned %d\n", rc);
        }

        call_jni_event(mod, fn_event, 1, 4, 1, 0, 0, NULL);
    } else {
        uint32_t act_args[3] = { (uint32_t)(uintptr_t)&fake_jni, (uint32_t)(uintptr_t)&activity_placeholder, 0 };
        if (fn_on_create_jni) {
            printf("[libRealRacing3] Calling onCreateJNI...\n");
            invoke_mod_fn(mod, fn_on_create_jni, 3, act_args);
        }
        if (fn_on_start_jni) {
            printf("[libRealRacing3] Calling onStartJNI...\n");
            invoke_mod_fn(mod, fn_on_start_jni,  2, act_args);
        }
        if (fn_on_resume_jni) {
            printf("[libRealRacing3] Calling onResumeJNI...\n");
            invoke_mod_fn(mod, fn_on_resume_jni, 2, act_args);
        }
        if (fn_on_surface_created) {
            printf("[libRealRacing3] Calling onViewCreatedJNI...\n");
            uint32_t sc_args[3] = { (uint32_t)(uintptr_t)&fake_jni, (uint32_t)(uintptr_t)&activity_placeholder, 0 };
            invoke_mod_fn(mod, fn_on_surface_created, 3, sc_args);
        }
        if (fn_on_surface_changed) {
            printf("[libRealRacing3] Calling onViewChangedJNI(%d, %d)...\n", surface_w, surface_h);
            uint32_t ch_args[5] = { (uint32_t)(uintptr_t)&fake_jni, (uint32_t)(uintptr_t)&activity_placeholder, (uint32_t)surface_w, (uint32_t)surface_h, 0 };
            invoke_mod_fn(mod, fn_on_surface_changed, 5, ch_args);
        }
    }

    if (fn_audio && mod->machine == 3) {
        audio_start();
    }

    watchdog_start(mod);
    SDL_GameController *pad = open_first_controller();

    int   running       = 1;
    int   is_fullscreen = prof->fullscreen;
    int   mouse_down    = 0;
    float steer_tilt    = 0.0f;

    printf("[JNI] Entering main game loop (%dx%d, %dx MSAA, %dx Anisotropic)...\n",
           surface_w, surface_h, prof->msaa, prof->anisotropy);
    printf("      Controls: Arrows/WASD = Steer/Brake/Nitro | Space/Enter = Gas/Select | Esc = Back | F11 = Fullscreen\n");

    while (running) {
        watchdog_frame();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = 0;
                    break;

                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        int w, h;
                        SDL_GL_GetDrawableSize(window, &w, &h);
                        if (w != surface_w || h != surface_h) {
                            surface_w = w;
                            surface_h = h;
                            glViewport(0, 0, w, h);
                            if (fn_event) {
                                call_jni_event(mod, fn_event, 3, 0, w, h, 0, NULL);
                            } else if (fn_on_surface_changed) {
                                uint32_t ch_args[5] = { (uint32_t)(uintptr_t)&fake_jni, 0, 0, (uint32_t)w, (uint32_t)h };
                                invoke_mod_fn(mod, fn_on_surface_changed, 5, ch_args);
                            }
                        }
                    } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                        watchdog_set_focused(1);
                    } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        watchdog_set_focused(0);
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT && fn_event) {
                        mouse_down = 1;
                        call_jni_event(mod, fn_event, 1, 1, ev.button.x, ev.button.y, 0 | (1 << 16), NULL);
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (mouse_down && fn_event) {
                        call_jni_event(mod, fn_event, 1, 1, ev.motion.x, ev.motion.y, 2 | (1 << 16), NULL);
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (ev.button.button == SDL_BUTTON_LEFT && fn_event) {
                        mouse_down = 0;
                        call_jni_event(mod, fn_event, 1, 1, ev.button.x, ev.button.y, 1 | (1 << 16), NULL);
                    }
                    break;

                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    int pressed = (ev.type == SDL_KEYDOWN) ? 1 : 0;
                    SDL_Keycode sym = ev.key.keysym.sym;
                    if (sym == SDLK_F11 || (sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))) {
                        if (pressed && !ev.key.repeat) {
                            toggle_window_fullscreen(window, &is_fullscreen);
                        }
                        break;
                    }
                    int keycode = 0;
                    switch (sym) {
                        case SDLK_UP:
                        case SDLK_w:         keycode = KEY_DPAD_UP;     break;
                        case SDLK_DOWN:
                        case SDLK_s:         keycode = KEY_DPAD_DOWN;   break;
                        case SDLK_LEFT:
                        case SDLK_a:         keycode = KEY_DPAD_LEFT;   break;
                        case SDLK_RIGHT:
                        case SDLK_d:         keycode = KEY_DPAD_RIGHT;  break;
                        case SDLK_RETURN:
                        case SDLK_SPACE:
                        case SDLK_x:         keycode = KEY_DPAD_CENTER; break;
                        case SDLK_z:         keycode = KEY_BUTTON_X;    break;
                        case SDLK_c:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT:    keycode = KEY_BUTTON_Y;    break;
                        case SDLK_ESCAPE:
                        case SDLK_BACKSPACE: keycode = KEY_BACK;        break;
                        default: break;
                    }
                    if (keycode && fn_event) {
                        call_jni_event(mod, fn_event, 1, 0, keycode, 0, pressed, NULL);
                    }
                    break;
                }

                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP: {
                    int pressed = (ev.type == SDL_CONTROLLERBUTTONDOWN) ? 1 : 0;
                    int keycode = 0;
                    switch (ev.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_A:             keycode = KEY_DPAD_CENTER; break;
                        case SDL_CONTROLLER_BUTTON_B:
                        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  keycode = KEY_BUTTON_X;    break;
                        case SDL_CONTROLLER_BUTTON_X:
                        case SDL_CONTROLLER_BUTTON_Y:
                        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: keycode = KEY_BUTTON_Y;    break;
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     keycode = KEY_DPAD_LEFT;   break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    keycode = KEY_DPAD_RIGHT;  break;
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:       keycode = KEY_DPAD_UP;     break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     keycode = KEY_DPAD_DOWN;   break;
                        case SDL_CONTROLLER_BUTTON_START:
                        case SDL_CONTROLLER_BUTTON_BACK:          keycode = KEY_BACK;        break;
                        default: break;
                    }
                    if (keycode && fn_event) {
                        call_jni_event(mod, fn_event, 1, 0, keycode, 0, pressed, NULL);
                    }
                    break;
                }
            }
        }

        if (fn_event) {
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A])       steer_tilt = -7.0f;
            else if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) steer_tilt =  7.0f;
            else                                                       steer_tilt =  0.0f;

            if (pad) {
                Sint16 sx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
                if (abs(sx) > 3200) {
                    steer_tilt = (sx / 32767.0f) * 7.0f;
                }
            }

            int tilt_y = (int)(steer_tilt * 6553.6f);
            int grav_z = (int)(9.81f * 6553.6f);
            call_jni_event(mod, fn_event, 4, 0, 0, tilt_y, grav_z, NULL);

            int keep = 1;
            call_jni_event(mod, fn_event, 0, 1, 0, 0, 0, &keep);
            if (keep == 0) {
                printf("[JNI] Engine requested termination.\n");
                running = 0;
            }
        } else if (fn_on_draw_frame) {
            static uint32_t s_frame_count = 0;
            static DWORD s_last_fps_time = 0;
            static uint32_t s_frames_since_fps = 0;
            s_frame_count++;
            s_frames_since_fps++;
            DWORD now = GetTickCount();
            if (s_frame_count == 1 || (s_last_fps_time > 0 && now - s_last_fps_time >= 5000)) {
                float fps = (s_last_fps_time > 0 && now > s_last_fps_time)
                          ? (float)s_frames_since_fps * 1000.0f / (float)(now - s_last_fps_time) : 60.0f;
                printf("[libRealRacing3] Rendering frame %u (%.1f FPS, running natively)...\n", s_frame_count, fps);
                fflush(stdout);
                s_last_fps_time = now;
                s_frames_since_fps = 0;
            } else if (s_last_fps_time == 0) {
                s_last_fps_time = now;
            }
            uint32_t df_args[3] = { (uint32_t)(uintptr_t)&fake_jni, (uint32_t)(uintptr_t)&activity_placeholder, 0 };
            invoke_mod_fn(mod, fn_on_draw_frame, 3, df_args);
        }

        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    audio_stop();
    opensles_shutdown();
    if (fn_event) {
        call_jni_event(mod, fn_event, 0, 0, 0, 0, 0, NULL);
    }
    if (fn_destroy) {
        uint32_t d_args[2] = { (uint32_t)(uintptr_t)&fake_jni, 0 };
        invoke_mod_fn(mod, fn_destroy, 2, d_args);
    }
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/* ================================================================== */
/* Backend 4: Cocos2d-x JNI Engine Loop                               */
/* ================================================================== */
static int run_cocos2dx_backend(elf_module_t *mod, const game_profile_t *prof,
                                SDL_Window *window, SDL_GLContext gl_ctx) {
    int surface_w = prof->width, surface_h = prof->height;
    SDL_GL_GetDrawableSize(window, &surface_w, &surface_h);

    void *fn_onload = (void *)elf_sym(mod, "JNI_OnLoad");
    if (fn_onload) {
        uint32_t ol_args[2] = { (uint32_t)(uintptr_t)&fake_jvm, 0 };
        invoke_mod_fn(mod, fn_onload, 2, ol_args);
    }

    void *fn_set_apk = resolve_jni_entry(
        mod, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath", "nativeSetApkPath", "nativeSetApkPath");
    void *fn_init = resolve_jni_entry(
        mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit", "Cocos2dxRenderer_nativeInit", "nativeInit");
    void *fn_render = resolve_jni_entry(
        mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender", "Cocos2dxRenderer_nativeRender", "nativeRender");
    void *fn_touch_begin = resolve_jni_entry(
        mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin", "nativeTouchesBegin", "nativeTouchesBegin");
    void *fn_touch_end = resolve_jni_entry(
        mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd", "nativeTouchesEnd", "nativeTouchesEnd");
    void *fn_key_down = resolve_jni_entry(
        mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown", "nativeKeyDown", "nativeKeyDown");

    opensles_init();

    if (fn_set_apk && prof->apk_path[0]) {
        jstring j_apk = fake_jni->NewStringUTF(&fake_jni, prof->apk_path);
        uint32_t a[3] = { (uint32_t)(uintptr_t)&fake_jni, 0, (uint32_t)(uintptr_t)j_apk };
        invoke_mod_fn(mod, fn_set_apk, 3, a);
    }
    if (fn_init) {
        uint32_t a[4] = { (uint32_t)(uintptr_t)&fake_jni, 0, (uint32_t)surface_w, (uint32_t)surface_h };
        invoke_mod_fn(mod, fn_init, 4, a);
    }

    watchdog_start(mod);
    int running = 1;
    int is_fullscreen = prof->fullscreen;

    printf("[Cocos2d-x] Entering main loop (%dx%d)...\n", surface_w, surface_h);
    while (running) {
        watchdog_frame();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                if (ev.key.keysym.sym == SDLK_F11) toggle_window_fullscreen(window, &is_fullscreen);
                else if (ev.key.keysym.sym == SDLK_ESCAPE && fn_key_down) {
                    uint32_t a[3] = { (uint32_t)(uintptr_t)&fake_jni, 0, KEY_BACK };
                    invoke_mod_fn(mod, fn_key_down, 3, a);
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && fn_touch_begin) {
                float fx = (float)ev.button.x, fy = (float)ev.button.y;
                uint32_t ux, uy;
                memcpy(&ux, &fx, 4);
                memcpy(&uy, &fy, 4);
                uint32_t a[5] = { (uint32_t)(uintptr_t)&fake_jni, 0, 0, ux, uy };
                invoke_mod_fn(mod, fn_touch_begin, 5, a);
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT && fn_touch_end) {
                float fx = (float)ev.button.x, fy = (float)ev.button.y;
                uint32_t ux, uy;
                memcpy(&ux, &fx, 4);
                memcpy(&uy, &fy, 4);
                uint32_t a[5] = { (uint32_t)(uintptr_t)&fake_jni, 0, 0, ux, uy };
                invoke_mod_fn(mod, fn_touch_end, 5, a);
            }
        }
        if (fn_render) {
            uint32_t a[2] = { (uint32_t)(uintptr_t)&fake_jni, 0 };
            invoke_mod_fn(mod, fn_render, 2, a);
        }
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    opensles_shutdown();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/* ================================================================== */
/* Backend 5: SDL2-for-Android (SDL_main)                             */
/* ================================================================== */
static int run_sdl2_android_backend(elf_module_t *mod, const game_profile_t *prof, SDL_Window *window) {
    void *fn_sdl_main = (void *)elf_sym(mod, "SDL_main");
    if (!fn_sdl_main) {
        fprintf(stderr, "[SDL2-Android] ERROR: SDL_main not found in %s\n", mod->name);
        return 1;
    }
    watchdog_start(mod);
    char *fake_argv[2] = { (char *)prof->so_path, NULL };
    uint32_t a[2] = { 1, (uint32_t)(uintptr_t)fake_argv };
    printf("[SDL2-Android] Invoking SDL_main(%p)...\n", fn_sdl_main);
    int rc = (int)invoke_mod_fn(mod, fn_sdl_main, 2, a);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}

/* ================================================================== */
/* CLI Argument Parser & Entry Point                                  */
/* ================================================================== */
static void print_usage(const char *prog) {
    printf("================================================================\n");
    printf(" Universal Android-to-Windows Native Port Engine\n");
    printf("================================================================\n");
    printf("Usage:\n");
    printf("  %s --scan <game.apk | game_folder>               (Check portability & symbols)\n", prog);
    printf("  %s --port <game.apk | game_folder> [--out <dir>] (Generate standalone port)\n", prog);
    printf("  %s <game_folder | game.apk | libgame.so | profile.ini> [options]\n\n", prog);
    printf("Options:\n");
    printf("  --scan <path>          Scan APK/folder to determine portability & backend\n");
    printf("  --port <path>          Generate a standalone Windows native port folder\n");
    printf("  --out <dir>            Output folder for --port (default: ..\\<name>-port)\n");
    printf("  --game <path>          Path to game folder, .apk, .so, or .ini\n");
    printf("  --profile <file.ini>   Load game configuration from INI profile\n");
    printf("  --so <libgame.so>      Override primary .so binary path\n");
    printf("  --inspect <path>       Alias for --scan\n");
    printf("  --backend <mode>       Force backend: auto | ndk | polarbit | cocos2dx | sdl2 | jni\n");
    printf("  --width <px>           Render width  (default: 1280)\n");
    printf("  --height <px>          Render height (default: 720)\n");
    printf("  --fullscreen           Start in borderless desktop fullscreen\n");
    printf("  --windowed             Start in resizable window (default)\n");
    printf("  --msaa <0|2|4|8>       Multisample anti-aliasing samples (default: 4)\n");
    printf("  --aniso <1..16>        Anisotropic texture filtering (default: 16)\n");
    printf("  --auto-mipmaps         Force automatic mipmap generation + trilinear\n");
    printf("  --no-mipmaps           Disable automatic mipmap generation\n");
    printf("  --dump-meshes          Export 3D meshes (.obj/.mtl) & textures (.tga)\n");
    printf("  --mod-dir <dir>        Directory for live HD texture replacements (.tga)\n");
    printf("  --allow-missing-syms   Auto-stub unresolved ELF imports instead of failing\n");
    printf("  --touch-sticks         Use virtual touch dual-sticks for NDK games\n");
    printf("================================================================\n");
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(65001);
    SetProcessDPIAware();
    AddVectoredExceptionHandler(1, stack_commit_handler);
    SetUnhandledExceptionFilter(crash_handler);

    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    game_profile_t prof;
    profile_init_defaults(&prof);

    const char *target_arg  = NULL;
    const char *scan_arg    = NULL;
    const char *port_arg    = NULL;
    const char *out_dir_arg = NULL;
    const char *so_override = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if ((strcmp(a, "--scan") == 0 || strcmp(a, "--inspect") == 0) && i + 1 < argc) {
            scan_arg = argv[++i];
        } else if (strcmp(a, "--port") == 0 && i + 1 < argc) {
            port_arg = argv[++i];
        } else if (strcmp(a, "--out") == 0 && i + 1 < argc) {
            out_dir_arg = argv[++i];
        } else if (strcmp(a, "--game") == 0 && i + 1 < argc) {
            target_arg = argv[++i];
        } else if (strcmp(a, "--profile") == 0 && i + 1 < argc) {
            if (profile_load_ini(&prof, argv[++i]) != 0) return 1;
        } else if (strcmp(a, "--so") == 0 && i + 1 < argc) {
            so_override = argv[++i];
        } else if (strcmp(a, "--backend") == 0 && i + 1 < argc) {
            const char *b = argv[++i];
            if (_stricmp(b, "ndk") == 0) prof.backend = BACKEND_NDK;
            else if (_stricmp(b, "polarbit") == 0 || _stricmp(b, "fuse") == 0) prof.backend = BACKEND_POLARBIT_FUSE;
            else if (_stricmp(b, "cocos2dx") == 0 || _stricmp(b, "cocos") == 0) prof.backend = BACKEND_COCOS2DX;
            else if (_stricmp(b, "sdl2") == 0 || _stricmp(b, "sdl") == 0) prof.backend = BACKEND_SDL2_ANDROID;
            else if (_stricmp(b, "unity") == 0) prof.backend = BACKEND_UNITY;
            else if (_stricmp(b, "jni") == 0) prof.backend = BACKEND_GENERIC_JNI;
            else prof.backend = BACKEND_AUTO;
        } else if (strcmp(a, "--width") == 0 && i + 1 < argc) {
            prof.width = atoi(argv[++i]);
        } else if (strcmp(a, "--height") == 0 && i + 1 < argc) {
            prof.height = atoi(argv[++i]);
        } else if (strcmp(a, "--fullscreen") == 0) {
            prof.fullscreen = 1;
        } else if (strcmp(a, "--windowed") == 0) {
            prof.fullscreen = 0;
        } else if (strcmp(a, "--msaa") == 0 && i + 1 < argc) {
            prof.msaa = atoi(argv[++i]);
        } else if (strcmp(a, "--aniso") == 0 && i + 1 < argc) {
            prof.anisotropy = atoi(argv[++i]);
        } else if (strcmp(a, "--auto-mipmaps") == 0) {
            prof.auto_mipmaps = 1;
        } else if (strcmp(a, "--no-mipmaps") == 0) {
            prof.auto_mipmaps = 0;
        } else if (strcmp(a, "--dump-meshes") == 0) {
            prof.dump_meshes = 1;
        } else if (strcmp(a, "--mod-dir") == 0 && i + 1 < argc) {
            snprintf(prof.mod_dir, sizeof(prof.mod_dir), "%s", argv[++i]);
        } else if (strcmp(a, "--allow-missing-syms") == 0) {
            prof.allow_missing_syms = 1;
        } else if (strcmp(a, "--touch-sticks") == 0) {
            prof.force_touch_sticks = 1;
        } else if (a[0] != '-' && !target_arg) {
            target_arg = a;
        }
    }

    if (scan_arg) {
        return profile_scan_target(scan_arg);
    }

    if (port_arg) {
        return profile_port_target(port_arg, out_dir_arg);
    }

    if (target_arg) {
        if (profile_resolve_target(&prof, target_arg) != 0 && !so_override) {
            fprintf(stderr, "[Engine] Failed to resolve game target '%s'. Run with --scan \"%s\" for details.\n",
                    target_arg, target_arg);
            return 1;
        }
    }
    if (so_override) {
        snprintf(prof.so_path, sizeof(prof.so_path), "%s", so_override);
    }

    if (!prof.so_path[0]) {
        fprintf(stderr, "[Engine] No Android .so library specified or discovered.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (prof.has_unity_dll || prof.backend == BACKEND_UNITY) {
        printf("================================================================\n");
        printf(" Unity 3D Android Game Detected (%s)\n", prof.unity_dll_path);
        printf("================================================================\n");
        printf(" Unity games use Managed C# (Assembly-CSharp.dll) + Unity assets.\n");
        printf(" To generate a standalone Windows Unity port, run:\n");
        printf("   native-port-engine.exe --port \"%s\"\n", target_arg ? target_arg : prof.game_root);
        printf("================================================================\n");
        return 0;
    }

    CreateDirectoryA(prof.save_dir, NULL);
    log_init("engine_log.txt");
    printf("================================================================\n");
    printf(" Universal Native Port Engine\n");
    printf("================================================================\n");
    printf(" Title      : %s\n", prof.title);
    printf(" Game Root  : %s\n", prof.game_root);
    printf(" Primary SO : %s (e_machine=%d)\n", prof.so_path, prof.elf_machine);
    printf(" Assets Dir : %s\n", prof.assets_dir);
    printf(" Save Dir   : %s\n", prof.save_dir);
    printf(" Mod Dir    : %s\n", prof.mod_dir);
    printf(" Resolution : %dx%d (%s, %dx MSAA, %dx Aniso)\n",
           prof.width, prof.height, prof.fullscreen ? "Fullscreen" : "Windowed",
           prof.msaa, prof.anisotropy);
    printf("================================================================\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                 SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "[Engine] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = create_engine_window(&prof);
    if (!window) {
        fprintf(stderr, "[Engine] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    int win_w = prof.width, win_h = prof.height;
    SDL_GetWindowSize(window, &win_w, &win_h);

    shims_set_paths(prof.game_root, prof.save_dir, prof.apk_path);
    shims_init();

    size_t sym_count = 0;
    const host_symbol_t *syms = shims_get_table(&sym_count);

    /* Pre-load any auxiliary shared libraries (e.g. libc++_shared.so, libfmodex.so) */
    static elf_module_t aux_mods[MAX_AUX_LIBS];
    int loaded_aux[MAX_AUX_LIBS] = {0};
    for (int i = 0; i < prof.aux_so_count; i++) {
        printf("[Engine] Pre-loading auxiliary library: %s\n", prof.aux_so_paths[i]);
        if (elf_load(prof.aux_so_paths[i], &aux_mods[i]) == 0) {
            elf_register_module(&aux_mods[i]);
            loaded_aux[i] = 1;
        }
    }
    for (int i = 0; i < prof.aux_so_count; i++) {
        if (loaded_aux[i]) {
            elf_relocate(&aux_mods[i], syms, sym_count, 1);
        }
    }

    /* Load primary game ELF */
    static elf_module_t main_mod;
    if (elf_load(prof.so_path, &main_mod) != 0) {
        fprintf(stderr, "[Engine] ERROR: Could not load ELF '%s'\n", prof.so_path);
        return 1;
    }
    elf_register_module(&main_mod);
    g_primary_mod = &main_mod;

    if (prof.backend == BACKEND_AUTO) {
        prof.backend = profile_detect_backend(&main_mod);
    }
    printf("[Engine] Selected Backend: %s\n", profile_backend_name(prof.backend));

    /* Configure HQ OpenGL & runtime subsystems BEFORE running ELF .init_array constructors */
    int auto_mip = (prof.auto_mipmaps >= 0) ? prof.auto_mipmaps : 1;
    gl_wrap_set_texture_quality(auto_mip, prof.anisotropy);
    gl_wrap_set_mod_dir(prof.mod_dir);

    SDL_GLContext jni_gl_ctx = NULL;
    if (prof.backend == BACKEND_NDK) {
        egl_wrap_init(window);
        egl_wrap_set_msaa(prof.msaa > 0);
        opensles_init();
        android_ndk_set_asset_root(prof.game_root);
        android_app_set_data_path(prof.save_dir);
        android_ndk_init(win_w, win_h);
    } else {
        g_jni_window = window;
        jni_gl_ctx = SDL_GL_CreateContext(window);
        SDL_GL_MakeCurrent(window, jni_gl_ctx);
        SDL_GL_SetSwapInterval(1);
        if (prof.msaa > 0) glEnable(0x809D /* GL_MULTISAMPLE */);
        glViewport(0, 0, win_w, win_h);
        gl_wrap_init();
        fake_jni_init(jni_swap_buffers_cb);
    }

    if (prof.dump_meshes) {
        char dump_dir[512];
        snprintf(dump_dir, sizeof(dump_dir), "%s\\mesh_dump", prof.game_root);
        mesh_dump_init(dump_dir);
    }

    /* Relocate ELF symbols against unified Bionic/GLES/EGL/NDK/JNI shim table */
    int allow_missing = prof.allow_missing_syms || (main_mod.machine == 40);
    if (elf_relocate(&main_mod, syms, sym_count, allow_missing) != 0) {
        fprintf(stderr, "[Engine] ERROR: Unresolved symbols. Re-run with --allow-missing-syms to stub them.\n");
        return 1;
    }

    /* Run C++ static constructors (.init_array) */
    for (int i = 0; i < prof.aux_so_count; i++) {
        if (loaded_aux[i] && aux_mods[i].machine == 3) {
            elf_init(&aux_mods[i]);
        }
    }
    elf_init(&main_mod);

    int rc = 0;
    if (prof.backend == BACKEND_NDK) {
        rc = run_ndk_backend(&main_mod, &prof, window);
    } else if (prof.backend == BACKEND_COCOS2DX) {
        rc = run_cocos2dx_backend(&main_mod, &prof, window, jni_gl_ctx);
    } else if (prof.backend == BACKEND_SDL2_ANDROID) {
        rc = run_sdl2_android_backend(&main_mod, &prof, window);
    } else {
        rc = run_jni_backend(&main_mod, &prof, window, jni_gl_ctx);
    }

    if (prof.dump_meshes) {
        mesh_dump_shutdown();
    }
    log_flush();
    return rc;
}
