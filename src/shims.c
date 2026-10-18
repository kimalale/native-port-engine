#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include "shims.h"
#include "gl_wrap.h"
#include "watchdog.h"
#include "log.h"
#include "arm32_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <sys/stat.h>
#include <locale.h>

#ifdef CDECL
#undef CDECL
#endif
#define CDECL __attribute__((cdecl))

/* Configurable VFS root paths */
static char g_game_root[512] = ".";
static char g_save_dir[512]  = "saves";
static char g_apk_path[512]  = "";

void shims_set_paths(const char *game_root, const char *save_dir, const char *apk_path) {
    if (game_root && game_root[0]) {
        strncpy(g_game_root, game_root, sizeof(g_game_root) - 1);
        g_game_root[sizeof(g_game_root) - 1] = '\0';
    }
    if (save_dir && save_dir[0]) {
        strncpy(g_save_dir, save_dir, sizeof(g_save_dir) - 1);
        g_save_dir[sizeof(g_save_dir) - 1] = '\0';
        /* Strip trailing slash if present */
        size_t len = strlen(g_save_dir);
        while (len > 1 && (g_save_dir[len - 1] == '/' || g_save_dir[len - 1] == '\\')) {
            g_save_dir[--len] = '\0';
        }
        _mkdir(g_save_dir);
    }
    if (apk_path && apk_path[0]) {
        strncpy(g_apk_path, apk_path, sizeof(g_apk_path) - 1);
        g_apk_path[sizeof(g_apk_path) - 1] = '\0';
    }
}

/* Bionic ctype tables (exported as pointers to 257-entry tables) */
static unsigned char bionic_ctype_[257];
static short bionic_tolower_tab_[257];
static short bionic_toupper_tab_[257];
static const unsigned char *bionic_ctype_ptr = bionic_ctype_;
static const short *bionic_tolower_tab_ptr = bionic_tolower_tab_;
static const short *bionic_toupper_tab_ptr = bionic_toupper_tab_;

/* Bionic __sF: 3x 0x54-byte FILE structs for stdin, stdout, stderr */
#define BIONIC_FILE_SIZE 0x54
static uint8_t bionic_sF_[3 * BIONIC_FILE_SIZE] __attribute__((aligned(16)));

static int is_std_stream(void *fp) {
    uint8_t *p = (uint8_t *)fp;
    return p >= bionic_sF_ && p < bionic_sF_ + sizeof(bionic_sF_);
}

static FILE *host_file(void *fp) {
    if (is_std_stream(fp)) {
        switch (((uint8_t *)fp - bionic_sF_) / BIONIC_FILE_SIZE) {
            case 0: return stdin;
            case 1: return stdout;
            default: return stderr;
        }
    }
    return (FILE *)fp;
}

static void resolve_vfs_path(const char *raw_path, int is_write, char *out_path, size_t out_size) {
    if (!raw_path || !raw_path[0]) {
        out_path[0] = '\0';
        return;
    }

    const char *p = raw_path;
    int from_sdcard = 0;
    if (strncmp(p, "/sdcard", 7) == 0) {
        p += 7;
        from_sdcard = 1;
    } else if (strncmp(p, "(null)", 6) == 0) {
        p += 6;
        from_sdcard = 1;
    }
    while (p[0] == '/' || p[0] == '\\') p++;

    /* Special case for Polarbit Data.vfs */
    if (strstr(p, "Data.vfs") != NULL) {
        char cand[512];
        snprintf(cand, sizeof(cand), "%s/Data.vfs", g_game_root);
        if (_access(cand, 0) == 0) { strncpy(out_path, cand, out_size - 1); out_path[out_size - 1] = '\0'; return; }
        snprintf(cand, sizeof(cand), "%s/apk/assets/Data.vfs", g_game_root);
        if (_access(cand, 0) == 0) { strncpy(out_path, cand, out_size - 1); out_path[out_size - 1] = '\0'; return; }
        snprintf(cand, sizeof(cand), "%s/assets/Data.vfs", g_game_root);
        if (_access(cand, 0) == 0) { strncpy(out_path, cand, out_size - 1); out_path[out_size - 1] = '\0'; return; }
        if (_access("Data.vfs", 0) == 0) { strncpy(out_path, "Data.vfs", out_size - 1); out_path[out_size - 1] = '\0'; return; }
    }

    /* Special case for APK self-inspection */
    if (strstr(p, ".apk") != NULL && g_apk_path[0] && _access(g_apk_path, 0) == 0) {
        strncpy(out_path, g_apk_path, out_size - 1);
        out_path[out_size - 1] = '\0';
        return;
    }

    if (from_sdcard || strncmp(p, "saves/", 6) == 0 || strncmp(p, "saves\\", 6) == 0) {
        const char *sub = (strncmp(p, "saves/", 6) == 0 || strncmp(p, "saves\\", 6) == 0) ? (p + 6) : p;
        /* Ensure any intermediate directory inside save_dir exists */
        snprintf(out_path, out_size, "%s/%s", g_save_dir, sub);
        char dir_buf[512];
        strncpy(dir_buf, out_path, sizeof(dir_buf) - 1);
        dir_buf[sizeof(dir_buf) - 1] = '\0';
        for (char *s = dir_buf; *s; s++) {
            if (*s == '/' || *s == '\\') {
                char ch = *s;
                *s = '\0';
                if (dir_buf[0]) _mkdir(dir_buf);
                *s = ch;
            }
        }
        return;
    }

    if (!is_write) {
        if (_access(p, 0) == 0) {
            strncpy(out_path, p, out_size - 1);
            out_path[out_size - 1] = '\0';
            return;
        }
        char cand[512];
        snprintf(cand, sizeof(cand), "%s/%s", g_game_root, p);
        if (_access(cand, 0) == 0) {
            strncpy(out_path, cand, out_size - 1);
            out_path[out_size - 1] = '\0';
            return;
        }
    }

    strncpy(out_path, p, out_size - 1);
    out_path[out_size - 1] = '\0';
}

static FILE * CDECL shim_fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    int is_write = (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL);
    char resolved[512];
    resolve_vfs_path(path, is_write, resolved, sizeof(resolved));
    FILE *f = fopen(resolved, mode);
    if (!f && strcmp(resolved, path) != 0) {
        f = fopen(path, mode);
    }
    return f;
}

static int CDECL shim_open(const char *raw_path, int flags, ...) {
    int pmode = _S_IREAD | _S_IWRITE;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    if (!raw_path) return -1;

    int is_write = (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) != 0;
    char resolved[512];
    resolve_vfs_path(raw_path, is_write, resolved, sizeof(resolved));

    int fd = _open(resolved, flags | _O_BINARY, pmode);
    if (fd < 0 && strcmp(resolved, raw_path) != 0) {
        fd = _open(raw_path, flags | _O_BINARY, pmode);
    }
    printf("[FS] open(\"%s\" -> \"%s\", 0x%x) -> fd %d\n", raw_path, resolved, flags, fd);
    return fd;
}

static int CDECL shim_close(int fd) { return _close(fd); }
static int CDECL shim_read(int fd, void *buf, size_t count) { return _read(fd, buf, (unsigned int)count); }
static int CDECL shim_write(int fd, const void *buf, size_t count) { return _write(fd, buf, (unsigned int)count); }
static long CDECL shim_lseek(int fd, long offset, int whence) { return _lseek(fd, offset, whence); }

static int CDECL shim_fstat(int fd, void *buf) {
    if (!buf) return -1;
    memset(buf, 0, 128);

    long cur = _lseek(fd, 0, SEEK_CUR);
    long sz = _lseek(fd, 0, SEEK_END);
    _lseek(fd, cur, SEEK_SET);

    /* Bionic x86 struct stat offsets (covers both standard Bionic & Polarbit PFile::Size) */
    *(uint32_t *)((char *)buf + 0x08) = 0100644; /* S_IFREG | 0644 */
    *(uint32_t *)((char *)buf + 0x20) = (uint32_t)sz;
    *(uint32_t *)((char *)buf + 0x2c) = (uint32_t)sz;
    *(uint32_t *)((char *)buf + 0x30) = (uint32_t)sz;
    return 0;
}

static int CDECL shim_mkdir(const char *path, int mode) {
    (void)mode;
    if (!path) return -1;
    char resolved[512];
    resolve_vfs_path(path, 1, resolved, sizeof(resolved));
    _mkdir(resolved);
    return 0;
}

