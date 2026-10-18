#include "minigore2_plugin.h"
#include "hook_util.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_active = 0;

/* ================================================================== */
/* 1. xt::java::* JNI Bridge Prologue Stubs                           */
/* ================================================================== */
static int __attribute__((cdecl)) stub_is_ad_available(void)       { return 0; }
static int __attribute__((cdecl)) stub_fetch_ad_reward(void)       { return 0; }
static void __attribute__((cdecl)) stub_display_video_ad(void)     {}
static void __attribute__((cdecl)) stub_display_popup(const char *s)   { printf("[MG2-Java] displayPopup(\"%s\")\n", s ? s : ""); }
static void __attribute__((cdecl)) stub_display_dialog(const char *s)  { printf("[MG2-Java] displayDialog(\"%s\")\n", s ? s : ""); }
static void __attribute__((cdecl)) stub_launch_browser(const char *s)  { printf("[MG2-Java] launchBrowser(\"%s\")\n", s ? s : ""); }

static int install_java_stubs(elf_module_t *mod) {
    struct { const char *sym; void *stub; } table[] = {
        { "_ZN2xt4java26isRewardedVideoAdAvailableEv", (void *)stub_is_ad_available },
        { "_ZN2xt4java25fetchVideoAdRewardPendingEv",  (void *)stub_fetch_ad_reward },
        { "_ZN2xt4java22displayRewardedVideoAdEv",     (void *)stub_display_video_ad },
        { "_ZN2xt4java12displayPopupEPKc",             (void *)stub_display_popup },
        { "_ZN2xt4java13displayDialogEPKc",            (void *)stub_display_dialog },
        { "_ZN2xt4java13launchBrowserEPKc",            (void *)stub_launch_browser },
    };
    int patched = 0;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (hook_prologue_jmp(mod, table[i].sym, table[i].stub))
            patched++;
    }
    return patched;
}

/* ================================================================== */
/* 2. Immediate-Mode UI Button Collector (xt::Imgui)                  */
/* ================================================================== */
#define MAX_UI_BUTTONS 64

typedef struct { float cx, cy, w, h; } ui_rect_t;
static ui_rect_t g_cur_btns[MAX_UI_BUTTONS];
static int       g_cur_btn_count = 0;
static ui_rect_t g_snap_btns[MAX_UI_BUTTONS];
static int       g_snap_btn_count = 0;
static CRITICAL_SECTION g_ui_lock;
static int       g_ui_ready = 0;

typedef void (__attribute__((cdecl)) *BeginFrameFn)(float dt);
typedef int  (__attribute__((cdecl)) *ButtonLogicFn)(unsigned id, int x, int y, int w, int h);
static BeginFrameFn  o_begin_frame = NULL;
static ButtonLogicFn o_button_logic = NULL;

static void publish_ui_frame(void) {
    if (!g_ui_ready) return;
    EnterCriticalSection(&g_ui_lock);
    memcpy(g_snap_btns, g_cur_btns, g_cur_btn_count * sizeof(ui_rect_t));
    g_snap_btn_count = g_cur_btn_count;
    LeaveCriticalSection(&g_ui_lock);
    g_cur_btn_count = 0;
}

static void __attribute__((cdecl)) h_begin_frame(float dt) {
    publish_ui_frame();
    o_begin_frame(dt);
}

static int __attribute__((cdecl)) h_button_logic(unsigned id, int x, int y, int w, int h) {
    if (w > 8 && h > 8 && g_cur_btn_count < MAX_UI_BUTTONS) {
        float cx = (float)x + (float)w * 0.5f;
        float cy = (float)y + (float)h * 0.5f;
        int dup = 0;
        for (int i = 0; i < g_cur_btn_count; i++) {
            if (fabsf(g_cur_btns[i].cx - cx) < 2.0f && fabsf(g_cur_btns[i].cy - cy) < 2.0f) {
                dup = 1;
                break;
            }
        }
        if (!dup) {
            g_cur_btns[g_cur_btn_count++] = (ui_rect_t){ cx, cy, (float)w, (float)h };
        }
    }
    return o_button_logic(id, x, y, w, h);
}

