#ifndef GL_WRAP_H
#define GL_WRAP_H

#include <stdint.h>
#include <stddef.h>
#include <windows.h>
#include <SDL2/SDL_opengl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t GLfixed;
typedef int32_t GLclampx;

/* Initialize OpenGL 1.1 / 2.0 extension pointers. Must be called with a current GL context. */
void gl_wrap_init(void);

/* Configure texture quality enhancements:
 * - auto_mipmaps: if non-zero, automatically generate mipmaps for static level-0 2D textures
 *   and upgrade GL_LINEAR to GL_LINEAR_MIPMAP_LINEAR.
 * - anisotropy: max anisotropic filtering level (1..16). Always applied to mipmapped textures. */
void gl_wrap_set_texture_quality(int auto_mipmaps, int anisotropy);

/* Legacy alias for setting anisotropy before or after context creation. */
void gl_wrap_set_anisotropy(int level);

/* Optional directory containing replacement HD textures named tex_<8hex>.tga */
void gl_wrap_set_mod_dir(const char *mod_dir);

/* Framebuffer the game gets when it binds framebuffer 0 (0 = the window). */
void gl_wrap_set_default_framebuffer(unsigned int fbo);

/* Dynamic GL / GLES symbol lookup for dlsym() and eglGetProcAddress(). */
void *gl_get_proc(const char *name);

/* Retrieve the full GL dispatch table so shims.c can also register all GL symbols for ELF relocation. */
typedef struct {
    const char *name;
    void *fn;
} gl_dispatch_entry_t;

const gl_dispatch_entry_t *gl_wrap_get_dispatch_table(size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* GL_WRAP_H */