static int CDECL shim_rmdir(const char *path) {
    if (!path) return -1;
    char resolved[512];
    resolve_vfs_path(path, 1, resolved, sizeof(resolved));
    return _rmdir(resolved);
}

static int CDECL shim_remove(const char *path) {
    if (!path) return -1;
    char resolved[512];
    resolve_vfs_path(path, 1, resolved, sizeof(resolved));
    return remove(resolved);
}

static int CDECL shim_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;
    char rold[512], rnew[512];
    resolve_vfs_path(oldpath, 1, rold, sizeof(rold));
    resolve_vfs_path(newpath, 1, rnew, sizeof(rnew));
    remove(rnew); /* Windows rename fails if destination exists; POSIX overwrites */
    return rename(rold, rnew);
}

static int CDECL shim_fclose(void *fp) { return is_std_stream(fp) ? 0 : fclose(host_file(fp)); }
static size_t CDECL shim_fread(void *b, size_t s, size_t n, void *fp) { return fread(b, s, n, host_file(fp)); }
static size_t CDECL shim_fwrite(const void *b, size_t s, size_t n, void *fp) { return fwrite(b, s, n, host_file(fp)); }
static int CDECL shim_fseek(void *fp, long off, int whence) { return fseek(host_file(fp), off, whence); }
static long CDECL shim_ftell(void *fp) { return ftell(host_file(fp)); }
static int CDECL shim_fputs(const char *str, void *fp) { return fputs(str, host_file(fp)); }
static int CDECL shim_fputc(int c, void *fp) { return fputc(c, host_file(fp)); }
static int CDECL shim_putc(int c, void *fp) { return putc(c, host_file(fp)); }
static int CDECL shim_getc(void *fp) { return getc(host_file(fp)); }
static int CDECL shim_ungetc(int c, void *fp) { return ungetc(c, host_file(fp)); }
static char * CDECL shim_fgets(char *b, int n, void *fp) { return fgets(b, n, host_file(fp)); }
static int CDECL shim_fflush(void *fp) { return fflush(fp ? host_file(fp) : NULL); }
static int CDECL shim_setvbuf(void *fp, char *buf, int mode, size_t size) {
    return is_std_stream(fp) ? 0 : setvbuf(host_file(fp), buf, mode, size);
}

/* Forward declarations of EGL wrappers */
extern void * CDECL wrap_eglGetDisplay(void *);
extern uint32_t CDECL wrap_eglInitialize(void *, int *, int *);
extern uint32_t CDECL wrap_eglChooseConfig(void *, const int *, void **, int, int *);
extern uint32_t CDECL wrap_eglGetConfigAttrib(void *, void *, int, int *);
extern void * CDECL wrap_eglCreateWindowSurface(void *, void *, void *, const int *);
extern void * CDECL wrap_eglCreateContext(void *, void *, void *, const int *);
extern uint32_t CDECL wrap_eglMakeCurrent(void *, void *, void *, void *);
extern void * CDECL wrap_eglGetCurrentContext(void);
extern void * CDECL wrap_eglGetCurrentSurface(int);
extern uint32_t CDECL wrap_eglQuerySurface(void *, void *, int, int *);
extern uint32_t CDECL wrap_eglSwapBuffers(void *, void *);
extern int CDECL wrap_eglGetError(void);
extern uint32_t CDECL wrap_eglBindAPI(uint32_t);
extern uint32_t CDECL wrap_eglDestroyContext(void *, void *);
extern uint32_t CDECL wrap_eglDestroySurface(void *, void *);
extern uint32_t CDECL wrap_eglTerminate(void *);
extern void * CDECL wrap_eglGetProcAddress(const char *);

/* Forward declarations of OpenSL ES */
extern int32_t CDECL wrap_slCreateEngine(void **, uint32_t, void *, uint32_t, const void *, const uint32_t *);
extern const void *SL_IID_ENGINE;
extern const void *SL_IID_PLAY;
extern const void *SL_IID_BUFFERQUEUE;
extern const void *SL_IID_VOLUME;
extern const void *SL_IID_SEEK;
extern const void *SL_IID_PLAYBACKRATE;
extern const void *SL_IID_DYNAMICINTERFACEMANAGEMENT;
extern const void *SL_IID_EFFECTSEND;

/* Forward declarations of Android NDK */
extern void * CDECL wrap_AAssetManager_open(void *, const char *, int);
extern int CDECL wrap_AAsset_read(void *, void *, size_t);
extern off_t CDECL wrap_AAsset_seek(void *, off_t, int);
extern off_t CDECL wrap_AAsset_getLength(void *);
extern const void * CDECL wrap_AAsset_getBuffer(void *);
extern void CDECL wrap_AAsset_close(void *);
extern int CDECL wrap_AAsset_openFileDescriptor(void *, off_t *, off_t *);
extern void * CDECL wrap_AAssetManager_openDir(void *, const char *);
extern const char * CDECL wrap_AAssetDir_getNextFileName(void *);
extern void CDECL wrap_AAssetDir_close(void *);
extern void * CDECL wrap_AConfiguration_new(void);
extern void CDECL wrap_AConfiguration_delete(void *);
extern void CDECL wrap_AConfiguration_fromAssetManager(void *, void *);
extern void CDECL wrap_AConfiguration_getCountry(void *, char *);
extern void CDECL wrap_AConfiguration_getLanguage(void *, char *);
extern int32_t CDECL wrap_ANativeWindow_getWidth(void *);
extern int32_t CDECL wrap_ANativeWindow_getHeight(void *);
extern int32_t CDECL wrap_ANativeWindow_setBuffersGeometry(void *, int32_t, int32_t, int32_t);
extern void * CDECL wrap_ALooper_prepare(int);
extern int CDECL wrap_ALooper_addFd(void *, int, int, int, void *, void *);
extern int CDECL wrap_ALooper_pollAll(int, int *, int *, void **);
extern void CDECL wrap_ALooper_wake(void *);
extern void CDECL wrap_AInputQueue_attachLooper(void *, void *, int, void *, void *);
extern void CDECL wrap_AInputQueue_detachLooper(void *);
extern int32_t CDECL wrap_AInputQueue_getEvent(void *, void **);
extern int32_t CDECL wrap_AInputQueue_preDispatchEvent(void *, void *);
extern void CDECL wrap_AInputQueue_finishEvent(void *, void *, int);
extern int32_t CDECL wrap_AInputEvent_getType(const void *);
extern int32_t CDECL wrap_AInputEvent_getDeviceId(const void *);
extern int32_t CDECL wrap_AInputEvent_getSource(const void *);
extern int32_t CDECL wrap_AMotionEvent_getAction(const void *);
extern int32_t CDECL wrap_AMotionEvent_getFlags(const void *);
extern int32_t CDECL wrap_AMotionEvent_getMetaState(const void *);
extern size_t CDECL wrap_AMotionEvent_getPointerCount(const void *);
extern int32_t CDECL wrap_AMotionEvent_getPointerId(const void *, size_t);
extern float CDECL wrap_AMotionEvent_getX(const void *, size_t);
extern float CDECL wrap_AMotionEvent_getY(const void *, size_t);
extern size_t CDECL wrap_AMotionEvent_getHistorySize(const void *);
extern float CDECL wrap_AMotionEvent_getHistoricalX(const void *, size_t, size_t);
extern float CDECL wrap_AMotionEvent_getHistoricalY(const void *, size_t, size_t);
extern int32_t CDECL wrap_AKeyEvent_getAction(const void *);
extern int32_t CDECL wrap_AKeyEvent_getKeyCode(const void *);
extern int32_t CDECL wrap_AKeyEvent_getFlags(const void *);
extern int32_t CDECL wrap_AKeyEvent_getMetaState(const void *);
extern void CDECL wrap_ANativeActivity_finish(void *);
extern int CDECL wrap_android_getCpuFamily(void);
extern uint64_t CDECL wrap_android_getCpuFeatures(void);
extern int CDECL wrap_android_getCpuCount(void);
extern void CDECL wrap_android_setCpu(int, uint64_t);
extern void CDECL wrap_app_dummy(void);
extern int8_t CDECL wrap_android_app_read_cmd(void *);
extern void CDECL wrap_android_app_pre_exec_cmd(void *, int8_t);
extern void CDECL wrap_android_app_post_exec_cmd(void *, int8_t);
extern int CDECL wrap___android_log_print(int, const char *, const char *, ...);
extern int CDECL wrap___android_log_write(int, const char *, const char *);
extern int CDECL wrap___android_log_vprint(int, const char *, const char *, va_list);