int minigore2_ui_nav_count(void) {
    if (!g_ui_ready) return 0;
    EnterCriticalSection(&g_ui_lock);
    int n = g_snap_btn_count;
    LeaveCriticalSection(&g_ui_lock);
    return n;
}

int minigore2_ui_nav_find(float cur_x, float cur_y, int dir_x, int dir_y,
                          float *out_x, float *out_y) {
    if (!g_ui_ready || (dir_x == 0 && dir_y == 0)) return 0;
    ui_rect_t btns[MAX_UI_BUTTONS];
    EnterCriticalSection(&g_ui_lock);
    int n = g_snap_btn_count;
    memcpy(btns, g_snap_btns, n * sizeof(ui_rect_t));
    LeaveCriticalSection(&g_ui_lock);
    if (n == 0) return 0;

    int best = -1;
    float best_score = 1e30f;
    for (int i = 0; i < n; i++) {
        float dx = btns[i].cx - cur_x;
        float dy = btns[i].cy - cur_y;
        float along = dx * (float)dir_x + dy * (float)dir_y;
        float perp  = fabsf(dx * (float)dir_y - dy * (float)dir_x);
        if (along < 8.0f) continue;
        if (perp > along * 2.5f + 80.0f) continue;
        float score = along + perp * 2.5f;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    if (best < 0) return 0;
    *out_x = btns[best].cx;
    *out_y = btns[best].cy;
    return 1;
}

/* ================================================================== */
/* 3. Direct Stick Injection & Player Screen Position                 */
/* ================================================================== */
#define ENV_PLAYER    0x132c14
#define ENV_VIEWPROJ  0x1f5f6c
#define ENV_STICKS    0x1f6040
#define ENTITY_POS    0x04

typedef int  (__attribute__((cdecl)) *ScreenSizeFn)(void);
typedef int  (__attribute__((cdecl)) *ControlUpdateFn)(void *self, float dt, void *character);
typedef void (__attribute__((cdecl)) *UpdateControlsFn)(void *state, float dt);

static uint8_t        **g_env = NULL;
static ScreenSizeFn     g_screen_w = NULL;
static ScreenSizeFn     g_screen_h = NULL;
static ControlUpdateFn  o_control_update = NULL;
static UpdateControlsFn o_update_controls = NULL;

static CRITICAL_SECTION g_stick_lock;
static float            g_sticks[4];
static DWORD            g_sticks_tick = 0;
static volatile LONG    g_controls_tick = 0;
static volatile float   g_player_sx = 0.0f;
static volatile float   g_player_sy = 0.0f;

static int project_ground(float x, float z, float *sx, float *sy) {
    uint8_t *e = g_env ? *g_env : NULL;
    if (!e || !g_screen_w || !g_screen_h) return 0;
    const float *m = (const float *)(e + ENV_VIEWPROJ);
    float cx = m[0] * x + m[8] * z + m[12];
    float cy = m[1] * x + m[9] * z + m[13];
    float cw = m[3] * x + m[11] * z + m[15];
    if (cw <= 1e-6f) return 0;
    float w = (float)g_screen_w(), h = (float)g_screen_h();
    *sx = (cx / cw * 0.5f + 0.5f) * w;
    *sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * h;
    return 1;
}

static int __attribute__((cdecl)) h_control_update(void *self, float dt, void *character) {
    uint8_t *e = g_env ? *g_env : NULL;
    if (e) {
        uint8_t *player = *(uint8_t **)(e + ENV_PLAYER);
        if (player) {
            const float *p = (const float *)(player + ENTITY_POS);
            float sx, sy;
            if (project_ground(p[0], p[1], &sx, &sy)) {
                g_player_sx = sx;
                g_player_sy = sy;
            }
        }
    }
    InterlockedExchange(&g_controls_tick, (LONG)GetTickCount());
    return o_control_update(self, dt, character);
}

static void __attribute__((cdecl)) h_update_controls(void *state, float dt) {
    o_update_controls(state, dt);
    uint8_t *e = g_env ? *g_env : NULL;
    if (!e) return;

    float s[4];
    EnterCriticalSection(&g_stick_lock);
    int fresh = g_sticks_tick != 0 && (GetTickCount() - g_sticks_tick < 200);
    memcpy(s, g_sticks, sizeof(s));
    LeaveCriticalSection(&g_stick_lock);
    if (!fresh) return;

    float *sticks = (float *)(e + ENV_STICKS);
    if (s[0] != 0.0f || s[1] != 0.0f) {
        sticks[0] = s[0];
        sticks[1] = s[1];
    }
    if (s[2] != 0.0f || s[3] != 0.0f) {
        sticks[2] = s[2];
        sticks[3] = s[3];
    }
}

int minigore2_set_sticks(float move_x, float move_y, float aim_x, float aim_y) {
    if (!g_active || !o_update_controls) return 0;
    EnterCriticalSection(&g_stick_lock);
    g_sticks[0] = move_x;
    g_sticks[1] = move_y;
    g_sticks[2] = aim_x;
    g_sticks[3] = aim_y;
    g_sticks_tick = GetTickCount();
    LeaveCriticalSection(&g_stick_lock);
    return 1;
}

int minigore2_in_level(void) {
    if (!g_active || !o_control_update) return 0;
    LONG tick = g_controls_tick;
    return tick != 0 && ((LONG)GetTickCount() - tick <= 150);
}

int minigore2_player_screen_pos(float *sx, float *sy) {
    if (!minigore2_in_level()) return 0;
    *sx = g_player_sx;
    *sy = g_player_sy;
    return 1;
}

int minigore2_plugin_active(void) {
    return g_active;
}

int minigore2_plugin_install(elf_module_t *mod) {
    if (!mod) return 0;
    if (!elf_sym(mod, "_ZN2xt4java26isRewardedVideoAdAvailableEv") &&
        !elf_sym(mod, "_ZN9StateGame14updateControlsEf")) {
        return 0;
    }

    int java_cnt = install_java_stubs(mod);

    InitializeCriticalSection(&g_ui_lock);
    o_begin_frame  = (BeginFrameFn)hook_got(mod, "_ZN2xt5Imgui10beginFrameEf", (void *)h_begin_frame);
    o_button_logic = (ButtonLogicFn)hook_got(mod, "_ZN2xt5Imgui11buttonLogicEjiiii", (void *)h_button_logic);
    if (o_begin_frame && o_button_logic) {
        g_ui_ready = 1;
    }

    InitializeCriticalSection(&g_stick_lock);
    g_env      = (uint8_t **)elf_sym(mod, "m_env");
    g_screen_w = (ScreenSizeFn)elf_sym(mod, "_ZN2xt6Screen8getWidthEv");
    g_screen_h = (ScreenSizeFn)elf_sym(mod, "_ZN2xt6Screen9getHeightEv");
    if (g_env) {
        o_control_update = (ControlUpdateFn)hook_vtable(mod, "_ZTV21CharacterControlHuman", 2, (void *)h_control_update);
        if (o_control_update)
            hook_got(mod, "_ZN21CharacterControlHuman6updateEfP9Character", (void *)h_control_update);
        o_update_controls = (UpdateControlsFn)hook_got(mod, "_ZN9StateGame14updateControlsEf", (void *)h_update_controls);
    }

    g_active = 1;
    printf("[Plugin:Minigore2] Activated (%d Java stubs, Imgui nav=%s, direct sticks=%s)\n",
           java_cnt, g_ui_ready ? "on" : "off", o_update_controls ? "on" : "off");
    return 1;
}