/* POSIX shims */
static int * CDECL shim___errno(void) {
    return _errno();
}

static void CDECL shim___stack_chk_fail(void) {
    printf("[FATAL] Stack check failed!\n");
    log_flush();
    ExitProcess(1);
}

typedef struct {
    int32_t tv_sec;
    int32_t tv_nsec;
} BionicTimespec;

static int CDECL shim_clock_gettime(int clock_id, BionicTimespec *ts) {
    (void)clock_id;
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    ts->tv_sec = (int32_t)(count.QuadPart / freq.QuadPart);
    ts->tv_nsec = (int32_t)(((count.QuadPart % freq.QuadPart) * 1000000000LL) / freq.QuadPart);
    return 0;
}

static int CDECL shim_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return 0;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = (((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10;
    t -= 11644473600000000ULL;
    tv->tv_sec = (long)(t / 1000000ULL);
    tv->tv_usec = (long)(t % 1000000ULL);
    return 0;
}

static int CDECL shim_nanosleep(const BionicTimespec *req, BionicTimespec *rem) {
    (void)rem;
    if (req) {
        DWORD ms = (DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
        Sleep(ms > 0 ? ms : 1);
    }
    return 0;
}

static int CDECL shim_usleep(uint32_t usec) {
    DWORD ms = usec / 1000;
    Sleep(ms > 0 ? ms : 1);
    return 0;
}

static int32_t CDECL shim_time(int32_t *t) {
    int32_t now = (int32_t)_time32(NULL);
    if (t) *t = now;
    return now;
}

static struct tm * CDECL shim_localtime(const int32_t *t) {
    __time32_t v = t ? *t : 0;
    return _localtime32(&v);
}

static int CDECL shim_setenv(const char *name, const char *val, int overwrite) {
    (void)overwrite;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s=%s", name ? name : "", val ? val : "");
    return _putenv(buf);
}

/* Pthread shims (lazy 4-byte pointer pool) */
#define HOST_MUTEX_MAGIC 0x3158544Du /* "MTX1" */
#define HOST_COND_MAGIC  0x31444E43u /* "CND1" */
#define COND_WAIT_SLICE_MS 50

typedef struct {
    uint32_t magic;
    CRITICAL_SECTION cs;
} HostMutex;

typedef struct {
    uint32_t magic;
    CONDITION_VARIABLE cv;
} HostCond;

static volatile LONG g_mutex_count = 0;
static volatile LONG g_cond_count = 0;

static int is_host_ptr(LONG v) {
    return (uint32_t)v >= 0x10000u;
}

static HostMutex *get_host_mutex(void *mutex) {
    volatile LONG *slot = (volatile LONG *)mutex;
    for (;;) {
        LONG v = *slot;
        if (is_host_ptr(v) && ((HostMutex *)(uintptr_t)v)->magic == HOST_MUTEX_MAGIC) {
            return (HostMutex *)(uintptr_t)v;
        }
        HostMutex *m = (HostMutex *)calloc(1, sizeof(HostMutex));
        InitializeCriticalSection(&m->cs);
        m->magic = HOST_MUTEX_MAGIC;
        if (InterlockedCompareExchange(slot, (LONG)(uintptr_t)m, v) == v) {
            InterlockedIncrement(&g_mutex_count);
            return m;
        }
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}

static HostCond *get_host_cond(void *cond) {
    volatile LONG *slot = (volatile LONG *)cond;
    for (;;) {
        LONG v = *slot;
        if (is_host_ptr(v) && ((HostCond *)(uintptr_t)v)->magic == HOST_COND_MAGIC) {
            return (HostCond *)(uintptr_t)v;
        }
        HostCond *c = (HostCond *)calloc(1, sizeof(HostCond));
        InitializeConditionVariable(&c->cv);
        c->magic = HOST_COND_MAGIC;
        if (InterlockedCompareExchange(slot, (LONG)(uintptr_t)c, v) == v) {
            InterlockedIncrement(&g_cond_count);
            return c;
        }
        free(c);
    }
}

static int CDECL shim_pthread_mutex_init(void *mutex, const void *attr) {
    (void)attr;
    if (mutex) *(volatile LONG *)mutex = 0;
    return 0;
}

static int CDECL shim_pthread_mutex_destroy(void *mutex) {
    if (!mutex) return 0;
    LONG v = InterlockedExchange((volatile LONG *)mutex, 0);
    if (is_host_ptr(v)) {
        HostMutex *m = (HostMutex *)(uintptr_t)v;
        if (m->magic == HOST_MUTEX_MAGIC) {
            m->magic = 0;
            DeleteCriticalSection(&m->cs);
            free(m);
            InterlockedDecrement(&g_mutex_count);
        }
    }
    return 0;
}

static int CDECL shim_pthread_mutex_lock(void *mutex) {
    if (!mutex) return 0;
    EnterCriticalSection(&get_host_mutex(mutex)->cs);
    return 0;
}

static int CDECL shim_pthread_mutex_trylock(void *mutex) {
    if (!mutex) return 0;
    return TryEnterCriticalSection(&get_host_mutex(mutex)->cs) ? 0 : EBUSY;
}

static int CDECL shim_pthread_mutex_unlock(void *mutex) {
    if (!mutex) return 0;
    LeaveCriticalSection(&get_host_mutex(mutex)->cs);
    return 0;
}

static int CDECL shim_pthread_cond_init(void *cv, const void *attr) {
    (void)attr;
    if (cv) *(volatile LONG *)cv = 0;
    return 0;
}

static int CDECL shim_pthread_cond_destroy(void *cv) {
    if (!cv) return 0;
    LONG v = InterlockedExchange((volatile LONG *)cv, 0);
    if (is_host_ptr(v)) {
        HostCond *c = (HostCond *)(uintptr_t)v;
        if (c->magic == HOST_COND_MAGIC) {
            c->magic = 0;
            free(c);
            InterlockedDecrement(&g_cond_count);
        }
    }
    return 0;
}

static int CDECL shim_pthread_cond_wait(void *cv, void *mutex) {
    if (!cv || !mutex) return 0;
    SleepConditionVariableCS(&get_host_cond(cv)->cv, &get_host_mutex(mutex)->cs, COND_WAIT_SLICE_MS);
    return 0;
}

static int CDECL shim_pthread_cond_signal(void *cv) {
    if (cv) WakeConditionVariable(&get_host_cond(cv)->cv);
    return 0;
}

static int CDECL shim_pthread_cond_broadcast(void *cv) {
    if (cv) WakeAllConditionVariable(&get_host_cond(cv)->cv);
    return 0;
}

void shims_get_sync_stats(long *mutexes, long *conds) {
    *mutexes = g_mutex_count;
    *conds = g_cond_count;
}

/* Thread tracking */
#define MAX_THREADS 64
typedef struct {
    DWORD tid;
    HANDLE handle;
    int in_use;
} ThreadInfo;

static ThreadInfo g_threads[MAX_THREADS];
static CRITICAL_SECTION g_thread_lock;
static volatile LONG g_thread_lock_inited = 0;

static void init_thread_lock(void) {
    if (InterlockedCompareExchange(&g_thread_lock_inited, 1, 0) == 0) {
        InitializeCriticalSection(&g_thread_lock);
        memset(g_threads, 0, sizeof(g_threads));
    }
}

static void register_thread(DWORD tid, HANDLE handle) {
    init_thread_lock();
    EnterCriticalSection(&g_thread_lock);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!g_threads[i].in_use) {
            g_threads[i].tid = tid;
            g_threads[i].handle = handle;
            g_threads[i].in_use = 1;
            break;
        }
    }
    LeaveCriticalSection(&g_thread_lock);
}

static HANDLE get_thread_handle_by_tid(DWORD tid) {
    init_thread_lock();
    EnterCriticalSection(&g_thread_lock);
    HANDLE h = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (g_threads[i].in_use && g_threads[i].tid == tid) {
            h = g_threads[i].handle;
            break;
        }
    }
    LeaveCriticalSection(&g_thread_lock);
    return h;
}

typedef struct {
    void *(*start_routine)(void *);
    void *arg;
} ThreadBridge;

static DWORD WINAPI win32_thread_proc(LPVOID lpParam) {
    ThreadBridge *tb = (ThreadBridge *)lpParam;
    void *(*fn)(void *) = tb->start_routine;
    void *arg = tb->arg;
    free(tb);

    /* Commit thread stack upfront to avoid Linux GCC sub esp guard-page faults */
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    char *p = (char *)&mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi))) {
        if (mbi.AllocationBase) {
            SIZE_T size_to_commit = (SIZE_T)(p - (char *)mbi.AllocationBase);
            VirtualAlloc(mbi.AllocationBase, size_to_commit, MEM_COMMIT, PAGE_READWRITE);
        }
    }

    fn(arg);
    return 0;
}

static void CDECL shim_safe_free(void *ptr) {
    if (!ptr) return;
    /* Use HeapValidate / HeapFree or standard free if allocated in the default process heap */
    HANDLE heap = GetProcessHeap();
    if (HeapValidate(heap, 0, ptr)) {
        free(ptr);
    }
}

static int CDECL shim_pthread_create(HANDLE *thread, const void *attr, void *(*start_routine)(void *), void *arg) {
    (void)attr;
    ThreadBridge *tb = (ThreadBridge *)malloc(sizeof(ThreadBridge));
    tb->start_routine = start_routine;
    tb->arg = arg;
    DWORD tid = 0;
    HANDLE h = CreateThread(NULL, 0, win32_thread_proc, tb, 0, &tid);
    if (h) {
        register_thread(tid, h);
        watchdog_register_thread(h, "pthread");
        *thread = h;
        return 0;
    }
    return -1;
}

static int CDECL shim_pthread_join(HANDLE thread, void **retval) {
    (void)retval;
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        init_thread_lock();
        EnterCriticalSection(&g_thread_lock);
        for (int i = 0; i < MAX_THREADS; i++) {
            if (g_threads[i].in_use && g_threads[i].handle == thread) {
                g_threads[i].in_use = 0;
                break;
            }
        }
        LeaveCriticalSection(&g_thread_lock);
    }
    return 0;
}

static int CDECL shim_pthread_detach(HANDLE thread) {
    (void)thread;
    return 0;
}

static HANDLE CDECL shim_pthread_self(void) {
    DWORD tid = GetCurrentThreadId();
    HANDLE h = get_thread_handle_by_tid(tid);
    if (!h) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
        register_thread(tid, h);
    }
    return h;
}

static void CDECL shim_pthread_exit(void *retval) {
    (void)retval;
    ExitThread(0);
}

static int CDECL shim_pthread_once(int *once_control, void (*init_routine)(void)) {
    volatile LONG *ctl = (volatile LONG *)once_control;
    if (*ctl == 2) return 0;
    if (InterlockedCompareExchange(ctl, 1, 0) == 0) {
        init_routine();
        InterlockedExchange(ctl, 2);
        return 0;
    }
    while (*ctl != 2) Sleep(0);
    return 0;
}

static int CDECL shim_pthread_key_create(DWORD *key, void (*destructor)(void *)) {
    (void)destructor;
    *key = TlsAlloc();
    return (*key != TLS_OUT_OF_INDEXES) ? 0 : -1;
}

static int CDECL shim_pthread_key_delete(DWORD key) { return TlsFree(key) ? 0 : -1; }
static void * CDECL shim_pthread_getspecific(DWORD key) { return TlsGetValue(key); }
static int CDECL shim_pthread_setspecific(DWORD key, const void *value) { return TlsSetValue(key, (LPVOID)value) ? 0 : -1; }
static int CDECL shim_pthread_attr_init(void *attr) { (void)attr; return 0; }
static int CDECL shim_pthread_attr_destroy(void *attr) { (void)attr; return 0; }
static int CDECL shim_pthread_attr_setdetachstate(void *attr, int state) { (void)attr; (void)state; return 0; }
static int CDECL shim_pthread_mutexattr_init(void *attr) { (void)attr; return 0; }
static int CDECL shim_pthread_mutexattr_settype(void *attr, int type) { (void)attr; (void)type; return 0; }
static int CDECL shim_pthread_mutexattr_destroy(void *attr) { (void)attr; return 0; }
static int CDECL shim_sched_yield(void) { Sleep(0); return 0; }

/* POSIX 48-bit lrand48 / srand48 */
static volatile LONGLONG g_rand48_state = 0x1234ABCD330ELL;

static long CDECL shim_lrand48(void) {
    LONGLONG old, next;
    do {
        old = g_rand48_state;
        next = (LONGLONG)(((unsigned long long)old * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL);
    } while (InterlockedCompareExchange64(&g_rand48_state, next, old) != old);
    return (long)((unsigned long long)next >> 17);
}

const volatile long long *shims_rand48_state(void) {
    return &g_rand48_state;
}

static void CDECL shim_srand48(long seed) {
    InterlockedExchange64(&g_rand48_state, (LONGLONG)((((unsigned long long)(uint32_t)seed) << 16) | 0x330EULL));
}

static void * CDECL shim_memalign(size_t alignment, size_t size) {
    (void)alignment;
    return malloc(size);
}

static float CDECL wrap_AMotionEvent_getAxisValue(const void *event, int32_t axis, size_t pointer_index) {
    (void)event; (void)axis; (void)pointer_index;
    return 0.0f;
}

static float CDECL wrap_AMotionEvent_getHistoricalAxisValue(const void *event, int32_t axis, size_t pointer_index, size_t history_pos) {
    (void)event; (void)axis; (void)pointer_index; (void)history_pos;
    return 0.0f;
}

static void * CDECL shim_dlopen(const char *filename, int flag) {
    printf("[DL] dlopen(\"%s\", 0x%x)\n", filename ? filename : "self", flag);
    return (void *)0x12345678;
}

static int CDECL shim_dlclose(void *handle) {
    (void)handle;
    return 0;
}

static const char * CDECL shim_dlerror(void) {
    return NULL;
}

static void * CDECL shim_dlsym(void *handle, const char *symbol) {
    (void)handle;
    if (!symbol) return NULL;
    if (strcmp(symbol, "AMotionEvent_getAxisValue") == 0) return (void *)wrap_AMotionEvent_getAxisValue;
    if (strcmp(symbol, "AMotionEvent_getHistoricalAxisValue") == 0) return (void *)wrap_AMotionEvent_getHistoricalAxisValue;
    void *elf_fn = (void *)elf_sym_global(symbol);
    if (elf_fn) return elf_fn;
    return gl_get_proc(symbol);
}

static int CDECL shim_dl_iterate_phdr(void *cb, void *data) { (void)cb; (void)data; return 0; }
static int CDECL shim___cxa_atexit(void (*func)(void *), void *arg, void *dso_handle) { (void)func; (void)arg; (void)dso_handle; return 0; }
static void CDECL shim___cxa_finalize(void *dso_handle) { (void)dso_handle; }

static void CDECL shim_abort(void) {
    char ctx[256] = {0};
    arm32_describe_context(ctx, sizeof(ctx));
    printf("[SHIM] abort() called! %s\n", ctx[0] ? ctx : "(no guest context)");
    log_flush();
    ExitProcess(1);
}

static void CDECL shim___assert2(const char *file, int line, const char *func, const char *expr) {
    char ctx[256] = {0};
    arm32_describe_context(ctx, sizeof(ctx));
    printf("[SHIM] __assert2: assertion '%s' failed at %s:%d in %s (%s)\n",
           expr ? expr : "?", file ? file : "?", line, func ? func : "?",
           ctx[0] ? ctx : "(no guest context)");
    fflush(stdout);
    log_flush();
}

static void CDECL shim_exit(int code) {
    printf("[SHIM] exit(%d) called!\n", code);
    log_flush();
    ExitProcess((UINT)code);
}

static void CDECL shim_google_potentially_blocking_region_begin(void) {}
static void CDECL shim_google_potentially_blocking_region_end(void) {}

static int CDECL shim_ret0(void) { return 0; }
static int CDECL shim_syscall(int number, ...) { (void)number; return -1; }
static int CDECL shim_ioctl(int fd, unsigned long request, ...) { (void)fd; (void)request; return 0; }
static int CDECL shim_fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }
static int CDECL shim_pipe(int pipefd[2]) { return _pipe(pipefd, 4096, _O_BINARY); }
static int CDECL shim_poll(void *fds, unsigned long nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return 0; }
static int CDECL shim_writev(int fd, const void *iov, int iovcnt) { (void)fd; (void)iov; (void)iovcnt; return -1; }

/* ARM EABI (__aeabi_*) & C++ EH / Unwind shims */
static uint32_t g_stack_chk_guard = 0x595e9fbdU;
static void *g_stdin_ptr  = &bionic_sF_[0];
static void *g_stdout_ptr = &bionic_sF_[1];
static void *g_stderr_ptr = &bionic_sF_[2];
static const char *g_sys_signame[32] = { "0", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV" };

static int CDECL shim___aeabi_atexit(void *obj, void (*func)(void *), void *dso) {
    (void)obj; (void)func; (void)dso;
    return 0;
}
static void CDECL shim___aeabi_memset(void *dest, size_t n, int c) {
    memset(dest, c, n);
}
static void CDECL shim___aeabi_memclr(void *dest, size_t n) {
    memset(dest, 0, n);
}
static void * CDECL shim___gnu_Unwind_Find_exidx(void *pc, int *pcount) {
    (void)pc;
    if (pcount) *pcount = 0;
    return NULL;
}
static int CDECL shim_raise(int sig) { (void)sig; return 0; }
static void * CDECL shim_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    return VirtualAlloc(NULL, length ? length : 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
static int CDECL shim_munmap(void *addr, size_t length) {
    (void)length;
    if (addr) VirtualFree(addr, 0, MEM_RELEASE);
    return 0;
}
static int CDECL shim_posix_memalign(void **memptr, size_t alignment, size_t size) {
    (void)alignment;
    if (!memptr) return 22;
    *memptr = malloc(size);
    return *memptr ? 0 : 12;
}
static int CDECL shim_pthread_equal(uint32_t t1, uint32_t t2) { return t1 == t2; }
static int CDECL shim_strerror_r(int errnum, char *buf, size_t buflen) {
    if (buf && buflen > 0) snprintf(buf, buflen, "errno %d", errnum);
    return 0;
}
static long CDECL shim_sysconf(int name) { (void)name; return 4096; }
static const char * CDECL shim_strsignal(int sig) { (void)sig; return "SIGNAL"; }
static int CDECL shim_stat(const char *path, void *buf) {
    (void)path;
    if (buf) memset(buf, 0, 128);
    return 0;
}

/* Softfp wrappers: ARM AAPCS passes and returns 32-bit floats bitwise in R0 (EAX on x86).
 * Standard x86 C runtime math functions return on the x87 FPU stack ST(0), so we must
 * explicitly bitcast float returns to uint32_t to populate EAX and pop ST(0). */
static inline uint32_t u32_from_float(float f) {
    uint32_t u; memcpy(&u, &f, 4); return u;
}
static inline float float_from_u32(uint32_t u) {
    float f; memcpy(&f, &u, 4); return f;
}

static uint32_t CDECL shim_sinf(uint32_t a0)   { return u32_from_float(sinf(float_from_u32(a0))); }
static uint32_t CDECL shim_cosf(uint32_t a0)   { return u32_from_float(cosf(float_from_u32(a0))); }
static uint32_t CDECL shim_tanf(uint32_t a0)   { return u32_from_float(tanf(float_from_u32(a0))); }
static uint32_t CDECL shim_asinf(uint32_t a0)  { return u32_from_float(asinf(float_from_u32(a0))); }
static uint32_t CDECL shim_acosf(uint32_t a0)  { return u32_from_float(acosf(float_from_u32(a0))); }
static uint32_t CDECL shim_atanf(uint32_t a0)  { return u32_from_float(atanf(float_from_u32(a0))); }
static uint32_t CDECL shim_atan2f(uint32_t a0, uint32_t a1) { return u32_from_float(atan2f(float_from_u32(a0), float_from_u32(a1))); }
static uint32_t CDECL shim_sqrtf(uint32_t a0)  { return u32_from_float(sqrtf(float_from_u32(a0))); }
static uint32_t CDECL shim_expf(uint32_t a0)   { return u32_from_float(expf(float_from_u32(a0))); }
static uint32_t CDECL shim_logf(uint32_t a0)   { return u32_from_float(logf(float_from_u32(a0))); }
static uint32_t CDECL shim_log10f(uint32_t a0) { return u32_from_float(log10f(float_from_u32(a0))); }
static uint32_t CDECL shim_powf(uint32_t a0, uint32_t a1) { return u32_from_float(powf(float_from_u32(a0), float_from_u32(a1))); }
static uint32_t CDECL shim_floorf(uint32_t a0) {
    float x = float_from_u32(a0);
    float res = floorf(x);
    uint32_t ret = u32_from_float(res);
    printf("[SHIM] floorf(0x%08X = %f) -> %f (0x%08X)\n", a0, x, res, ret);
    return ret;
}
static uint32_t CDECL shim_ceilf(uint32_t a0)  {
    float x = float_from_u32(a0);
    float res = ceilf(x);
    uint32_t ret = u32_from_float(res);
    printf("[SHIM] ceilf(0x%08X = %f) -> %f (0x%08X)\n", a0, x, res, ret);
    return ret;
}
static uint32_t CDECL shim_fmodf(uint32_t a0, uint32_t a1) { return u32_from_float(fmodf(float_from_u32(a0), float_from_u32(a1))); }
static uint32_t CDECL shim_fabsf(uint32_t a0)  { return u32_from_float(fabsf(float_from_u32(a0))); }
static uint32_t CDECL shim_rintf(uint32_t a0)  { return u32_from_float(rintf(float_from_u32(a0))); }

/* Base host symbol table (merged at init with all GLES 1.1 & 2.0 dispatch entries) */
static const host_symbol_t g_base_symbols[] = {
    /* Bionic / Libc runtime */
    { "__android_log_print",  (uintptr_t)wrap___android_log_print },
    { "__android_log_write",  (uintptr_t)wrap___android_log_write },
    { "__android_log_vprint", (uintptr_t)wrap___android_log_vprint },
    { "__cxa_atexit",         (uintptr_t)shim___cxa_atexit },
    { "__cxa_finalize",       (uintptr_t)shim___cxa_finalize },
    { "__aeabi_atexit",       (uintptr_t)shim___aeabi_atexit },
    { "__aeabi_memcpy",       (uintptr_t)memcpy },
    { "__aeabi_memcpy4",      (uintptr_t)memcpy },
    { "__aeabi_memcpy8",      (uintptr_t)memcpy },
    { "__aeabi_memmove",      (uintptr_t)memmove },
    { "__aeabi_memmove4",     (uintptr_t)memmove },
    { "__aeabi_memmove8",     (uintptr_t)memmove },
    { "__aeabi_memset",       (uintptr_t)shim___aeabi_memset },
    { "__aeabi_memset4",      (uintptr_t)shim___aeabi_memset },
    { "__aeabi_memset8",      (uintptr_t)shim___aeabi_memset },
    { "__aeabi_memclr",       (uintptr_t)shim___aeabi_memclr },
    { "__aeabi_memclr4",      (uintptr_t)shim___aeabi_memclr },
    { "__aeabi_memclr8",      (uintptr_t)shim___aeabi_memclr },
    { "__cxa_begin_cleanup",  (uintptr_t)shim_ret0 },
    { "__cxa_type_match",     (uintptr_t)shim_ret0 },
    { "__cxa_call_unexpected",(uintptr_t)shim_abort },
    { "__gnu_Unwind_Find_exidx", (uintptr_t)shim___gnu_Unwind_Find_exidx },
    { "__assert2",            (uintptr_t)shim___assert2 },
    { "__errno",              (uintptr_t)shim___errno },
    { "__google_potentially_blocking_region_begin", (uintptr_t)shim_google_potentially_blocking_region_begin },
    { "__google_potentially_blocking_region_end",   (uintptr_t)shim_google_potentially_blocking_region_end },
    { "__sF",                 (uintptr_t)bionic_sF_ },
    { "stdin",                (uintptr_t)&g_stdin_ptr },
    { "stdout",               (uintptr_t)&g_stdout_ptr },
    { "stderr",               (uintptr_t)&g_stderr_ptr },
    { "sys_signame",          (uintptr_t)g_sys_signame },
    { "__stack_chk_fail",     (uintptr_t)shim___stack_chk_fail },
    { "__stack_chk_guard",    (uintptr_t)&g_stack_chk_guard },
    { "_ctype_",              (uintptr_t)&bionic_ctype_ptr },
    { "_tolower_tab_",        (uintptr_t)&bionic_tolower_tab_ptr },
    { "_toupper_tab_",        (uintptr_t)&bionic_toupper_tab_ptr },
    { "abort",                (uintptr_t)shim_abort },
    { "raise",                (uintptr_t)shim_raise },
    { "exit",                 (uintptr_t)shim_exit },
    { "getuid",               (uintptr_t)shim_ret0 },
    { "geteuid",              (uintptr_t)shim_ret0 },
    { "getgid",               (uintptr_t)shim_ret0 },
    { "getegid",              (uintptr_t)shim_ret0 },
    { "getpid",               (uintptr_t)shim_ret0 },
    { "mmap",                 (uintptr_t)shim_mmap },
    { "munmap",               (uintptr_t)shim_munmap },
    { "dup2",                 (uintptr_t)_dup2 },
    { "stat",                 (uintptr_t)shim_stat },
    { "lstat",                (uintptr_t)shim_stat },
    { "setjmp",               (uintptr_t)shim_ret0 },
    { "_setjmp",              (uintptr_t)shim_ret0 },
    { "longjmp",              (uintptr_t)shim_ret0 },
    { "_longjmp",             (uintptr_t)shim_ret0 },
    { "sigaction",            (uintptr_t)shim_ret0 },
    { "sigaltstack",          (uintptr_t)shim_ret0 },
    { "strsignal",            (uintptr_t)shim_strsignal },
    { "dladdr",               (uintptr_t)shim_ret0 },
    { "sysconf",              (uintptr_t)shim_sysconf },
    { "strerror_r",           (uintptr_t)shim_strerror_r },
    { "pthread_equal",        (uintptr_t)shim_pthread_equal },
    { "pthread_cond_timedwait",(uintptr_t)shim_ret0 },
    { "glShaderBinary",       (uintptr_t)shim_ret0 },
    { "glReleaseShaderCompiler",(uintptr_t)shim_ret0 },
    { "calloc",               (uintptr_t)calloc },
    { "malloc",               (uintptr_t)malloc },
    { "free",                 (uintptr_t)shim_safe_free },
    { "realloc",              (uintptr_t)realloc },
    { "memalign",             (uintptr_t)shim_memalign },
    { "posix_memalign",       (uintptr_t)shim_posix_memalign },
    { "memcpy",               (uintptr_t)memcpy },
    { "memmove",              (uintptr_t)memmove },
    { "memset",               (uintptr_t)memset },
    { "memchr",               (uintptr_t)memchr },
    { "memcmp",               (uintptr_t)memcmp },
    { "strlen",               (uintptr_t)strlen },
    { "strcpy",               (uintptr_t)strcpy },
    { "strncpy",              (uintptr_t)strncpy },
    { "strcat",               (uintptr_t)strcat },
    { "strncat",              (uintptr_t)strncat },
    { "strchr",               (uintptr_t)strchr },
    { "strrchr",              (uintptr_t)strrchr },
    { "strstr",               (uintptr_t)strstr },
    { "strcmp",               (uintptr_t)strcmp },
    { "strncmp",              (uintptr_t)strncmp },
    { "strcasecmp",           (uintptr_t)_stricmp },
    { "strncasecmp",          (uintptr_t)_strnicmp },
    { "strdup",               (uintptr_t)_strdup },
    { "strtod",               (uintptr_t)strtod },
    { "strtol",               (uintptr_t)strtol },
    { "strtoul",              (uintptr_t)strtoul },
    { "strtoll",              (uintptr_t)strtoll },
    { "strtoull",             (uintptr_t)strtoull },
    { "strtoimax",            (uintptr_t)strtoll },
    { "strtoumax",            (uintptr_t)strtoull },
    { "sprintf",              (uintptr_t)sprintf },
    { "snprintf",             (uintptr_t)snprintf },
    { "sscanf",               (uintptr_t)sscanf },
    { "vsscanf",              (uintptr_t)vsscanf },
    { "vasprintf",            (uintptr_t)shim_ret0 },
    { "fseeko",               (uintptr_t)shim_fseek },
    { "ftello",               (uintptr_t)shim_ftell },
    { "vsnprintf",            (uintptr_t)vsnprintf },
    { "vsprintf",             (uintptr_t)vsprintf },
    { "printf",               (uintptr_t)printf },
    { "fprintf",              (uintptr_t)fprintf },
    { "vfprintf",             (uintptr_t)vfprintf },
    { "strerror",             (uintptr_t)strerror },
    { "strxfrm",              (uintptr_t)strxfrm },
    { "strcoll",              (uintptr_t)strcoll },
    { "atoi",                 (uintptr_t)atoi },
    { "atof",                 (uintptr_t)atof },
    { "qsort",                (uintptr_t)qsort },
    { "bsearch",              (uintptr_t)bsearch },
    { "rand",                 (uintptr_t)rand },
    { "srand",                (uintptr_t)srand },
    { "getenv",               (uintptr_t)getenv },
    { "setenv",               (uintptr_t)shim_setenv },

    /* Math (using softfp bitcast shims for float functions) */
    { "sin",                  (uintptr_t)sin },
    { "cos",                  (uintptr_t)cos },
    { "tan",                  (uintptr_t)tan },
    { "sinf",                 (uintptr_t)shim_sinf },
    { "cosf",                 (uintptr_t)shim_cosf },
    { "tanf",                 (uintptr_t)shim_tanf },
    { "asin",                 (uintptr_t)asin },
    { "asinf",                (uintptr_t)shim_asinf },
    { "acos",                 (uintptr_t)acos },
    { "acosf",                (uintptr_t)shim_acosf },
    { "atan",                 (uintptr_t)atan },
    { "atanf",                (uintptr_t)shim_atanf },
    { "atan2",                (uintptr_t)atan2 },
    { "atan2f",               (uintptr_t)shim_atan2f },
    { "sinh",                 (uintptr_t)sinh },
    { "cosh",                 (uintptr_t)cosh },
    { "tanh",                 (uintptr_t)tanh },
    { "sqrt",                 (uintptr_t)sqrt },
    { "sqrtf",                (uintptr_t)shim_sqrtf },
    { "exp",                  (uintptr_t)exp },
    { "expf",                 (uintptr_t)shim_expf },
    { "log",                  (uintptr_t)log },
    { "logf",                 (uintptr_t)shim_logf },
    { "log10",                (uintptr_t)log10 },
    { "log10f",               (uintptr_t)shim_log10f },
    { "pow",                  (uintptr_t)pow },
    { "powf",                 (uintptr_t)shim_powf },
    { "floor",                (uintptr_t)floor },
    { "floorf",               (uintptr_t)shim_floorf },
    { "ceil",                 (uintptr_t)ceil },
    { "ceilf",                (uintptr_t)shim_ceilf },
    { "fmod",                 (uintptr_t)fmod },
    { "fmodf",                (uintptr_t)shim_fmodf },
    { "modf",                 (uintptr_t)modf },
    { "modff",                (uintptr_t)modff },
    { "frexp",                (uintptr_t)frexp },
    { "ldexp",                (uintptr_t)ldexp },
    { "rint",                 (uintptr_t)rint },
    { "rintf",                (uintptr_t)shim_rintf },
    { "fabsf",                (uintptr_t)shim_fabsf },
    { "lrand48",              (uintptr_t)shim_lrand48 },
    { "srand48",              (uintptr_t)shim_srand48 },

    /* File IO & VFS */
    { "fopen",                (uintptr_t)shim_fopen },
    { "fclose",               (uintptr_t)shim_fclose },
    { "fread",                (uintptr_t)shim_fread },
    { "fwrite",               (uintptr_t)shim_fwrite },
    { "fseek",                (uintptr_t)shim_fseek },
    { "ftell",                (uintptr_t)shim_ftell },
    { "fputs",                (uintptr_t)shim_fputs },
    { "fputc",                (uintptr_t)shim_fputc },
    { "fgets",                (uintptr_t)shim_fgets },
    { "fflush",               (uintptr_t)shim_fflush },
    { "setvbuf",              (uintptr_t)shim_setvbuf },
    { "remove",               (uintptr_t)shim_remove },
    { "unlink",               (uintptr_t)shim_remove },
    { "rename",               (uintptr_t)shim_rename },
    { "mkdir",                (uintptr_t)shim_mkdir },
    { "rmdir",                (uintptr_t)shim_rmdir },
    { "open",                 (uintptr_t)shim_open },
    { "close",                (uintptr_t)shim_close },
    { "read",                 (uintptr_t)shim_read },
    { "write",                (uintptr_t)shim_write },
    { "lseek",                (uintptr_t)shim_lseek },
    { "fstat",                (uintptr_t)shim_fstat },
    { "fdopen",               (uintptr_t)fdopen },
    { "getc",                 (uintptr_t)shim_getc },
    { "putc",                 (uintptr_t)shim_putc },
    { "ungetc",               (uintptr_t)shim_ungetc },
    { "fcntl",                (uintptr_t)shim_fcntl },
    { "ioctl",                (uintptr_t)shim_ioctl },
    { "pipe",                 (uintptr_t)shim_pipe },
    { "poll",                 (uintptr_t)shim_poll },
    { "writev",               (uintptr_t)shim_writev },
    { "syscall",              (uintptr_t)shim_syscall },

    /* Sockets (Winsock2) */
    { "socket",               (uintptr_t)socket },
    { "bind",                 (uintptr_t)bind },
    { "connect",              (uintptr_t)connect },
    { "listen",               (uintptr_t)listen },
    { "accept",               (uintptr_t)accept },
    { "send",                 (uintptr_t)send },
    { "sendto",               (uintptr_t)sendto },
    { "recv",                 (uintptr_t)recv },
    { "recvfrom",             (uintptr_t)recvfrom },
    { "select",               (uintptr_t)select },
    { "shutdown",             (uintptr_t)shutdown },
    { "getsockopt",           (uintptr_t)getsockopt },
    { "setsockopt",           (uintptr_t)setsockopt },
    { "getpeername",          (uintptr_t)getpeername },
    { "getsockname",          (uintptr_t)getsockname },
    { "getaddrinfo",          (uintptr_t)getaddrinfo },
    { "freeaddrinfo",         (uintptr_t)freeaddrinfo },
    { "gai_strerror",         (uintptr_t)gai_strerror },

    /* Time */
    { "time",                 (uintptr_t)shim_time },
    { "clock",                (uintptr_t)clock },
    { "clock_gettime",        (uintptr_t)shim_clock_gettime },
    { "gettimeofday",         (uintptr_t)shim_gettimeofday },
    { "localtime",            (uintptr_t)shim_localtime },
    { "strftime",             (uintptr_t)strftime },
    { "mktime",               (uintptr_t)mktime },
    { "nanosleep",            (uintptr_t)shim_nanosleep },
    { "usleep",               (uintptr_t)shim_usleep },
    { "difftime",             (uintptr_t)difftime },

    /* Pthreads */
    { "pthread_create",       (uintptr_t)shim_pthread_create },
    { "pthread_join",         (uintptr_t)shim_pthread_join },
    { "pthread_detach",       (uintptr_t)shim_pthread_detach },
    { "pthread_self",         (uintptr_t)shim_pthread_self },
    { "pthread_exit",         (uintptr_t)shim_pthread_exit },
    { "pthread_mutex_init",   (uintptr_t)shim_pthread_mutex_init },
    { "pthread_mutex_destroy",(uintptr_t)shim_pthread_mutex_destroy },
    { "pthread_mutex_lock",   (uintptr_t)shim_pthread_mutex_lock },
    { "pthread_mutex_trylock",(uintptr_t)shim_pthread_mutex_trylock },
    { "pthread_mutex_unlock", (uintptr_t)shim_pthread_mutex_unlock },
    { "pthread_cond_init",    (uintptr_t)shim_pthread_cond_init },
    { "pthread_cond_destroy", (uintptr_t)shim_pthread_cond_destroy },
    { "pthread_cond_wait",    (uintptr_t)shim_pthread_cond_wait },
    { "pthread_cond_signal",  (uintptr_t)shim_pthread_cond_signal },
    { "pthread_cond_broadcast",(uintptr_t)shim_pthread_cond_broadcast },
    { "pthread_once",         (uintptr_t)shim_pthread_once },
    { "pthread_key_create",   (uintptr_t)shim_pthread_key_create },
    { "pthread_key_delete",   (uintptr_t)shim_pthread_key_delete },
    { "pthread_getspecific",  (uintptr_t)shim_pthread_getspecific },
    { "pthread_setspecific",  (uintptr_t)shim_pthread_setspecific },
    { "pthread_attr_init",    (uintptr_t)shim_pthread_attr_init },
    { "pthread_attr_destroy", (uintptr_t)shim_pthread_attr_destroy },
    { "pthread_attr_setdetachstate", (uintptr_t)shim_pthread_attr_setdetachstate },
    { "pthread_mutexattr_init",      (uintptr_t)shim_pthread_mutexattr_init },
    { "pthread_mutexattr_settype",   (uintptr_t)shim_pthread_mutexattr_settype },
    { "pthread_mutexattr_destroy",   (uintptr_t)shim_pthread_mutexattr_destroy },
    { "sched_yield",          (uintptr_t)shim_sched_yield },

    /* Wchar / Ctype */
    { "btowc",                (uintptr_t)btowc },
    { "getwc",                (uintptr_t)getwc },
    { "putwc",                (uintptr_t)putwc },
    { "ungetwc",              (uintptr_t)ungetwc },
    { "iswctype",             (uintptr_t)iswctype },
    { "wctype",               (uintptr_t)wctype },
    { "mbrtowc",              (uintptr_t)mbrtowc },
    { "wcrtomb",              (uintptr_t)wcrtomb },
    { "wcscoll",              (uintptr_t)wcscoll },
    { "wcsftime",             (uintptr_t)wcsftime },
    { "wcslen",               (uintptr_t)wcslen },
    { "wcsxfrm",              (uintptr_t)wcsxfrm },
    { "wctob",                (uintptr_t)wctob },
    { "wmemchr",              (uintptr_t)wmemchr },
    { "wmemcmp",              (uintptr_t)wmemcmp },
    { "wmemcpy",              (uintptr_t)wmemcpy },
    { "wmemmove",             (uintptr_t)wmemmove },
    { "wmemset",              (uintptr_t)wmemset },
    { "towlower",             (uintptr_t)towlower },
    { "towupper",             (uintptr_t)towupper },
    { "setlocale",            (uintptr_t)setlocale },

    /* Dynamic linking */
    { "dlopen",               (uintptr_t)shim_dlopen },
    { "dlsym",                (uintptr_t)shim_dlsym },
    { "dlclose",              (uintptr_t)shim_dlclose },
    { "dlerror",              (uintptr_t)shim_dlerror },
    { "dl_iterate_phdr",      (uintptr_t)shim_dl_iterate_phdr },

    /* Android NDK APIs */
    { "AAsset_close",         (uintptr_t)wrap_AAsset_close },
    { "AAsset_getBuffer",     (uintptr_t)wrap_AAsset_getBuffer },
    { "AAsset_getLength",     (uintptr_t)wrap_AAsset_getLength },
    { "AAsset_openFileDescriptor", (uintptr_t)wrap_AAsset_openFileDescriptor },
    { "AAsset_read",          (uintptr_t)wrap_AAsset_read },
    { "AAsset_seek",          (uintptr_t)wrap_AAsset_seek },
    { "AAssetDir_close",      (uintptr_t)wrap_AAssetDir_close },
    { "AAssetDir_getNextFileName", (uintptr_t)wrap_AAssetDir_getNextFileName },
    { "AAssetManager_open",   (uintptr_t)wrap_AAssetManager_open },
    { "AAssetManager_openDir",(uintptr_t)wrap_AAssetManager_openDir },
    { "AConfiguration_delete",(uintptr_t)wrap_AConfiguration_delete },
    { "AConfiguration_fromAssetManager", (uintptr_t)wrap_AConfiguration_fromAssetManager },
    { "AConfiguration_getCountry", (uintptr_t)wrap_AConfiguration_getCountry },
    { "AConfiguration_getLanguage",(uintptr_t)wrap_AConfiguration_getLanguage },
    { "AConfiguration_new",   (uintptr_t)wrap_AConfiguration_new },
    { "AInputEvent_getDeviceId", (uintptr_t)wrap_AInputEvent_getDeviceId },
    { "AInputEvent_getSource", (uintptr_t)wrap_AInputEvent_getSource },
    { "AInputEvent_getType",  (uintptr_t)wrap_AInputEvent_getType },
    { "AInputQueue_attachLooper", (uintptr_t)wrap_AInputQueue_attachLooper },
    { "AInputQueue_detachLooper", (uintptr_t)wrap_AInputQueue_detachLooper },
    { "AInputQueue_finishEvent", (uintptr_t)wrap_AInputQueue_finishEvent },
    { "AInputQueue_getEvent", (uintptr_t)wrap_AInputQueue_getEvent },
    { "AInputQueue_preDispatchEvent", (uintptr_t)wrap_AInputQueue_preDispatchEvent },
    { "AKeyEvent_getAction",  (uintptr_t)wrap_AKeyEvent_getAction },
    { "AKeyEvent_getFlags",   (uintptr_t)wrap_AKeyEvent_getFlags },
    { "AKeyEvent_getKeyCode", (uintptr_t)wrap_AKeyEvent_getKeyCode },
    { "AKeyEvent_getMetaState", (uintptr_t)wrap_AKeyEvent_getMetaState },
    { "ALooper_addFd",        (uintptr_t)wrap_ALooper_addFd },
    { "ALooper_pollAll",      (uintptr_t)wrap_ALooper_pollAll },
    { "ALooper_prepare",      (uintptr_t)wrap_ALooper_prepare },
    { "ALooper_wake",         (uintptr_t)wrap_ALooper_wake },
    { "AMotionEvent_getAction", (uintptr_t)wrap_AMotionEvent_getAction },
    { "AMotionEvent_getFlags",(uintptr_t)wrap_AMotionEvent_getFlags },
    { "AMotionEvent_getHistoricalX", (uintptr_t)wrap_AMotionEvent_getHistoricalX },
    { "AMotionEvent_getHistoricalY", (uintptr_t)wrap_AMotionEvent_getHistoricalY },
    { "AMotionEvent_getHistorySize", (uintptr_t)wrap_AMotionEvent_getHistorySize },
    { "AMotionEvent_getMetaState", (uintptr_t)wrap_AMotionEvent_getMetaState },
    { "AMotionEvent_getPointerCount", (uintptr_t)wrap_AMotionEvent_getPointerCount },
    { "AMotionEvent_getPointerId", (uintptr_t)wrap_AMotionEvent_getPointerId },
    { "AMotionEvent_getX",    (uintptr_t)wrap_AMotionEvent_getX },
    { "AMotionEvent_getY",    (uintptr_t)wrap_AMotionEvent_getY },
    { "ANativeActivity_finish", (uintptr_t)wrap_ANativeActivity_finish },
    { "ANativeWindow_getHeight", (uintptr_t)wrap_ANativeWindow_getHeight },
    { "ANativeWindow_getWidth",  (uintptr_t)wrap_ANativeWindow_getWidth },
    { "ANativeWindow_setBuffersGeometry", (uintptr_t)wrap_ANativeWindow_setBuffersGeometry },
    { "android_app_post_exec_cmd", (uintptr_t)wrap_android_app_post_exec_cmd },
    { "android_app_pre_exec_cmd",  (uintptr_t)wrap_android_app_pre_exec_cmd },
    { "android_app_read_cmd",      (uintptr_t)wrap_android_app_read_cmd },
    { "android_getCpuCount",       (uintptr_t)wrap_android_getCpuCount },
    { "android_getCpuFamily",      (uintptr_t)wrap_android_getCpuFamily },
    { "android_getCpuFeatures",    (uintptr_t)wrap_android_getCpuFeatures },
    { "android_setCpu",            (uintptr_t)wrap_android_setCpu },
    { "app_dummy",                 (uintptr_t)wrap_app_dummy },

    /* EGL APIs */
    { "eglBindAPI",                (uintptr_t)wrap_eglBindAPI },
    { "eglChooseConfig",           (uintptr_t)wrap_eglChooseConfig },
    { "eglCreateContext",          (uintptr_t)wrap_eglCreateContext },
    { "eglCreateWindowSurface",    (uintptr_t)wrap_eglCreateWindowSurface },
    { "eglDestroyContext",         (uintptr_t)wrap_eglDestroyContext },
    { "eglDestroySurface",         (uintptr_t)wrap_eglDestroySurface },
    { "eglGetConfigAttrib",        (uintptr_t)wrap_eglGetConfigAttrib },
    { "eglGetCurrentContext",      (uintptr_t)wrap_eglGetCurrentContext },
    { "eglGetCurrentSurface",      (uintptr_t)wrap_eglGetCurrentSurface },
    { "eglGetDisplay",             (uintptr_t)wrap_eglGetDisplay },
    { "eglGetError",               (uintptr_t)wrap_eglGetError },
    { "eglGetProcAddress",         (uintptr_t)wrap_eglGetProcAddress },
    { "eglInitialize",             (uintptr_t)wrap_eglInitialize },
    { "eglMakeCurrent",            (uintptr_t)wrap_eglMakeCurrent },
    { "eglQuerySurface",           (uintptr_t)wrap_eglQuerySurface },
    { "eglSwapBuffers",            (uintptr_t)wrap_eglSwapBuffers },
    { "eglTerminate",              (uintptr_t)wrap_eglTerminate },

    /* OpenSL ES */
    { "slCreateEngine",            (uintptr_t)wrap_slCreateEngine },
    { "SL_IID_BUFFERQUEUE",        (uintptr_t)&SL_IID_BUFFERQUEUE },
    { "SL_IID_DYNAMICINTERFACEMANAGEMENT", (uintptr_t)&SL_IID_DYNAMICINTERFACEMANAGEMENT },
    { "SL_IID_EFFECTSEND",         (uintptr_t)&SL_IID_EFFECTSEND },
    { "SL_IID_ENGINE",             (uintptr_t)&SL_IID_ENGINE },
    { "SL_IID_PLAY",               (uintptr_t)&SL_IID_PLAY },
    { "SL_IID_PLAYBACKRATE",       (uintptr_t)&SL_IID_PLAYBACKRATE },
    { "SL_IID_SEEK",               (uintptr_t)&SL_IID_SEEK },
    { "SL_IID_VOLUME",             (uintptr_t)&SL_IID_VOLUME }
};

static host_symbol_t *g_combined_symbols = NULL;
static size_t g_combined_count = 0;

static void build_combined_symbol_table(void) {
    if (g_combined_symbols) return;
    size_t base_n = sizeof(g_base_symbols) / sizeof(g_base_symbols[0]);
    size_t gl_n = 0;
    const gl_dispatch_entry_t *gl_entries = gl_wrap_get_dispatch_table(&gl_n);

    g_combined_symbols = (host_symbol_t *)calloc(base_n + gl_n, sizeof(host_symbol_t));
    memcpy(g_combined_symbols, g_base_symbols, base_n * sizeof(host_symbol_t));
    size_t out_idx = base_n;
    for (size_t i = 0; i < gl_n; i++) {
        /* Skip eglGetProcAddress / eglGetError duplicates already in g_base_symbols */
        if (strncmp(gl_entries[i].name, "egl", 3) == 0) continue;
        g_combined_symbols[out_idx].name = gl_entries[i].name;
        g_combined_symbols[out_idx].func = (uintptr_t)gl_entries[i].fn;
        out_idx++;
    }
    g_combined_count = out_idx;
}

void shims_init(void) {
    timeBeginPeriod(1);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    for (int i = 0; i < 256; i++) {
        unsigned char mask = 0;
        if (isupper(i)) mask |= 0x01;
        if (islower(i)) mask |= 0x02;
        if (isdigit(i)) mask |= 0x04;
        if (isspace(i)) mask |= 0x08;
        if (ispunct(i)) mask |= 0x10;
        if (iscntrl(i)) mask |= 0x20;
        if (isxdigit(i)) mask |= 0x40;
        if (isblank(i)) mask |= 0x80;
        bionic_ctype_[i + 1] = mask;
        bionic_tolower_tab_[i + 1] = (short)tolower(i);
        bionic_toupper_tab_[i + 1] = (short)toupper(i);
    }
    bionic_ctype_[0] = 0;
    bionic_tolower_tab_[0] = -1;
    bionic_toupper_tab_[0] = -1;

    init_thread_lock();
    build_combined_symbol_table();
}

const host_symbol_t *shims_get_table(size_t *count) {
    if (!g_combined_symbols) build_combined_symbol_table();
    if (count) *count = g_combined_count;
    return g_combined_symbols;
}
