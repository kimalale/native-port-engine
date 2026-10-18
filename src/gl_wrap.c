#include "gl_wrap.h"
#include "gl_buffers.h"
#include "mesh_dump.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CDECL
#undef CDECL
#endif
#define CDECL __attribute__((cdecl))
#define FIX2FLT(x) ((float)(x) / 65536.0f)

/* ------------------------------------------------------------- */
/* OPENGL EXTENSION FUNCTION POINTERS                            */
/* ------------------------------------------------------------- */
typedef void (APIENTRY *PFN_glActiveTexture)(GLenum);
typedef void (APIENTRY *PFN_glClientActiveTexture)(GLenum);
typedef void (APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_glBindAttribLocation)(GLuint, GLuint, const GLchar *);
typedef void (APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glBlendColor)(GLclampf, GLclampf, GLclampf, GLclampf);
typedef void (APIENTRY *PFN_glBlendEquation)(GLenum);
typedef void (APIENTRY *PFN_glBlendEquationSeparate)(GLenum, GLenum);
typedef void (APIENTRY *PFN_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
typedef void (APIENTRY *PFN_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (APIENTRY *PFN_glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);
typedef void (APIENTRY *PFN_glCompileShader)(GLuint);
typedef void (APIENTRY *PFN_glCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
typedef void (APIENTRY *PFN_glCompressedTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *);
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum);
typedef void (APIENTRY *PFN_glDeleteBuffers)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_glDeleteProgram)(GLuint);
typedef void (APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_glDeleteShader)(GLuint);
typedef void (APIENTRY *PFN_glDetachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_glDisableVertexAttribArray)(GLuint);
typedef void (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef void (APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void (APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glGenerateMipmap)(GLenum);
typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glGetActiveAttrib)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef void (APIENTRY *PFN_glGetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef void (APIENTRY *PFN_glGetAttachedShaders)(GLuint, GLsizei, GLsizei *, GLuint *);
typedef GLint (APIENTRY *PFN_glGetAttribLocation)(GLuint, const GLchar *);
typedef void (APIENTRY *PFN_glGetBufferParameteriv)(GLenum, GLenum, GLint *);
typedef void (APIENTRY *PFN_glGetFramebufferAttachmentParameteriv)(GLenum, GLenum, GLenum, GLint *);
typedef void (APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_glGetRenderbufferParameteriv)(GLenum, GLenum, GLint *);
typedef void (APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (APIENTRY *PFN_glGetShaderPrecisionFormat)(GLenum, GLenum, GLint *, GLint *);
typedef void (APIENTRY *PFN_glGetShaderSource)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint *);
typedef GLint (APIENTRY *PFN_glGetUniformLocation)(GLuint, const GLchar *);
typedef void (APIENTRY *PFN_glGetUniformfv)(GLuint, GLint, GLfloat *);
typedef void (APIENTRY *PFN_glGetUniformiv)(GLuint, GLint, GLint *);
typedef void (APIENTRY *PFN_glGetVertexAttribPointerv)(GLuint, GLenum, void **);
typedef void (APIENTRY *PFN_glGetVertexAttribfv)(GLuint, GLenum, GLfloat *);
typedef void (APIENTRY *PFN_glGetVertexAttribiv)(GLuint, GLenum, GLint *);
typedef GLboolean (APIENTRY *PFN_glIsFramebuffer)(GLuint);
typedef GLboolean (APIENTRY *PFN_glIsProgram)(GLuint);
typedef GLboolean (APIENTRY *PFN_glIsRenderbuffer)(GLuint);
typedef GLboolean (APIENTRY *PFN_glIsShader)(GLuint);
typedef void (APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void (APIENTRY *PFN_glMultiTexCoord4f)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glPointParameterf)(GLenum, GLfloat);
typedef void (APIENTRY *PFN_glPointParameterfv)(GLenum, const GLfloat *);
typedef void (APIENTRY *PFN_glReleaseShaderCompiler)(void);
typedef void (APIENTRY *PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_glSampleCoverage)(GLclampf, GLboolean);
typedef void (APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void (APIENTRY *PFN_glStencilFuncSeparate)(GLenum, GLenum, GLint, GLuint);
typedef void (APIENTRY *PFN_glStencilMaskSeparate)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glStencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
typedef void (APIENTRY *PFN_glUniform1f)(GLint, GLfloat);
typedef void (APIENTRY *PFN_glUniform1fv)(GLint, GLsizei, const GLfloat *);
typedef void (APIENTRY *PFN_glUniform1i)(GLint, GLint);
typedef void (APIENTRY *PFN_glUniform1iv)(GLint, GLsizei, const GLint *);
typedef void (APIENTRY *PFN_glUniform2f)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glUniform2fv)(GLint, GLsizei, const GLfloat *);
typedef void (APIENTRY *PFN_glUniform2i)(GLint, GLint, GLint);
typedef void (APIENTRY *PFN_glUniform2iv)(GLint, GLsizei, const GLint *);
typedef void (APIENTRY *PFN_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glUniform3fv)(GLint, GLsizei, const GLfloat *);
typedef void (APIENTRY *PFN_glUniform3i)(GLint, GLint, GLint, GLint);
typedef void (APIENTRY *PFN_glUniform3iv)(GLint, GLsizei, const GLint *);
typedef void (APIENTRY *PFN_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glUniform4fv)(GLint, GLsizei, const GLfloat *);
typedef void (APIENTRY *PFN_glUniform4i)(GLint, GLint, GLint, GLint, GLint);
typedef void (APIENTRY *PFN_glUniform4iv)(GLint, GLsizei, const GLint *);
typedef void (APIENTRY *PFN_glUniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (APIENTRY *PFN_glUniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (APIENTRY *PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (APIENTRY *PFN_glUseProgram)(GLuint);
typedef void (APIENTRY *PFN_glValidateProgram)(GLuint);
typedef void (APIENTRY *PFN_glVertexAttrib1f)(GLuint, GLfloat);
typedef void (APIENTRY *PFN_glVertexAttrib1fv)(GLuint, const GLfloat *);
typedef void (APIENTRY *PFN_glVertexAttrib2f)(GLuint, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glVertexAttrib2fv)(GLuint, const GLfloat *);
typedef void (APIENTRY *PFN_glVertexAttrib3f)(GLuint, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glVertexAttrib3fv)(GLuint, const GLfloat *);
typedef void (APIENTRY *PFN_glVertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glVertexAttrib4fv)(GLuint, const GLfloat *);
typedef void (APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);

static PFN_glActiveTexture pfn_glActiveTexture = NULL;
static PFN_glClientActiveTexture pfn_glClientActiveTexture = NULL;
static PFN_glAttachShader pfn_glAttachShader = NULL;
static PFN_glBindAttribLocation pfn_glBindAttribLocation = NULL;
static PFN_glBindBuffer pfn_glBindBuffer = NULL;
static PFN_glBindFramebuffer pfn_glBindFramebuffer = NULL;
static PFN_glBindRenderbuffer pfn_glBindRenderbuffer = NULL;
static PFN_glBlendColor pfn_glBlendColor = NULL;
static PFN_glBlendEquation pfn_glBlendEquation = NULL;
static PFN_glBlendEquationSeparate pfn_glBlendEquationSeparate = NULL;
static PFN_glBlendFuncSeparate pfn_glBlendFuncSeparate = NULL;
static PFN_glBufferData pfn_glBufferData = NULL;
static PFN_glBufferSubData pfn_glBufferSubData = NULL;
static PFN_glCheckFramebufferStatus pfn_glCheckFramebufferStatus = NULL;
static PFN_glCompileShader pfn_glCompileShader = NULL;
static PFN_glCompressedTexImage2D pfn_glCompressedTexImage2D = NULL;
static PFN_glCompressedTexSubImage2D pfn_glCompressedTexSubImage2D = NULL;
static PFN_glCreateProgram pfn_glCreateProgram = NULL;
static PFN_glCreateShader pfn_glCreateShader = NULL;
static PFN_glDeleteBuffers pfn_glDeleteBuffers = NULL;
static PFN_glDeleteFramebuffers pfn_glDeleteFramebuffers = NULL;
static PFN_glDeleteProgram pfn_glDeleteProgram = NULL;
static PFN_glDeleteRenderbuffers pfn_glDeleteRenderbuffers = NULL;
static PFN_glDeleteShader pfn_glDeleteShader = NULL;
static PFN_glDetachShader pfn_glDetachShader = NULL;
static PFN_glDisableVertexAttribArray pfn_glDisableVertexAttribArray = NULL;
static PFN_glEnableVertexAttribArray pfn_glEnableVertexAttribArray = NULL;
static PFN_glFramebufferRenderbuffer pfn_glFramebufferRenderbuffer = NULL;
static PFN_glFramebufferTexture2D pfn_glFramebufferTexture2D = NULL;
static PFN_glGenBuffers pfn_glGenBuffers = NULL;
static PFN_glGenerateMipmap pfn_glGenerateMipmap = NULL;
static PFN_glGenFramebuffers pfn_glGenFramebuffers = NULL;
static PFN_glGenRenderbuffers pfn_glGenRenderbuffers = NULL;
static PFN_glGetActiveAttrib pfn_glGetActiveAttrib = NULL;
static PFN_glGetActiveUniform pfn_glGetActiveUniform = NULL;
static PFN_glGetAttachedShaders pfn_glGetAttachedShaders = NULL;
static PFN_glGetAttribLocation pfn_glGetAttribLocation = NULL;
static PFN_glGetBufferParameteriv pfn_glGetBufferParameteriv = NULL;
static PFN_glGetFramebufferAttachmentParameteriv pfn_glGetFramebufferAttachmentParameteriv = NULL;
static PFN_glGetProgramInfoLog pfn_glGetProgramInfoLog = NULL;
static PFN_glGetProgramiv pfn_glGetProgramiv = NULL;
static PFN_glGetRenderbufferParameteriv pfn_glGetRenderbufferParameteriv = NULL;
static PFN_glGetShaderInfoLog pfn_glGetShaderInfoLog = NULL;
static PFN_glGetShaderPrecisionFormat pfn_glGetShaderPrecisionFormat = NULL;
static PFN_glGetShaderSource pfn_glGetShaderSource = NULL;
static PFN_glGetShaderiv pfn_glGetShaderiv = NULL;
static PFN_glGetUniformLocation pfn_glGetUniformLocation = NULL;
static PFN_glGetUniformfv pfn_glGetUniformfv = NULL;
static PFN_glGetUniformiv pfn_glGetUniformiv = NULL;
static PFN_glGetVertexAttribPointerv pfn_glGetVertexAttribPointerv = NULL;
static PFN_glGetVertexAttribfv pfn_glGetVertexAttribfv = NULL;
static PFN_glGetVertexAttribiv pfn_glGetVertexAttribiv = NULL;
static PFN_glIsFramebuffer pfn_glIsFramebuffer = NULL;
static PFN_glIsProgram pfn_glIsProgram = NULL;
static PFN_glIsRenderbuffer pfn_glIsRenderbuffer = NULL;
static PFN_glIsShader pfn_glIsShader = NULL;
static PFN_glLinkProgram pfn_glLinkProgram = NULL;
static PFN_glMultiTexCoord4f pfn_glMultiTexCoord4f = NULL;
static PFN_glPointParameterf pfn_glPointParameterf = NULL;
static PFN_glPointParameterfv pfn_glPointParameterfv = NULL;
static PFN_glReleaseShaderCompiler pfn_glReleaseShaderCompiler = NULL;
static PFN_glRenderbufferStorage pfn_glRenderbufferStorage = NULL;
static PFN_glSampleCoverage pfn_glSampleCoverage = NULL;
static PFN_glShaderSource pfn_glShaderSource = NULL;
static PFN_glStencilFuncSeparate pfn_glStencilFuncSeparate = NULL;
static PFN_glStencilMaskSeparate pfn_glStencilMaskSeparate = NULL;
static PFN_glStencilOpSeparate pfn_glStencilOpSeparate = NULL;
static PFN_glUniform1f pfn_glUniform1f = NULL;
static PFN_glUniform1fv pfn_glUniform1fv = NULL;
static PFN_glUniform1i pfn_glUniform1i = NULL;
static PFN_glUniform1iv pfn_glUniform1iv = NULL;
static PFN_glUniform2f pfn_glUniform2f = NULL;
static PFN_glUniform2fv pfn_glUniform2fv = NULL;
static PFN_glUniform2i pfn_glUniform2i = NULL;
static PFN_glUniform2iv pfn_glUniform2iv = NULL;
static PFN_glUniform3f pfn_glUniform3f = NULL;
static PFN_glUniform3fv pfn_glUniform3fv = NULL;
static PFN_glUniform3i pfn_glUniform3i = NULL;
static PFN_glUniform3iv pfn_glUniform3iv = NULL;
static PFN_glUniform4f pfn_glUniform4f = NULL;
static PFN_glUniform4fv pfn_glUniform4fv = NULL;
static PFN_glUniform4i pfn_glUniform4i = NULL;
static PFN_glUniform4iv pfn_glUniform4iv = NULL;
static PFN_glUniformMatrix2fv pfn_glUniformMatrix2fv = NULL;
static PFN_glUniformMatrix3fv pfn_glUniformMatrix3fv = NULL;
static PFN_glUniformMatrix4fv pfn_glUniformMatrix4fv = NULL;
static PFN_glUseProgram pfn_glUseProgram = NULL;
static PFN_glValidateProgram pfn_glValidateProgram = NULL;
static PFN_glVertexAttrib1f pfn_glVertexAttrib1f = NULL;
static PFN_glVertexAttrib1fv pfn_glVertexAttrib1fv = NULL;
static PFN_glVertexAttrib2f pfn_glVertexAttrib2f = NULL;
static PFN_glVertexAttrib2fv pfn_glVertexAttrib2fv = NULL;
static PFN_glVertexAttrib3f pfn_glVertexAttrib3f = NULL;
static PFN_glVertexAttrib3fv pfn_glVertexAttrib3fv = NULL;
static PFN_glVertexAttrib4f pfn_glVertexAttrib4f = NULL;
static PFN_glVertexAttrib4fv pfn_glVertexAttrib4fv = NULL;
static PFN_glVertexAttribPointer pfn_glVertexAttribPointer = NULL;

/* ------------------------------------------------------------- */
/* TEXTURE QUALITY ENHANCEMENTS & MODDING                        */
/* ------------------------------------------------------------- */
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

#define MAX_TRACKED_TEXTURES 65536

static int g_requested_aniso = 16;
static int g_requested_auto_mips = 0;
static int g_tex_mipmaps = 0;
static float g_tex_aniso = 0.0f;
static unsigned char g_tex_has_mips[MAX_TRACKED_TEXTURES / 8];
static char g_mod_dir[512] = "";
static GLuint g_default_fbo = 0;

void gl_wrap_set_anisotropy(int level) {
    g_requested_aniso = level;
}

void gl_wrap_set_mod_dir(const char *mod_dir) {
    if (mod_dir && mod_dir[0]) {
        strncpy(g_mod_dir, mod_dir, sizeof(g_mod_dir) - 1);
        g_mod_dir[sizeof(g_mod_dir) - 1] = '\0';
        printf("[GL] HD Texture Mod Directory enabled: %s\n", g_mod_dir);
    } else {
        g_mod_dir[0] = '\0';
    }
}

void gl_wrap_set_default_framebuffer(unsigned int fbo) {
    g_default_fbo = (GLuint)fbo;
}

static GLuint bound_texture_2d(void) {
    GLint tex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    return (GLuint)tex;
}

static int tex_has_mips(GLuint tex) {
    if (tex == 0 || tex >= MAX_TRACKED_TEXTURES) return 0;
    return (g_tex_has_mips[tex >> 3] >> (tex & 7)) & 1;
}

static void tex_set_mips(GLuint tex, int on) {
    if (tex == 0 || tex >= MAX_TRACKED_TEXTURES) return;
    if (on) g_tex_has_mips[tex >> 3] |= (unsigned char)(1 << (tex & 7));
    else    g_tex_has_mips[tex >> 3] &= (unsigned char)~(1 << (tex & 7));
}

static uint32_t fnv1a_tex(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

/* Load an uncompressed 24-bit or 32-bit TGA replacement texture from mod_dir */
static uint8_t *try_load_mod_tga(uint32_t hash, int *out_w, int *out_h) {
    if (!g_mod_dir[0]) return NULL;
    char path[768];
    snprintf(path, sizeof(path), "%s/tex_%08x.tga", g_mod_dir, hash);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t hdr[18];
    if (fread(hdr, 1, 18, f) != 18 || hdr[2] != 2) {
        fclose(f);
        return NULL;
    }
    int w = hdr[12] | (hdr[13] << 8);
    int h = hdr[14] | (hdr[15] << 8);
    int bpp = hdr[16];
    int top_origin = (hdr[17] & 0x20) != 0;
    if (w <= 0 || h <= 0 || (bpp != 24 && bpp != 32)) {
        fclose(f);
        return NULL;
    }
    if (hdr[0] > 0) fseek(f, hdr[0], SEEK_CUR);

    int bytes_per_px = bpp / 8;
    size_t raw_size = (size_t)w * (size_t)h * (size_t)bytes_per_px;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
    if (!raw || !rgba || fread(raw, 1, raw_size, f) != raw_size) {
        free(raw);
        free(rgba);
        fclose(f);
        return NULL;
    }
    fclose(f);

    for (int y = 0; y < h; y++) {
        int src_y = top_origin ? y : (h - 1 - y);
        const uint8_t *src = raw + (size_t)src_y * (size_t)w * (size_t)bytes_per_px;
        uint8_t *dst = rgba + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            dst[x * 4 + 0] = src[x * bytes_per_px + 2];
            dst[x * 4 + 1] = src[x * bytes_per_px + 1];
            dst[x * 4 + 2] = src[x * bytes_per_px + 0];
            dst[x * 4 + 3] = (bytes_per_px == 4) ? src[x * 4 + 3] : 255;
        }
    }
    free(raw);
    *out_w = w;
    *out_h = h;
    printf("[GL] Loaded HD replacement texture %s (%dx%d)\n", path, w, h);
    return rgba;
}

/* Software ETC1 RGB8 decoder for mobile compressed textures on desktop OpenGL */
static uint8_t clamp_u8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void decode_etc1_rgb8(const uint8_t *src, int width, int height, uint8_t *dst_rgb) {
    static const int mod_table[8][4] = {
        {  2,   8,  -2,   -8 },
        {  5,  17,  -5,  -17 },
        {  9,  29,  -9,  -29 },
        { 13,  42, -13,  -42 },
        { 18,  60, -18,  -60 },
        { 24,  80, -24,  -80 },
        { 33, 106, -33, -106 },
        { 47, 183, -47, -183 }
    };
    int bw = (width + 3) / 4;
    int bh = (height + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++, src += 8) {
            uint32_t high = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
                            ((uint32_t)src[2] << 8)  | (uint32_t)src[3];
            uint32_t low  = ((uint32_t)src[4] << 24) | ((uint32_t)src[5] << 16) |
                            ((uint32_t)src[6] << 8)  | (uint32_t)src[7];
            int diff = (high >> 1) & 1;
            int flip = high & 1;
            int r1, g1, b1, r2, g2, b2;
            if (diff) {
                int br1 = (high >> 27) & 0x1f;
                int bg1 = (high >> 19) & 0x1f;
                int bb1 = (high >> 11) & 0x1f;
                int dr  = (int)((high >> 24) & 0x7); if (dr >= 4) dr -= 8;
                int dg  = (int)((high >> 16) & 0x7); if (dg >= 4) dg -= 8;
                int db  = (int)((high >>  8) & 0x7); if (db >= 4) db -= 8;
                int br2 = br1 + dr, bg2 = bg1 + dg, bb2 = bb1 + db;
                r1 = (br1 << 3) | (br1 >> 2);
                g1 = (bg1 << 3) | (bg1 >> 2);
                b1 = (bb1 << 3) | (bb1 >> 2);
                r2 = (br2 << 3) | (br2 >> 2);
                g2 = (bg2 << 3) | (bg2 >> 2);
                b2 = (bb2 << 3) | (bb2 >> 2);
            } else {
                r1 = ((high >> 28) & 0xf) * 17;
                r2 = ((high >> 24) & 0xf) * 17;
                g1 = ((high >> 20) & 0xf) * 17;
                g2 = ((high >> 16) & 0xf) * 17;
                b1 = ((high >> 12) & 0xf) * 17;
                b2 = ((high >>  8) & 0xf) * 17;
            }
            int cw1 = (high >> 5) & 0x7;
            int cw2 = (high >> 2) & 0x7;
            for (int py = 0; py < 4; py++) {
                for (int px = 0; px < 4; px++) {
                    int x = bx * 4 + px;
                    int y = by * 4 + py;
                    if (x >= width || y >= height) continue;
                    int bit_idx = px * 4 + py;
                    int idx = (((low >> (bit_idx + 16)) & 1) << 1) | ((low >> bit_idx) & 1);
                    int sub = flip ? (py >= 2) : (px >= 2);
                    int delta = mod_table[sub ? cw2 : cw1][idx];
                    uint8_t *p = dst_rgb + ((size_t)y * (size_t)width + (size_t)x) * 3;
                    p[0] = clamp_u8((sub ? r2 : r1) + delta);
                    p[1] = clamp_u8((sub ? g2 : g1) + delta);
                    p[2] = clamp_u8((sub ? b2 : b1) + delta);
                }
            }
        }
    }
}

/* Apply minification filter upgrades and anisotropic filtering */
static void apply_min_filter(GLenum target, GLenum pname, GLint param) {
    if (pname == GL_TEXTURE_MIN_FILTER) {
        if (param == GL_LINEAR_MIPMAP_NEAREST || param == GL_NEAREST_MIPMAP_LINEAR || param == GL_LINEAR_MIPMAP_LINEAR) {
            glTexParameteri(target, pname, GL_LINEAR_MIPMAP_LINEAR);
            if (g_tex_aniso > 1.0f) glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_tex_aniso);
            return;
        }
        if (g_tex_mipmaps && target == GL_TEXTURE_2D && param == GL_LINEAR && tex_has_mips(bound_texture_2d())) {
            glTexParameteri(target, pname, GL_LINEAR_MIPMAP_LINEAR);
            if (g_tex_aniso > 1.0f) glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_tex_aniso);
            return;
        }
    }
    glTexParameteri(target, pname, param);
}

/* ------------------------------------------------------------- */
/* __cdecl WRAPPERS FOR OPENGL ES 1.1 & 2.0                      */
/* ------------------------------------------------------------- */

void CDECL wrap_glActiveTexture(GLenum texture) { if (pfn_glActiveTexture) pfn_glActiveTexture(texture); }
void CDECL wrap_glClientActiveTexture(GLenum texture) { if (pfn_glClientActiveTexture) pfn_glClientActiveTexture(texture); }
void CDECL wrap_glAttachShader(GLuint program, GLuint shader) { if (pfn_glAttachShader) pfn_glAttachShader(program, shader); }
void CDECL wrap_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) { if (pfn_glBindAttribLocation) pfn_glBindAttribLocation(program, index, name); }
void CDECL wrap_glBindBuffer(GLenum target, GLuint buffer) { if (pfn_glBindBuffer) pfn_glBindBuffer(target, buffer); }
void CDECL wrap_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (framebuffer == 0) framebuffer = g_default_fbo;
    if (pfn_glBindFramebuffer) pfn_glBindFramebuffer(target, framebuffer);
}
void CDECL wrap_glBindRenderbuffer(GLenum target, GLuint renderbuffer) { if (pfn_glBindRenderbuffer) pfn_glBindRenderbuffer(target, renderbuffer); }
void CDECL wrap_glBindTexture(GLenum target, GLuint texture) { glBindTexture(target, texture); }
void CDECL wrap_glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { if (pfn_glBlendColor) pfn_glBlendColor(r, g, b, a); }
void CDECL wrap_glBlendEquation(GLenum mode) { if (pfn_glBlendEquation) pfn_glBlendEquation(mode); }
void CDECL wrap_glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) { if (pfn_glBlendEquationSeparate) pfn_glBlendEquationSeparate(modeRGB, modeAlpha); }
void CDECL wrap_glBlendFunc(GLenum sfactor, GLenum dfactor) { glBlendFunc(sfactor, dfactor); }
void CDECL wrap_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { if (pfn_glBlendFuncSeparate) pfn_glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha); }
void CDECL wrap_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) { if (pfn_glBufferData) pfn_glBufferData(target, size, data, usage); }
void CDECL wrap_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) { if (pfn_glBufferSubData) pfn_glBufferSubData(target, offset, size, data); }
GLenum CDECL wrap_glCheckFramebufferStatus(GLenum target) {
    return pfn_glCheckFramebufferStatus ? pfn_glCheckFramebufferStatus(target) : 0x8CD5; /* GL_FRAMEBUFFER_COMPLETE */
}
void CDECL wrap_glClear(GLbitfield mask) { glClear(mask); }
void CDECL wrap_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { glClearColor(r, g, b, a); }
void CDECL wrap_glClearDepthf(GLclampf depth) { glClearDepth((double)depth); }
void CDECL wrap_glClearStencil(GLint s) { glClearStencil(s); }
void CDECL wrap_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { glColorMask(r, g, b, a); }
void CDECL wrap_glCompileShader(GLuint shader) {
    if (pfn_glCompileShader) {
        pfn_glCompileShader(shader);
        GLint status = 0;
        if (pfn_glGetShaderiv) {
            pfn_glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (!status && pfn_glGetShaderInfoLog) {
                char log[1024];
                pfn_glGetShaderInfoLog(shader, sizeof(log), NULL, log);
                printf("[GL] Shader compile error: %s\n", log);
            }
        }
    }
}
void CDECL wrap_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
    if (internalformat == GL_ETC1_RGB8_OES && data && width > 0 && height > 0) {
        uint8_t *rgb = (uint8_t *)malloc((size_t)width * (size_t)height * 3);
        if (rgb) {
            decode_etc1_rgb8((const uint8_t *)data, width, height, rgb);
            glTexImage2D(target, level, GL_RGB, width, height, border, GL_RGB, GL_UNSIGNED_BYTE, rgb);
            free(rgb);
            return;
        }
    }
    if (pfn_glCompressedTexImage2D) pfn_glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}
void CDECL wrap_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) {
    if (pfn_glCompressedTexSubImage2D) pfn_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}
void CDECL wrap_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
}
void CDECL wrap_glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}
GLuint CDECL wrap_glCreateProgram(void) { return pfn_glCreateProgram ? pfn_glCreateProgram() : 0; }
GLuint CDECL wrap_glCreateShader(GLenum type) { return pfn_glCreateShader ? pfn_glCreateShader(type) : 0; }
void CDECL wrap_glCullFace(GLenum mode) { glCullFace(mode); }
void CDECL wrap_glDeleteBuffers(GLsizei n, const GLuint *buffers) { if (pfn_glDeleteBuffers) pfn_glDeleteBuffers(n, buffers); }
void CDECL wrap_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) { if (pfn_glDeleteFramebuffers) pfn_glDeleteFramebuffers(n, framebuffers); }
void CDECL wrap_glDeleteProgram(GLuint program) { if (pfn_glDeleteProgram) pfn_glDeleteProgram(program); }
void CDECL wrap_glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) { if (pfn_glDeleteRenderbuffers) pfn_glDeleteRenderbuffers(n, renderbuffers); }
void CDECL wrap_glDeleteShader(GLuint shader) { if (pfn_glDeleteShader) pfn_glDeleteShader(shader); }
void CDECL wrap_glDeleteTextures(GLsizei n, const GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) tex_set_mips(textures[i], 0);
    glDeleteTextures(n, textures);
}
void CDECL wrap_glDepthFunc(GLenum func) { glDepthFunc(func); }
void CDECL wrap_glDepthMask(GLboolean flag) { glDepthMask(flag); }
void CDECL wrap_glDepthRangef(GLclampf n, GLclampf f) { glDepthRange(n, f); }
void CDECL wrap_glDetachShader(GLuint program, GLuint shader) { if (pfn_glDetachShader) pfn_glDetachShader(program, shader); }
void CDECL wrap_glDisable(GLenum cap) { glDisable(cap); }
void CDECL wrap_glDisableVertexAttribArray(GLuint index) { if (pfn_glDisableVertexAttribArray) pfn_glDisableVertexAttribArray(index); }
void CDECL wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (g_mesh_dump_enabled) mesh_dump_draw_arrays(mode, first, count);
    glDrawArrays(mode, first, count);
}
void CDECL wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (g_mesh_dump_enabled) mesh_dump_draw_elements(mode, count, type, indices);
    glDrawElements(mode, count, type, indices);
}
void CDECL wrap_glEnable(GLenum cap) { glEnable(cap); }
void CDECL wrap_glEnableVertexAttribArray(GLuint index) { if (pfn_glEnableVertexAttribArray) pfn_glEnableVertexAttribArray(index); }
void CDECL wrap_glFinish(void) { glFinish(); }
void CDECL wrap_glFlush(void) { glFlush(); }
void CDECL wrap_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    if (pfn_glFramebufferRenderbuffer) pfn_glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}
void CDECL wrap_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    if (pfn_glFramebufferTexture2D) pfn_glFramebufferTexture2D(target, attachment, textarget, texture, level);
}
void CDECL wrap_glFrontFace(GLenum mode) { glFrontFace(mode); }
void CDECL wrap_glGenBuffers(GLsizei n, GLuint *buffers) { if (pfn_glGenBuffers) pfn_glGenBuffers(n, buffers); }
void CDECL wrap_glGenerateMipmap(GLenum target) { if (pfn_glGenerateMipmap) pfn_glGenerateMipmap(target); }
void CDECL wrap_glGenFramebuffers(GLsizei n, GLuint *framebuffers) { if (pfn_glGenFramebuffers) pfn_glGenFramebuffers(n, framebuffers); }
void CDECL wrap_glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) { if (pfn_glGenRenderbuffers) pfn_glGenRenderbuffers(n, renderbuffers); }
void CDECL wrap_glGenTextures(GLsizei n, GLuint *textures) { glGenTextures(n, textures); }
void CDECL wrap_glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    if (pfn_glGetActiveAttrib) pfn_glGetActiveAttrib(program, index, bufsize, length, size, type, name);
}
void CDECL wrap_glGetActiveUniform(GLuint program, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    if (pfn_glGetActiveUniform) pfn_glGetActiveUniform(program, index, bufsize, length, size, type, name);
}
void CDECL wrap_glGetAttachedShaders(GLuint program, GLsizei maxcount, GLsizei *count, GLuint *shaders) {
    if (pfn_glGetAttachedShaders) pfn_glGetAttachedShaders(program, maxcount, count, shaders);
}
GLint CDECL wrap_glGetAttribLocation(GLuint program, const GLchar *name) {
    return pfn_glGetAttribLocation ? pfn_glGetAttribLocation(program, name) : -1;
}
void CDECL wrap_glGetBooleanv(GLenum pname, GLboolean *params) { glGetBooleanv(pname, params); }
void CDECL wrap_glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (pfn_glGetBufferParameteriv) pfn_glGetBufferParameteriv(target, pname, params);
}
GLenum CDECL wrap_glGetError(void) { return glGetError(); }
void CDECL wrap_glGetFloatv(GLenum pname, GLfloat *params) { glGetFloatv(pname, params); }
void CDECL wrap_glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    if (pfn_glGetFramebufferAttachmentParameteriv) pfn_glGetFramebufferAttachmentParameteriv(target, attachment, pname, params);
}
void CDECL wrap_glGetIntegerv(GLenum pname, GLint *params) { glGetIntegerv(pname, params); }
void CDECL wrap_glGetProgramInfoLog(GLuint program, GLsizei bufsize, GLsizei *length, GLchar *infolog) {
    if (pfn_glGetProgramInfoLog) pfn_glGetProgramInfoLog(program, bufsize, length, infolog);
}
void CDECL wrap_glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    if (pfn_glGetProgramiv) pfn_glGetProgramiv(program, pname, params);
}
void CDECL wrap_glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (pfn_glGetRenderbufferParameteriv) pfn_glGetRenderbufferParameteriv(target, pname, params);
}
void CDECL wrap_glGetShaderInfoLog(GLuint shader, GLsizei bufsize, GLsizei *length, GLchar *infolog) {
    if (pfn_glGetShaderInfoLog) pfn_glGetShaderInfoLog(shader, bufsize, length, infolog);
}
void CDECL wrap_glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision) {
    if (pfn_glGetShaderPrecisionFormat) {
        pfn_glGetShaderPrecisionFormat(shadertype, precisiontype, range, precision);
    } else {
        if (range) { range[0] = 127; range[1] = 127; }
        if (precision) *precision = 23;
    }
}
void CDECL wrap_glGetShaderSource(GLuint shader, GLsizei bufsize, GLsizei *length, GLchar *source) {
    if (pfn_glGetShaderSource) pfn_glGetShaderSource(shader, bufsize, length, source);
}
void CDECL wrap_glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    if (pfn_glGetShaderiv) pfn_glGetShaderiv(shader, pname, params);
}
const GLubyte * CDECL wrap_glGetString(GLenum name) {
    if (name == GL_EXTENSIONS) {
        static char ext_buf[8192] = {0};
        if (!ext_buf[0]) {
            const char *real = (const char *)glGetString(GL_EXTENSIONS);
            snprintf(ext_buf, sizeof(ext_buf),
                "%s GL_OES_point_sprite GL_OES_point_size_array "
                "GL_OES_texture_cube_map GL_OES_read_format "
                "GL_OES_framebuffer_object GL_OES_matrix_palette "
                "GL_OES_byte_coordinates GL_OES_fixed_point "
                "GL_OES_compressed_paletted_texture GL_OES_compressed_ETC1_RGB8_texture",
                real ? real : "");
        }
        return (const GLubyte *)ext_buf;
    }
    return glGetString(name);
}
void CDECL wrap_glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) { glGetTexParameterfv(target, pname, params); }
void CDECL wrap_glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) { glGetTexParameteriv(target, pname, params); }
GLint CDECL wrap_glGetUniformLocation(GLuint program, const GLchar *name) {
    return pfn_glGetUniformLocation ? pfn_glGetUniformLocation(program, name) : -1;
}
void CDECL wrap_glGetUniformfv(GLuint program, GLint location, GLfloat *params) { if (pfn_glGetUniformfv) pfn_glGetUniformfv(program, location, params); }
void CDECL wrap_glGetUniformiv(GLuint program, GLint location, GLint *params) { if (pfn_glGetUniformiv) pfn_glGetUniformiv(program, location, params); }
void CDECL wrap_glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) { if (pfn_glGetVertexAttribPointerv) pfn_glGetVertexAttribPointerv(index, pname, pointer); }
void CDECL wrap_glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) { if (pfn_glGetVertexAttribfv) pfn_glGetVertexAttribfv(index, pname, params); }
void CDECL wrap_glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) { if (pfn_glGetVertexAttribiv) pfn_glGetVertexAttribiv(index, pname, params); }
void CDECL wrap_glHint(GLenum target, GLenum mode) { glHint(target, mode); }
GLboolean CDECL wrap_glIsEnabled(GLenum cap) { return glIsEnabled(cap); }
GLboolean CDECL wrap_glIsFramebuffer(GLuint framebuffer) { return pfn_glIsFramebuffer ? pfn_glIsFramebuffer(framebuffer) : GL_FALSE; }
GLboolean CDECL wrap_glIsProgram(GLuint program) { return pfn_glIsProgram ? pfn_glIsProgram(program) : GL_FALSE; }
GLboolean CDECL wrap_glIsRenderbuffer(GLuint renderbuffer) { return pfn_glIsRenderbuffer ? pfn_glIsRenderbuffer(renderbuffer) : GL_FALSE; }
GLboolean CDECL wrap_glIsShader(GLuint shader) { return pfn_glIsShader ? pfn_glIsShader(shader) : GL_FALSE; }
GLboolean CDECL wrap_glIsTexture(GLuint texture) { return glIsTexture(texture); }
void CDECL wrap_glLineWidth(GLfloat width) { glLineWidth(width); }
void CDECL wrap_glLinkProgram(GLuint program) {
    if (pfn_glLinkProgram) {
        pfn_glLinkProgram(program);
        GLint status = 0;
        if (pfn_glGetProgramiv) {
            pfn_glGetProgramiv(program, GL_LINK_STATUS, &status);
            if (!status && pfn_glGetProgramInfoLog) {
                char log[1024];
                pfn_glGetProgramInfoLog(program, sizeof(log), NULL, log);
                printf("[GL] Program link error: %s\n", log);
            }
        }
    }
}
void CDECL wrap_glPixelStorei(GLenum pname, GLint param) { glPixelStorei(pname, param); }
void CDECL wrap_glPolygonOffset(GLfloat factor, GLfloat units) { glPolygonOffset(factor, units); }
void CDECL wrap_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, GLvoid *pixels) { glReadPixels(x, y, w, h, f, t, pixels); }
void CDECL wrap_glReleaseShaderCompiler(void) { if (pfn_glReleaseShaderCompiler) pfn_glReleaseShaderCompiler(); }
void CDECL wrap_glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    if (pfn_glRenderbufferStorage) pfn_glRenderbufferStorage(target, internalformat, width, height);
}
void CDECL wrap_glSampleCoverage(GLclampf value, GLboolean invert) { if (pfn_glSampleCoverage) pfn_glSampleCoverage(value, invert); }
void CDECL wrap_glScissor(GLint x, GLint y, GLsizei width, GLsizei height) { glScissor(x, y, width, height); }
void CDECL wrap_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) {
    if (!string || count <= 0) {
        if (pfn_glShaderSource) pfn_glShaderSource(shader, count, string, length);
        return;
    }
    static const char *preamble = "#version 120\n#define lowp\n#define mediump\n#define highp\n#line 1 0\n";
    const GLchar **new_strings = (const GLchar **)malloc((count + 1) * sizeof(GLchar *));
    GLint *new_lengths = (GLint *)malloc((count + 1) * sizeof(GLint));
    new_strings[0] = preamble;
    new_lengths[0] = (GLint)strlen(preamble);
    for (int i = 0; i < count; i++) {
        new_strings[i + 1] = string[i];
        new_lengths[i + 1] = (length && length[i] >= 0) ? length[i] : (GLint)strlen(string[i]);
    }
    if (pfn_glShaderSource) pfn_glShaderSource(shader, count + 1, new_strings, new_lengths);
    free(new_strings);
    free(new_lengths);
}
void CDECL wrap_glStencilFunc(GLenum func, GLint ref, GLuint mask) { glStencilFunc(func, ref, mask); }
void CDECL wrap_glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) { if (pfn_glStencilFuncSeparate) pfn_glStencilFuncSeparate(face, func, ref, mask); }
void CDECL wrap_glStencilMask(GLuint mask) { glStencilMask(mask); }
void CDECL wrap_glStencilMaskSeparate(GLenum face, GLuint mask) { if (pfn_glStencilMaskSeparate) pfn_glStencilMaskSeparate(face, mask); }
void CDECL wrap_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) { glStencilOp(fail, zfail, zpass); }
void CDECL wrap_glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass) { if (pfn_glStencilOpSeparate) pfn_glStencilOpSeparate(face, fail, zfail, zpass); }

void CDECL wrap_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    /* Live HD texture replacement check on level-0 RGBA8 uploads */
    if (g_mod_dir[0] && target == GL_TEXTURE_2D && level == 0 && pixels != NULL &&
        format == GL_RGBA && type == GL_UNSIGNED_BYTE && width > 0 && height > 0) {
        uint32_t hash = fnv1a_tex(pixels, (size_t)width * (size_t)height * 4);
        int mod_w = 0, mod_h = 0;
        uint8_t *mod_rgba = try_load_mod_tga(hash, &mod_w, &mod_h);
        if (mod_rgba) {
            GLuint tex = bound_texture_2d();
            glTexParameteri(target, GL_GENERATE_MIPMAP, GL_TRUE);
            glTexImage2D(target, 0, GL_RGBA, mod_w, mod_h, border, GL_RGBA, GL_UNSIGNED_BYTE, mod_rgba);
            free(mod_rgba);
            tex_set_mips(tex, 1);
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            if (g_tex_aniso > 1.0f) glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_tex_aniso);
            return;
        }
    }

    if (!g_tex_mipmaps || target != GL_TEXTURE_2D || level != 0) {
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
        return;
    }

    GLuint tex = bound_texture_2d();
    int enhance = (pixels != NULL);
    int had_mips = tex_has_mips(tex);

    if (enhance) {
        glTexParameteri(target, GL_GENERATE_MIPMAP, GL_TRUE);
    } else if (had_mips) {
        glTexParameteri(target, GL_GENERATE_MIPMAP, GL_FALSE);
    }

    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    tex_set_mips(tex, enhance);

    GLint min_filter = 0;
    glGetTexParameteriv(target, GL_TEXTURE_MIN_FILTER, &min_filter);
    if (enhance) {
        if (min_filter == GL_LINEAR) glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        if (g_tex_aniso > 1.0f) glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_tex_aniso);
    } else if (had_mips) {
        if (min_filter == GL_LINEAR_MIPMAP_LINEAR) glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        if (g_tex_aniso > 1.0f) glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
    }
}

void CDECL wrap_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (pname == GL_TEXTURE_MIN_FILTER) {
        apply_min_filter(target, pname, (GLint)param);
        return;
    }
    glTexParameterf(target, pname, param);
}
void CDECL wrap_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (pname == GL_TEXTURE_MIN_FILTER && params) {
        apply_min_filter(target, pname, (GLint)params[0]);
        return;
    }
    glTexParameterfv(target, pname, params);
}
void CDECL wrap_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    apply_min_filter(target, pname, param);
}
void CDECL wrap_glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    if (pname == GL_TEXTURE_MIN_FILTER && params) {
        apply_min_filter(target, pname, params[0]);
        return;
    }
    glTexParameteriv(target, pname, params);
}
void CDECL wrap_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) {
    glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void CDECL wrap_glUniform1f(GLint location, GLfloat v0) { if (pfn_glUniform1f) pfn_glUniform1f(location, v0); }
void CDECL wrap_glUniform1fv(GLint location, GLsizei count, const GLfloat *value) { if (pfn_glUniform1fv) pfn_glUniform1fv(location, count, value); }
void CDECL wrap_glUniform1i(GLint location, GLint v0) { if (pfn_glUniform1i) pfn_glUniform1i(location, v0); }
void CDECL wrap_glUniform1iv(GLint location, GLsizei count, const GLint *value) { if (pfn_glUniform1iv) pfn_glUniform1iv(location, count, value); }
void CDECL wrap_glUniform2f(GLint location, GLfloat v0, GLfloat v1) { if (pfn_glUniform2f) pfn_glUniform2f(location, v0, v1); }
void CDECL wrap_glUniform2fv(GLint location, GLsizei count, const GLfloat *value) { if (pfn_glUniform2fv) pfn_glUniform2fv(location, count, value); }
void CDECL wrap_glUniform2i(GLint location, GLint v0, GLint v1) { if (pfn_glUniform2i) pfn_glUniform2i(location, v0, v1); }
void CDECL wrap_glUniform2iv(GLint location, GLsizei count, const GLint *value) { if (pfn_glUniform2iv) pfn_glUniform2iv(location, count, value); }
void CDECL wrap_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { if (pfn_glUniform3f) pfn_glUniform3f(location, v0, v1, v2); }
void CDECL wrap_glUniform3fv(GLint location, GLsizei count, const GLfloat *value) { if (pfn_glUniform3fv) pfn_glUniform3fv(location, count, value); }
void CDECL wrap_glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) { if (pfn_glUniform3i) pfn_glUniform3i(location, v0, v1, v2); }
void CDECL wrap_glUniform3iv(GLint location, GLsizei count, const GLint *value) { if (pfn_glUniform3iv) pfn_glUniform3iv(location, count, value); }
void CDECL wrap_glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { if (pfn_glUniform4f) pfn_glUniform4f(location, v0, v1, v2, v3); }
void CDECL wrap_glUniform4fv(GLint location, GLsizei count, const GLfloat *value) { if (pfn_glUniform4fv) pfn_glUniform4fv(location, count, value); }
void CDECL wrap_glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { if (pfn_glUniform4i) pfn_glUniform4i(location, v0, v1, v2, v3); }
void CDECL wrap_glUniform4iv(GLint location, GLsizei count, const GLint *value) { if (pfn_glUniform4iv) pfn_glUniform4iv(location, count, value); }
void CDECL wrap_glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { if (pfn_glUniformMatrix2fv) pfn_glUniformMatrix2fv(location, count, transpose, value); }
void CDECL wrap_glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { if (pfn_glUniformMatrix3fv) pfn_glUniformMatrix3fv(location, count, transpose, value); }
void CDECL wrap_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { if (pfn_glUniformMatrix4fv) pfn_glUniformMatrix4fv(location, count, transpose, value); }
void CDECL wrap_glUseProgram(GLuint program) { if (pfn_glUseProgram) pfn_glUseProgram(program); }
void CDECL wrap_glValidateProgram(GLuint program) { if (pfn_glValidateProgram) pfn_glValidateProgram(program); }
void CDECL wrap_glVertexAttrib1f(GLuint index, GLfloat x) { if (pfn_glVertexAttrib1f) pfn_glVertexAttrib1f(index, x); }
void CDECL wrap_glVertexAttrib1fv(GLuint index, const GLfloat *v) { if (pfn_glVertexAttrib1fv) pfn_glVertexAttrib1fv(index, v); }
void CDECL wrap_glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) { if (pfn_glVertexAttrib2f) pfn_glVertexAttrib2f(index, x, y); }
void CDECL wrap_glVertexAttrib2fv(GLuint index, const GLfloat *v) { if (pfn_glVertexAttrib2fv) pfn_glVertexAttrib2fv(index, v); }
void CDECL wrap_glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) { if (pfn_glVertexAttrib3f) pfn_glVertexAttrib3f(index, x, y, z); }
void CDECL wrap_glVertexAttrib3fv(GLuint index, const GLfloat *v) { if (pfn_glVertexAttrib3fv) pfn_glVertexAttrib3fv(index, v); }
void CDECL wrap_glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { if (pfn_glVertexAttrib4f) pfn_glVertexAttrib4f(index, x, y, z, w); }
void CDECL wrap_glVertexAttrib4fv(GLuint index, const GLfloat *v) { if (pfn_glVertexAttrib4fv) pfn_glVertexAttrib4fv(index, v); }
void CDECL wrap_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    if (pfn_glVertexAttribPointer) pfn_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}
void CDECL wrap_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { glViewport(x, y, width, height); }

/* ------------------------------------------------------------- */
/* OPENGL ES 1.1 FIXED-FUNCTION & FIXED-POINT WRAPPERS           */
/* ------------------------------------------------------------- */
void CDECL wrap_glAlphaFunc(GLenum func, GLclampf ref) { glAlphaFunc(func, ref); }
void CDECL wrap_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { glColor4f(r, g, b, a); }
void CDECL wrap_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4ub(r, g, b, a); }
void CDECL wrap_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { glColorPointer(size, type, stride, pointer); }
void CDECL wrap_glDisableClientState(GLenum array) { glDisableClientState(array); }
void CDECL wrap_glEnableClientState(GLenum array) { glEnableClientState(array); }
void CDECL wrap_glFogf(GLenum pname, GLfloat param) { glFogf(pname, param); }
void CDECL wrap_glFogfv(GLenum pname, const GLfloat *params) { glFogfv(pname, params); }
void CDECL wrap_glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { glFrustum(l, r, b, t, n, f); }
void CDECL wrap_glGetClipPlanef(GLenum plane, GLfloat *equation) { glGetClipPlane(plane, (GLdouble *)equation); }
void CDECL wrap_glClipPlanef(GLenum plane, const GLfloat *equation) {
    GLdouble eq[4] = { equation[0], equation[1], equation[2], equation[3] };
    glClipPlane(plane, eq);
}
void CDECL wrap_glGetLightfv(GLenum light, GLenum pname, GLfloat *params) { glGetLightfv(light, pname, params); }
void CDECL wrap_glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params) { glGetMaterialfv(face, pname, params); }
void CDECL wrap_glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params) { glGetTexEnvfv(target, pname, params); }
void CDECL wrap_glGetTexEnviv(GLenum target, GLenum pname, GLint *params) { glGetTexEnviv(target, pname, params); }
void CDECL wrap_glLightModelf(GLenum pname, GLfloat param) { glLightModelf(pname, param); }
void CDECL wrap_glLightModelfv(GLenum pname, const GLfloat *params) { glLightModelfv(pname, params); }
void CDECL wrap_glLightf(GLenum light, GLenum pname, GLfloat param) { glLightf(light, pname, param); }
void CDECL wrap_glLightfv(GLenum light, GLenum pname, const GLfloat *params) { glLightfv(light, pname, params); }
void CDECL wrap_glLoadIdentity(void) { glLoadIdentity(); }
void CDECL wrap_glLoadMatrixf(const GLfloat *m) { glLoadMatrixf(m); }
void CDECL wrap_glLogicOp(GLenum opcode) { glLogicOp(opcode); }
void CDECL wrap_glMaterialf(GLenum face, GLenum pname, GLfloat param) { glMaterialf(face, pname, param); }
void CDECL wrap_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) { glMaterialfv(face, pname, params); }
void CDECL wrap_glMatrixMode(GLenum mode) { glMatrixMode(mode); }
void CDECL wrap_glMultMatrixf(const GLfloat *m) { glMultMatrixf(m); }
void CDECL wrap_glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    if (pfn_glMultiTexCoord4f) pfn_glMultiTexCoord4f(target, s, t, r, q);
}
void CDECL wrap_glNormal3f(GLfloat x, GLfloat y, GLfloat z) { glNormal3f(x, y, z); }
void CDECL wrap_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) { glNormalPointer(type, stride, pointer); }
void CDECL wrap_glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { glOrtho(l, r, b, t, n, f); }
void CDECL wrap_glPointParameterf(GLenum pname, GLfloat param) { if (pfn_glPointParameterf) pfn_glPointParameterf(pname, param); }
void CDECL wrap_glPointParameterfv(GLenum pname, const GLfloat *params) { if (pfn_glPointParameterfv) pfn_glPointParameterfv(pname, params); }
void CDECL wrap_glPointSize(GLfloat size) { glPointSize(size); }
void CDECL wrap_glPopMatrix(void) { glPopMatrix(); }
void CDECL wrap_glPushMatrix(void) { glPushMatrix(); }
void CDECL wrap_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { glRotatef(angle, x, y, z); }
void CDECL wrap_glScalef(GLfloat x, GLfloat y, GLfloat z) { glScalef(x, y, z); }
void CDECL wrap_glShadeModel(GLenum mode) { glShadeModel(mode); }
void CDECL wrap_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { glTexCoordPointer(size, type, stride, pointer); }
void CDECL wrap_glTexEnvi(GLenum target, GLenum pname, GLint param) { glTexEnvi(target, pname, param); }
void CDECL wrap_glTexEnvf(GLenum target, GLenum pname, GLfloat param) { glTexEnvf(target, pname, param); }
void CDECL wrap_glTexEnviv(GLenum target, GLenum pname, const GLint *params) { glTexEnviv(target, pname, params); }
void CDECL wrap_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params) { glTexEnvfv(target, pname, params); }
void CDECL wrap_glTranslatef(GLfloat x, GLfloat y, GLfloat z) { glTranslatef(x, y, z); }
void CDECL wrap_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { glVertexPointer(size, type, stride, pointer); }

/* 16.16 Fixed-point conversions */
void CDECL wrap_glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) { glOrtho(FIX2FLT(l), FIX2FLT(r), FIX2FLT(b), FIX2FLT(t), FIX2FLT(n), FIX2FLT(f)); }
void CDECL wrap_glFrustumx(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) { glFrustum(FIX2FLT(l), FIX2FLT(r), FIX2FLT(b), FIX2FLT(t), FIX2FLT(n), FIX2FLT(f)); }
void CDECL wrap_glTranslatex(GLfixed x, GLfixed y, GLfixed z) { glTranslatef(FIX2FLT(x), FIX2FLT(y), FIX2FLT(z)); }
void CDECL wrap_glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z) { glRotatef(FIX2FLT(angle), FIX2FLT(x), FIX2FLT(y), FIX2FLT(z)); }
void CDECL wrap_glScalex(GLfixed x, GLfixed y, GLfixed z) { glScalef(FIX2FLT(x), FIX2FLT(y), FIX2FLT(z)); }
void CDECL wrap_glClearColorx(GLfixed r, GLfixed g, GLfixed b, GLfixed a) { glClearColor(FIX2FLT(r), FIX2FLT(g), FIX2FLT(b), FIX2FLT(a)); }
void CDECL wrap_glClearDepthx(GLclampx depth) { glClearDepth(FIX2FLT(depth)); }
void CDECL wrap_glDepthRangex(GLclampx n, GLclampx f) { glDepthRange(FIX2FLT(n), FIX2FLT(f)); }
void CDECL wrap_glColor4x(GLfixed r, GLfixed g, GLfixed b, GLfixed a) { glColor4f(FIX2FLT(r), FIX2FLT(g), FIX2FLT(b), FIX2FLT(a)); }
void CDECL wrap_glNormal3x(GLfixed x, GLfixed y, GLfixed z) { glNormal3f(FIX2FLT(x), FIX2FLT(y), FIX2FLT(z)); }
void CDECL wrap_glAlphaFuncx(GLenum func, GLclampx ref) { glAlphaFunc(func, FIX2FLT(ref)); }
void CDECL wrap_glLoadMatrixx(const GLfixed *m) {
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = FIX2FLT(m[i]);
    glLoadMatrixf(f);
}
void CDECL wrap_glMultMatrixx(const GLfixed *m) {
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = FIX2FLT(m[i]);
    glMultMatrixf(f);
}
void CDECL wrap_glLightx(GLenum light, GLenum pname, GLfixed param) { glLightf(light, pname, FIX2FLT(param)); }
void CDECL wrap_glLightxv(GLenum light, GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_SPOT_EXPONENT || pname == GL_SPOT_CUTOFF ||
                 pname == GL_CONSTANT_ATTENUATION || pname == GL_LINEAR_ATTENUATION ||
                 pname == GL_QUADRATIC_ATTENUATION) ? 1 :
                (pname == GL_SPOT_DIRECTION) ? 3 : 4;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glLightfv(light, pname, f);
}
void CDECL wrap_glMaterialx(GLenum face, GLenum pname, GLfixed param) { glMaterialf(face, pname, FIX2FLT(param)); }
void CDECL wrap_glMaterialxv(GLenum face, GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_SHININESS) ? 1 : 4;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glMaterialfv(face, pname, f);
}
void CDECL wrap_glLightModelx(GLenum pname, GLfixed param) { glLightModelf(pname, FIX2FLT(param)); }
void CDECL wrap_glLightModelxv(GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_LIGHT_MODEL_TWO_SIDE) ? 1 : 4;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glLightModelfv(pname, f);
}
void CDECL wrap_glTexEnvx(GLenum target, GLenum pname, GLfixed param) {
    glTexEnvf(target, pname, (pname == GL_TEXTURE_ENV_MODE) ? (float)param : FIX2FLT(param));
}
void CDECL wrap_glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_TEXTURE_ENV_COLOR) ? 4 : 1;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glTexEnvfv(target, pname, f);
}
void CDECL wrap_glFogx(GLenum pname, GLfixed param) { glFogf(pname, FIX2FLT(param)); }
void CDECL wrap_glFogxv(GLenum pname, const GLfixed *params) {
    float f[4];
    int count = (pname == GL_FOG_COLOR) ? 4 : 1;
    for (int i = 0; i < count; i++) f[i] = FIX2FLT(params[i]);
    glFogfv(pname, f);
}
void CDECL wrap_glPointParameterx(GLenum pname, GLfixed param) { wrap_glPointParameterf(pname, FIX2FLT(param)); }
void CDECL wrap_glPointParameterxv(GLenum pname, const GLfixed *params) {
    float f[3] = { FIX2FLT(params[0]), FIX2FLT(params[1]), FIX2FLT(params[2]) };
    wrap_glPointParameterfv(pname, f);
}
void CDECL wrap_glPointSizex(GLfixed size) { glPointSize(FIX2FLT(size)); }
void CDECL wrap_glPolygonOffsetx(GLfixed factor, GLfixed units) { glPolygonOffset(FIX2FLT(factor), FIX2FLT(units)); }
void CDECL wrap_glLineWidthx(GLfixed width) { glLineWidth(FIX2FLT(width)); }
void CDECL wrap_glSampleCoveragex(GLclampx value, GLboolean invert) { wrap_glSampleCoverage(FIX2FLT(value), invert); }
void CDECL wrap_glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q) {
    wrap_glMultiTexCoord4f(target, FIX2FLT(s), FIX2FLT(t), FIX2FLT(r), FIX2FLT(q));
}
void CDECL wrap_glTexParameterx(GLenum target, GLenum pname, GLfixed param) { wrap_glTexParameteri(target, pname, param); }
void CDECL wrap_glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params) { wrap_glTexParameteriv(target, pname, (const GLint *)params); }
void CDECL wrap_glGetFixedv(GLenum pname, GLfixed *params) {
    float f[16];
    glGetFloatv(pname, f);
    for (int i = 0; i < 16; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
void CDECL wrap_glGetLightxv(GLenum light, GLenum pname, GLfixed *params) {
    float f[4];
    glGetLightfv(light, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
void CDECL wrap_glGetMaterialxv(GLenum face, GLenum pname, GLfixed *params) {
    float f[4];
    glGetMaterialfv(face, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
void CDECL wrap_glGetTexEnvxv(GLenum target, GLenum pname, GLfixed *params) {
    float f[4];
    glGetTexEnvfv(target, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
void CDECL wrap_glGetTexParameterxv(GLenum target, GLenum pname, GLfixed *params) {
    float f[4];
    glGetTexParameterfv(target, pname, f);
    for (int i = 0; i < 4; i++) params[i] = (GLfixed)(f[i] * 65536.0f);
}
void CDECL wrap_glGetPointerv(GLenum pname, void **params) { glGetPointerv(pname, params); }
void CDECL wrap_glClipPlanex(GLenum plane, const GLfixed *equation) {
    GLdouble eq[4] = { FIX2FLT(equation[0]), FIX2FLT(equation[1]), FIX2FLT(equation[2]), FIX2FLT(equation[3]) };
    glClipPlane(plane, eq);
}
void CDECL wrap_glGetClipPlanex(GLenum plane, GLfixed *equation) {
    GLdouble eq[4];
    glGetClipPlane(plane, eq);
    for (int i = 0; i < 4; i++) equation[i] = (GLfixed)(eq[i] * 65536.0f);
}

static void CDECL wrap_stub(void) {}
static int CDECL wrap_ret0(void) { return 0; }
static int CDECL wrap_ret1(void) { return 1; }

void gl_wrap_init(void) {
    gl_buffers_init();

    pfn_glActiveTexture = (PFN_glActiveTexture)SDL_GL_GetProcAddress("glActiveTexture");
    pfn_glClientActiveTexture = (PFN_glClientActiveTexture)SDL_GL_GetProcAddress("glClientActiveTexture");
    pfn_glAttachShader = (PFN_glAttachShader)SDL_GL_GetProcAddress("glAttachShader");
    pfn_glBindAttribLocation = (PFN_glBindAttribLocation)SDL_GL_GetProcAddress("glBindAttribLocation");
    pfn_glBindBuffer = (PFN_glBindBuffer)SDL_GL_GetProcAddress("glBindBuffer");
    pfn_glBindFramebuffer = (PFN_glBindFramebuffer)SDL_GL_GetProcAddress("glBindFramebuffer");
    if (!pfn_glBindFramebuffer) pfn_glBindFramebuffer = (PFN_glBindFramebuffer)SDL_GL_GetProcAddress("glBindFramebufferEXT");
    pfn_glBindRenderbuffer = (PFN_glBindRenderbuffer)SDL_GL_GetProcAddress("glBindRenderbuffer");
    if (!pfn_glBindRenderbuffer) pfn_glBindRenderbuffer = (PFN_glBindRenderbuffer)SDL_GL_GetProcAddress("glBindRenderbufferEXT");
    pfn_glBlendColor = (PFN_glBlendColor)SDL_GL_GetProcAddress("glBlendColor");
    pfn_glBlendEquation = (PFN_glBlendEquation)SDL_GL_GetProcAddress("glBlendEquation");
    pfn_glBlendEquationSeparate = (PFN_glBlendEquationSeparate)SDL_GL_GetProcAddress("glBlendEquationSeparate");
    pfn_glBlendFuncSeparate = (PFN_glBlendFuncSeparate)SDL_GL_GetProcAddress("glBlendFuncSeparate");
    pfn_glBufferData = (PFN_glBufferData)SDL_GL_GetProcAddress("glBufferData");
    pfn_glBufferSubData = (PFN_glBufferSubData)SDL_GL_GetProcAddress("glBufferSubData");
    pfn_glCheckFramebufferStatus = (PFN_glCheckFramebufferStatus)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    if (!pfn_glCheckFramebufferStatus) pfn_glCheckFramebufferStatus = (PFN_glCheckFramebufferStatus)SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT");
    pfn_glCompileShader = (PFN_glCompileShader)SDL_GL_GetProcAddress("glCompileShader");
    pfn_glCompressedTexImage2D = (PFN_glCompressedTexImage2D)SDL_GL_GetProcAddress("glCompressedTexImage2D");
    pfn_glCompressedTexSubImage2D = (PFN_glCompressedTexSubImage2D)SDL_GL_GetProcAddress("glCompressedTexSubImage2D");
    pfn_glCreateProgram = (PFN_glCreateProgram)SDL_GL_GetProcAddress("glCreateProgram");
    pfn_glCreateShader = (PFN_glCreateShader)SDL_GL_GetProcAddress("glCreateShader");
    pfn_glDeleteBuffers = (PFN_glDeleteBuffers)SDL_GL_GetProcAddress("glDeleteBuffers");
    pfn_glDeleteFramebuffers = (PFN_glDeleteFramebuffers)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    if (!pfn_glDeleteFramebuffers) pfn_glDeleteFramebuffers = (PFN_glDeleteFramebuffers)SDL_GL_GetProcAddress("glDeleteFramebuffersEXT");
    pfn_glDeleteProgram = (PFN_glDeleteProgram)SDL_GL_GetProcAddress("glDeleteProgram");
    pfn_glDeleteRenderbuffers = (PFN_glDeleteRenderbuffers)SDL_GL_GetProcAddress("glDeleteRenderbuffers");
    if (!pfn_glDeleteRenderbuffers) pfn_glDeleteRenderbuffers = (PFN_glDeleteRenderbuffers)SDL_GL_GetProcAddress("glDeleteRenderbuffersEXT");
    pfn_glDeleteShader = (PFN_glDeleteShader)SDL_GL_GetProcAddress("glDeleteShader");
    pfn_glDetachShader = (PFN_glDetachShader)SDL_GL_GetProcAddress("glDetachShader");
    pfn_glDisableVertexAttribArray = (PFN_glDisableVertexAttribArray)SDL_GL_GetProcAddress("glDisableVertexAttribArray");
    pfn_glEnableVertexAttribArray = (PFN_glEnableVertexAttribArray)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    pfn_glFramebufferRenderbuffer = (PFN_glFramebufferRenderbuffer)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    if (!pfn_glFramebufferRenderbuffer) pfn_glFramebufferRenderbuffer = (PFN_glFramebufferRenderbuffer)SDL_GL_GetProcAddress("glFramebufferRenderbufferEXT");
    pfn_glFramebufferTexture2D = (PFN_glFramebufferTexture2D)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    if (!pfn_glFramebufferTexture2D) pfn_glFramebufferTexture2D = (PFN_glFramebufferTexture2D)SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
    pfn_glGenBuffers = (PFN_glGenBuffers)SDL_GL_GetProcAddress("glGenBuffers");
    pfn_glGenerateMipmap = (PFN_glGenerateMipmap)SDL_GL_GetProcAddress("glGenerateMipmap");
    if (!pfn_glGenerateMipmap) pfn_glGenerateMipmap = (PFN_glGenerateMipmap)SDL_GL_GetProcAddress("glGenerateMipmapEXT");
    pfn_glGenFramebuffers = (PFN_glGenFramebuffers)SDL_GL_GetProcAddress("glGenFramebuffers");
    if (!pfn_glGenFramebuffers) pfn_glGenFramebuffers = (PFN_glGenFramebuffers)SDL_GL_GetProcAddress("glGenFramebuffersEXT");
    pfn_glGenRenderbuffers = (PFN_glGenRenderbuffers)SDL_GL_GetProcAddress("glGenRenderbuffers");
    if (!pfn_glGenRenderbuffers) pfn_glGenRenderbuffers = (PFN_glGenRenderbuffers)SDL_GL_GetProcAddress("glGenRenderbuffersEXT");
    pfn_glGetActiveAttrib = (PFN_glGetActiveAttrib)SDL_GL_GetProcAddress("glGetActiveAttrib");
    pfn_glGetActiveUniform = (PFN_glGetActiveUniform)SDL_GL_GetProcAddress("glGetActiveUniform");
    pfn_glGetAttachedShaders = (PFN_glGetAttachedShaders)SDL_GL_GetProcAddress("glGetAttachedShaders");
    pfn_glGetAttribLocation = (PFN_glGetAttribLocation)SDL_GL_GetProcAddress("glGetAttribLocation");
    pfn_glGetBufferParameteriv = (PFN_glGetBufferParameteriv)SDL_GL_GetProcAddress("glGetBufferParameteriv");
    pfn_glGetFramebufferAttachmentParameteriv = (PFN_glGetFramebufferAttachmentParameteriv)SDL_GL_GetProcAddress("glGetFramebufferAttachmentParameteriv");
    pfn_glGetProgramInfoLog = (PFN_glGetProgramInfoLog)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    pfn_glGetProgramiv = (PFN_glGetProgramiv)SDL_GL_GetProcAddress("glGetProgramiv");
    pfn_glGetRenderbufferParameteriv = (PFN_glGetRenderbufferParameteriv)SDL_GL_GetProcAddress("glGetRenderbufferParameteriv");
    pfn_glGetShaderInfoLog = (PFN_glGetShaderInfoLog)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    pfn_glGetShaderPrecisionFormat = (PFN_glGetShaderPrecisionFormat)SDL_GL_GetProcAddress("glGetShaderPrecisionFormat");
    pfn_glGetShaderSource = (PFN_glGetShaderSource)SDL_GL_GetProcAddress("glGetShaderSource");
    pfn_glGetShaderiv = (PFN_glGetShaderiv)SDL_GL_GetProcAddress("glGetShaderiv");
    pfn_glGetUniformLocation = (PFN_glGetUniformLocation)SDL_GL_GetProcAddress("glGetUniformLocation");
    pfn_glGetUniformfv = (PFN_glGetUniformfv)SDL_GL_GetProcAddress("glGetUniformfv");
    pfn_glGetUniformiv = (PFN_glGetUniformiv)SDL_GL_GetProcAddress("glGetUniformiv");
    pfn_glGetVertexAttribPointerv = (PFN_glGetVertexAttribPointerv)SDL_GL_GetProcAddress("glGetVertexAttribPointerv");
    pfn_glGetVertexAttribfv = (PFN_glGetVertexAttribfv)SDL_GL_GetProcAddress("glGetVertexAttribfv");
    pfn_glGetVertexAttribiv = (PFN_glGetVertexAttribiv)SDL_GL_GetProcAddress("glGetVertexAttribiv");
    pfn_glIsFramebuffer = (PFN_glIsFramebuffer)SDL_GL_GetProcAddress("glIsFramebuffer");
    pfn_glIsProgram = (PFN_glIsProgram)SDL_GL_GetProcAddress("glIsProgram");
    pfn_glIsRenderbuffer = (PFN_glIsRenderbuffer)SDL_GL_GetProcAddress("glIsRenderbuffer");
    pfn_glIsShader = (PFN_glIsShader)SDL_GL_GetProcAddress("glIsShader");
    pfn_glLinkProgram = (PFN_glLinkProgram)SDL_GL_GetProcAddress("glLinkProgram");
    pfn_glMultiTexCoord4f = (PFN_glMultiTexCoord4f)SDL_GL_GetProcAddress("glMultiTexCoord4f");
    pfn_glPointParameterf = (PFN_glPointParameterf)SDL_GL_GetProcAddress("glPointParameterf");
    pfn_glPointParameterfv = (PFN_glPointParameterfv)SDL_GL_GetProcAddress("glPointParameterfv");
    pfn_glReleaseShaderCompiler = (PFN_glReleaseShaderCompiler)SDL_GL_GetProcAddress("glReleaseShaderCompiler");
    pfn_glRenderbufferStorage = (PFN_glRenderbufferStorage)SDL_GL_GetProcAddress("glRenderbufferStorage");
    if (!pfn_glRenderbufferStorage) pfn_glRenderbufferStorage = (PFN_glRenderbufferStorage)SDL_GL_GetProcAddress("glRenderbufferStorageEXT");
    pfn_glSampleCoverage = (PFN_glSampleCoverage)SDL_GL_GetProcAddress("glSampleCoverage");
    pfn_glShaderSource = (PFN_glShaderSource)SDL_GL_GetProcAddress("glShaderSource");
    pfn_glStencilFuncSeparate = (PFN_glStencilFuncSeparate)SDL_GL_GetProcAddress("glStencilFuncSeparate");
    pfn_glStencilMaskSeparate = (PFN_glStencilMaskSeparate)SDL_GL_GetProcAddress("glStencilMaskSeparate");
    pfn_glStencilOpSeparate = (PFN_glStencilOpSeparate)SDL_GL_GetProcAddress("glStencilOpSeparate");
    pfn_glUniform1f = (PFN_glUniform1f)SDL_GL_GetProcAddress("glUniform1f");
    pfn_glUniform1fv = (PFN_glUniform1fv)SDL_GL_GetProcAddress("glUniform1fv");
    pfn_glUniform1i = (PFN_glUniform1i)SDL_GL_GetProcAddress("glUniform1i");
    pfn_glUniform1iv = (PFN_glUniform1iv)SDL_GL_GetProcAddress("glUniform1iv");
    pfn_glUniform2f = (PFN_glUniform2f)SDL_GL_GetProcAddress("glUniform2f");
    pfn_glUniform2fv = (PFN_glUniform2fv)SDL_GL_GetProcAddress("glUniform2fv");
    pfn_glUniform2i = (PFN_glUniform2i)SDL_GL_GetProcAddress("glUniform2i");
    pfn_glUniform2iv = (PFN_glUniform2iv)SDL_GL_GetProcAddress("glUniform2iv");
    pfn_glUniform3f = (PFN_glUniform3f)SDL_GL_GetProcAddress("glUniform3f");
    pfn_glUniform3fv = (PFN_glUniform3fv)SDL_GL_GetProcAddress("glUniform3fv");
    pfn_glUniform3i = (PFN_glUniform3i)SDL_GL_GetProcAddress("glUniform3i");
    pfn_glUniform3iv = (PFN_glUniform3iv)SDL_GL_GetProcAddress("glUniform3iv");
    pfn_glUniform4f = (PFN_glUniform4f)SDL_GL_GetProcAddress("glUniform4f");
    pfn_glUniform4fv = (PFN_glUniform4fv)SDL_GL_GetProcAddress("glUniform4fv");
    pfn_glUniform4i = (PFN_glUniform4i)SDL_GL_GetProcAddress("glUniform4i");
    pfn_glUniform4iv = (PFN_glUniform4iv)SDL_GL_GetProcAddress("glUniform4iv");
    pfn_glUniformMatrix2fv = (PFN_glUniformMatrix2fv)SDL_GL_GetProcAddress("glUniformMatrix2fv");
    pfn_glUniformMatrix3fv = (PFN_glUniformMatrix3fv)SDL_GL_GetProcAddress("glUniformMatrix3fv");
    pfn_glUniformMatrix4fv = (PFN_glUniformMatrix4fv)SDL_GL_GetProcAddress("glUniformMatrix4fv");
    pfn_glUseProgram = (PFN_glUseProgram)SDL_GL_GetProcAddress("glUseProgram");
    pfn_glValidateProgram = (PFN_glValidateProgram)SDL_GL_GetProcAddress("glValidateProgram");
    pfn_glVertexAttrib1f = (PFN_glVertexAttrib1f)SDL_GL_GetProcAddress("glVertexAttrib1f");
    pfn_glVertexAttrib1fv = (PFN_glVertexAttrib1fv)SDL_GL_GetProcAddress("glVertexAttrib1fv");
    pfn_glVertexAttrib2f = (PFN_glVertexAttrib2f)SDL_GL_GetProcAddress("glVertexAttrib2f");
    pfn_glVertexAttrib2fv = (PFN_glVertexAttrib2fv)SDL_GL_GetProcAddress("glVertexAttrib2fv");
    pfn_glVertexAttrib3f = (PFN_glVertexAttrib3f)SDL_GL_GetProcAddress("glVertexAttrib3f");
    pfn_glVertexAttrib3fv = (PFN_glVertexAttrib3fv)SDL_GL_GetProcAddress("glVertexAttrib3fv");
    pfn_glVertexAttrib4f = (PFN_glVertexAttrib4f)SDL_GL_GetProcAddress("glVertexAttrib4f");
    pfn_glVertexAttrib4fv = (PFN_glVertexAttrib4fv)SDL_GL_GetProcAddress("glVertexAttrib4fv");
    pfn_glVertexAttribPointer = (PFN_glVertexAttribPointer)SDL_GL_GetProcAddress("glVertexAttribPointer");

    gl_wrap_set_texture_quality(g_requested_auto_mips, g_requested_aniso);
}

void gl_wrap_set_texture_quality(int auto_mipmaps, int anisotropy) {
    g_requested_auto_mips = auto_mipmaps;
    g_requested_aniso = anisotropy;

    const char *version = (const char *)glGetString(GL_VERSION);
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (!version) return; /* GL context not yet current */

    int major = 0, minor = 0;
    sscanf(version, "%d.%d", &major, &minor);

    g_tex_mipmaps = auto_mipmaps && (major > 1 || (major == 1 && minor >= 4));
    g_tex_aniso = 0.0f;
    if (anisotropy > 1 && extensions && strstr(extensions, "GL_EXT_texture_filter_anisotropic")) {
        GLfloat max_aniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
        g_tex_aniso = (float)anisotropy < max_aniso ? (float)anisotropy : max_aniso;
    }

    printf("[GL] Texture filtering: %s, anisotropy %.0fx\n",
           g_tex_mipmaps ? "trilinear + auto-mipmaps" : "trilinear (on mipmapped textures)",
           g_tex_aniso > 1.0f ? g_tex_aniso : 1.0f);
    fflush(stdout);
}

/* Unified GLES 1.1 + GLES 2.0 dispatch table */
static const gl_dispatch_entry_t g_gl_dispatch[] = {
    /* Core & GLES 2.0 */
    { "glActiveTexture",                       (void *)wrap_glActiveTexture },
    { "glAttachShader",                        (void *)wrap_glAttachShader },
    { "glBindAttribLocation",                  (void *)wrap_glBindAttribLocation },
    { "glBindBuffer",                          (void *)glBindBuffer_soloader },
    { "glBindBufferOES",                       (void *)glBindBuffer_soloader },
    { "glBindFramebuffer",                     (void *)wrap_glBindFramebuffer },
    { "glBindFramebufferOES",                  (void *)wrap_glBindFramebuffer },
    { "glBindRenderbuffer",                    (void *)wrap_glBindRenderbuffer },
    { "glBindRenderbufferOES",                 (void *)wrap_glBindRenderbuffer },
    { "glBindTexture",                         (void *)wrap_glBindTexture },
    { "glBlendColor",                          (void *)wrap_glBlendColor },
    { "glBlendEquation",                       (void *)wrap_glBlendEquation },
    { "glBlendEquationSeparate",               (void *)wrap_glBlendEquationSeparate },
    { "glBlendFunc",                           (void *)wrap_glBlendFunc },
    { "glBlendFuncSeparate",                   (void *)wrap_glBlendFuncSeparate },
    { "glBufferData",                          (void *)wrap_glBufferData },
    { "glBufferDataOES",                       (void *)wrap_glBufferData },
    { "glBufferSubData",                       (void *)wrap_glBufferSubData },
    { "glBufferSubDataOES",                    (void *)wrap_glBufferSubData },
    { "glCheckFramebufferStatus",              (void *)wrap_glCheckFramebufferStatus },
    { "glCheckFramebufferStatusOES",           (void *)wrap_glCheckFramebufferStatus },
    { "glClear",                               (void *)wrap_glClear },
    { "glClearColor",                          (void *)wrap_glClearColor },
    { "glClearDepthf",                         (void *)wrap_glClearDepthf },
    { "glClearStencil",                        (void *)wrap_glClearStencil },
    { "glColorMask",                           (void *)wrap_glColorMask },
    { "glCompileShader",                       (void *)wrap_glCompileShader },
    { "glCompressedTexImage2D",                (void *)wrap_glCompressedTexImage2D },
    { "glCompressedTexSubImage2D",             (void *)wrap_glCompressedTexSubImage2D },
    { "glCopyTexImage2D",                      (void *)wrap_glCopyTexImage2D },
    { "glCopyTexSubImage2D",                   (void *)wrap_glCopyTexSubImage2D },
    { "glCreateProgram",                       (void *)wrap_glCreateProgram },
    { "glCreateShader",                        (void *)wrap_glCreateShader },
    { "glCullFace",                            (void *)wrap_glCullFace },
    { "glDeleteBuffers",                       (void *)glDeleteBuffers_soloader },
    { "glDeleteBuffersOES",                    (void *)glDeleteBuffers_soloader },
    { "glDeleteFramebuffers",                  (void *)wrap_glDeleteFramebuffers },
    { "glDeleteFramebuffersOES",               (void *)wrap_glDeleteFramebuffers },
    { "glDeleteProgram",                       (void *)wrap_glDeleteProgram },
    { "glDeleteRenderbuffers",                 (void *)wrap_glDeleteRenderbuffers },
    { "glDeleteRenderbuffersOES",              (void *)wrap_glDeleteRenderbuffers },
    { "glDeleteShader",                        (void *)wrap_glDeleteShader },
    { "glDeleteTextures",                      (void *)wrap_glDeleteTextures },
    { "glDepthFunc",                           (void *)wrap_glDepthFunc },
    { "glDepthMask",                           (void *)wrap_glDepthMask },
    { "glDepthRangef",                         (void *)wrap_glDepthRangef },
    { "glDetachShader",                        (void *)wrap_glDetachShader },
    { "glDisable",                             (void *)wrap_glDisable },
    { "glDisableVertexAttribArray",            (void *)wrap_glDisableVertexAttribArray },
    { "glDrawArrays",                          (void *)wrap_glDrawArrays },
    { "glDrawElements",                        (void *)wrap_glDrawElements },
    { "glEnable",                              (void *)wrap_glEnable },
    { "glEnableVertexAttribArray",             (void *)wrap_glEnableVertexAttribArray },
    { "glFinish",                              (void *)wrap_glFinish },
    { "glFlush",                               (void *)wrap_glFlush },
    { "glFramebufferRenderbuffer",             (void *)wrap_glFramebufferRenderbuffer },
    { "glFramebufferRenderbufferOES",          (void *)wrap_glFramebufferRenderbuffer },
    { "glFramebufferTexture2D",                (void *)wrap_glFramebufferTexture2D },
    { "glFramebufferTexture2DOES",             (void *)wrap_glFramebufferTexture2D },
    { "glFrontFace",                           (void *)wrap_glFrontFace },
    { "glGenBuffers",                          (void *)glGenBuffers_soloader },
    { "glGenBuffersOES",                       (void *)glGenBuffers_soloader },
    { "glGenerateMipmap",                      (void *)wrap_glGenerateMipmap },
    { "glGenerateMipmapOES",                   (void *)wrap_glGenerateMipmap },
    { "glGenFramebuffers",                     (void *)wrap_glGenFramebuffers },
    { "glGenFramebuffersOES",                  (void *)wrap_glGenFramebuffers },
    { "glGenRenderbuffers",                    (void *)wrap_glGenRenderbuffers },
    { "glGenRenderbuffersOES",                 (void *)wrap_glGenRenderbuffers },
    { "glGenTextures",                         (void *)wrap_glGenTextures },
    { "glGetActiveAttrib",                     (void *)wrap_glGetActiveAttrib },
    { "glGetActiveUniform",                    (void *)wrap_glGetActiveUniform },
    { "glGetAttachedShaders",                  (void *)wrap_glGetAttachedShaders },
    { "glGetAttribLocation",                   (void *)wrap_glGetAttribLocation },
    { "glGetBooleanv",                         (void *)wrap_glGetBooleanv },
    { "glGetBufferParameteriv",                (void *)wrap_glGetBufferParameteriv },
    { "glGetBufferParameterivOES",             (void *)wrap_glGetBufferParameteriv },
    { "glGetError",                            (void *)wrap_glGetError },
    { "glGetFloatv",                           (void *)wrap_glGetFloatv },
    { "glGetFramebufferAttachmentParameteriv", (void *)wrap_glGetFramebufferAttachmentParameteriv },
    { "glGetIntegerv",                         (void *)wrap_glGetIntegerv },
    { "glGetProgramInfoLog",                   (void *)wrap_glGetProgramInfoLog },
    { "glGetProgramiv",                        (void *)wrap_glGetProgramiv },
    { "glGetRenderbufferParameteriv",          (void *)wrap_glGetRenderbufferParameteriv },
    { "glGetShaderInfoLog",                    (void *)wrap_glGetShaderInfoLog },
    { "glGetShaderPrecisionFormat",            (void *)wrap_glGetShaderPrecisionFormat },
    { "glGetShaderSource",                     (void *)wrap_glGetShaderSource },
    { "glGetShaderiv",                         (void *)wrap_glGetShaderiv },
    { "glGetString",                           (void *)wrap_glGetString },
    { "glGetTexParameterfv",                   (void *)wrap_glGetTexParameterfv },
    { "glGetTexParameteriv",                   (void *)wrap_glGetTexParameteriv },
    { "glGetUniformLocation",                  (void *)wrap_glGetUniformLocation },
    { "glGetUniformfv",                        (void *)wrap_glGetUniformfv },
    { "glGetUniformiv",                        (void *)wrap_glGetUniformiv },
    { "glGetVertexAttribPointerv",             (void *)wrap_glGetVertexAttribPointerv },
    { "glGetVertexAttribfv",                   (void *)wrap_glGetVertexAttribfv },
    { "glGetVertexAttribiv",                   (void *)wrap_glGetVertexAttribiv },
    { "glHint",                                (void *)wrap_glHint },
    { "glIsBuffer",                            (void *)glIsBuffer_soloader },
    { "glIsBufferOES",                         (void *)glIsBuffer_soloader },
    { "glIsEnabled",                           (void *)wrap_glIsEnabled },
    { "glIsFramebuffer",                       (void *)wrap_glIsFramebuffer },
    { "glIsProgram",                           (void *)wrap_glIsProgram },
    { "glIsRenderbuffer",                      (void *)wrap_glIsRenderbuffer },
    { "glIsShader",                            (void *)wrap_glIsShader },
    { "glIsTexture",                           (void *)wrap_glIsTexture },
    { "glLineWidth",                           (void *)wrap_glLineWidth },
    { "glLinkProgram",                         (void *)wrap_glLinkProgram },
    { "glPixelStorei",                         (void *)wrap_glPixelStorei },
    { "glPolygonOffset",                       (void *)wrap_glPolygonOffset },
    { "glReadPixels",                          (void *)wrap_glReadPixels },
    { "glReleaseShaderCompiler",               (void *)wrap_glReleaseShaderCompiler },
    { "glRenderbufferStorage",                 (void *)wrap_glRenderbufferStorage },
    { "glRenderbufferStorageOES",              (void *)wrap_glRenderbufferStorage },
    { "glSampleCoverage",                      (void *)wrap_glSampleCoverage },
    { "glScissor",                             (void *)wrap_glScissor },
    { "glShaderSource",                        (void *)wrap_glShaderSource },
    { "glStencilFunc",                         (void *)wrap_glStencilFunc },
    { "glStencilFuncSeparate",                 (void *)wrap_glStencilFuncSeparate },
    { "glStencilMask",                         (void *)wrap_glStencilMask },
    { "glStencilMaskSeparate",                 (void *)wrap_glStencilMaskSeparate },
    { "glStencilOp",                           (void *)wrap_glStencilOp },
    { "glStencilOpSeparate",                   (void *)wrap_glStencilOpSeparate },
    { "glTexImage2D",                          (void *)wrap_glTexImage2D },
    { "glTexParameterf",                       (void *)wrap_glTexParameterf },
    { "glTexParameterfv",                      (void *)wrap_glTexParameterfv },
    { "glTexParameteri",                       (void *)wrap_glTexParameteri },
    { "glTexParameteriv",                      (void *)wrap_glTexParameteriv },
    { "glTexSubImage2D",                       (void *)wrap_glTexSubImage2D },
    { "glUniform1f",                           (void *)wrap_glUniform1f },
    { "glUniform1fv",                          (void *)wrap_glUniform1fv },
    { "glUniform1i",                           (void *)wrap_glUniform1i },
    { "glUniform1iv",                          (void *)wrap_glUniform1iv },
    { "glUniform2f",                           (void *)wrap_glUniform2f },
    { "glUniform2fv",                          (void *)wrap_glUniform2fv },
    { "glUniform2i",                           (void *)wrap_glUniform2i },
    { "glUniform2iv",                          (void *)wrap_glUniform2iv },
    { "glUniform3f",                           (void *)wrap_glUniform3f },
    { "glUniform3fv",                          (void *)wrap_glUniform3fv },
    { "glUniform3i",                           (void *)wrap_glUniform3i },
    { "glUniform3iv",                          (void *)wrap_glUniform3iv },
    { "glUniform4f",                           (void *)wrap_glUniform4f },
    { "glUniform4fv",                          (void *)wrap_glUniform4fv },
    { "glUniform4i",                           (void *)wrap_glUniform4i },
    { "glUniform4iv",                          (void *)wrap_glUniform4iv },
    { "glUniformMatrix2fv",                    (void *)wrap_glUniformMatrix2fv },
    { "glUniformMatrix3fv",                    (void *)wrap_glUniformMatrix3fv },
    { "glUniformMatrix4fv",                    (void *)wrap_glUniformMatrix4fv },
    { "glUseProgram",                          (void *)wrap_glUseProgram },
    { "glValidateProgram",                     (void *)wrap_glValidateProgram },
    { "glVertexAttrib1f",                      (void *)wrap_glVertexAttrib1f },
    { "glVertexAttrib1fv",                     (void *)wrap_glVertexAttrib1fv },
    { "glVertexAttrib2f",                      (void *)wrap_glVertexAttrib2f },
    { "glVertexAttrib2fv",                     (void *)wrap_glVertexAttrib2fv },
    { "glVertexAttrib3f",                      (void *)wrap_glVertexAttrib3f },
    { "glVertexAttrib3fv",                     (void *)wrap_glVertexAttrib3fv },
    { "glVertexAttrib4f",                      (void *)wrap_glVertexAttrib4f },
    { "glVertexAttrib4fv",                     (void *)wrap_glVertexAttrib4fv },
    { "glVertexAttribPointer",                 (void *)wrap_glVertexAttribPointer },
    { "glViewport",                            (void *)wrap_glViewport },

    /* GLES 1.1 Fixed-Function & Extensions */
    { "glAlphaFunc",                           (void *)wrap_glAlphaFunc },
    { "glClientActiveTexture",                 (void *)wrap_glClientActiveTexture },
    { "glClipPlanef",                          (void *)wrap_glClipPlanef },
    { "glColor4f",                             (void *)wrap_glColor4f },
    { "glColor4ub",                            (void *)wrap_glColor4ub },
    { "glColorPointer",                        (void *)wrap_glColorPointer },
    { "glDisableClientState",                  (void *)wrap_glDisableClientState },
    { "glEnableClientState",                   (void *)wrap_glEnableClientState },
    { "glFogf",                                (void *)wrap_glFogf },
    { "glFogfv",                               (void *)wrap_glFogfv },
    { "glFrustumf",                            (void *)wrap_glFrustumf },
    { "glGetClipPlanef",                       (void *)wrap_glGetClipPlanef },
    { "glGetLightfv",                          (void *)wrap_glGetLightfv },
    { "glGetMaterialfv",                       (void *)wrap_glGetMaterialfv },
    { "glGetPointerv",                         (void *)wrap_glGetPointerv },
    { "glGetTexEnvfv",                         (void *)wrap_glGetTexEnvfv },
    { "glGetTexEnviv",                         (void *)wrap_glGetTexEnviv },
    { "glLightModelf",                         (void *)wrap_glLightModelf },
    { "glLightModelfv",                        (void *)wrap_glLightModelfv },
    { "glLightf",                              (void *)wrap_glLightf },
    { "glLightfv",                             (void *)wrap_glLightfv },
    { "glLoadIdentity",                        (void *)wrap_glLoadIdentity },
    { "glLoadMatrixf",                         (void *)wrap_glLoadMatrixf },
    { "glLogicOp",                             (void *)wrap_glLogicOp },
    { "glMaterialf",                           (void *)wrap_glMaterialf },
    { "glMaterialfv",                          (void *)wrap_glMaterialfv },
    { "glMatrixMode",                          (void *)wrap_glMatrixMode },
    { "glMultMatrixf",                         (void *)wrap_glMultMatrixf },
    { "glMultiTexCoord4f",                     (void *)wrap_glMultiTexCoord4f },
    { "glNormal3f",                            (void *)wrap_glNormal3f },
    { "glNormalPointer",                       (void *)wrap_glNormalPointer },
    { "glOrthof",                              (void *)wrap_glOrthof },
    { "glPointParameterf",                     (void *)wrap_glPointParameterf },
    { "glPointParameterfv",                    (void *)wrap_glPointParameterfv },
    { "glPointSize",                           (void *)wrap_glPointSize },
    { "glPopMatrix",                           (void *)wrap_glPopMatrix },
    { "glPushMatrix",                          (void *)wrap_glPushMatrix },
    { "glRotatef",                             (void *)wrap_glRotatef },
    { "glScalef",                              (void *)wrap_glScalef },
    { "glShadeModel",                          (void *)wrap_glShadeModel },
    { "glTexCoordPointer",                     (void *)wrap_glTexCoordPointer },
    { "glTexEnvi",                             (void *)wrap_glTexEnvi },
    { "glTexEnvf",                             (void *)wrap_glTexEnvf },
    { "glTexEnviv",                            (void *)wrap_glTexEnviv },
    { "glTexEnvfv",                            (void *)wrap_glTexEnvfv },
    { "glTranslatef",                          (void *)wrap_glTranslatef },
    { "glVertexPointer",                       (void *)wrap_glVertexPointer },

    /* GLES 1.1 Fixed-Point (gl*x) */
    { "glAlphaFuncx",                          (void *)wrap_glAlphaFuncx },
    { "glClearColorx",                         (void *)wrap_glClearColorx },
    { "glClearDepthx",                         (void *)wrap_glClearDepthx },
    { "glClipPlanex",                          (void *)wrap_glClipPlanex },
    { "glColor4x",                             (void *)wrap_glColor4x },
    { "glDepthRangex",                         (void *)wrap_glDepthRangex },
    { "glFogx",                                (void *)wrap_glFogx },
    { "glFogxv",                               (void *)wrap_glFogxv },
    { "glFrustumx",                            (void *)wrap_glFrustumx },
    { "glGetClipPlanex",                       (void *)wrap_glGetClipPlanex },
    { "glGetFixedv",                           (void *)wrap_glGetFixedv },
    { "glGetLightxv",                          (void *)wrap_glGetLightxv },
    { "glGetMaterialxv",                       (void *)wrap_glGetMaterialxv },
    { "glGetTexEnvxv",                         (void *)wrap_glGetTexEnvxv },
    { "glGetTexParameterxv",                   (void *)wrap_glGetTexParameterxv },
    { "glLightModelx",                         (void *)wrap_glLightModelx },
    { "glLightModelxv",                        (void *)wrap_glLightModelxv },
    { "glLightx",                              (void *)wrap_glLightx },
    { "glLightxv",                             (void *)wrap_glLightxv },
    { "glLineWidthx",                          (void *)wrap_glLineWidthx },
    { "glLoadMatrixx",                         (void *)wrap_glLoadMatrixx },
    { "glMaterialx",                           (void *)wrap_glMaterialx },
    { "glMaterialxv",                          (void *)wrap_glMaterialxv },
    { "glMultMatrixx",                         (void *)wrap_glMultMatrixx },
    { "glMultiTexCoord4x",                     (void *)wrap_glMultiTexCoord4x },
    { "glNormal3x",                            (void *)wrap_glNormal3x },
    { "glOrthox",                              (void *)wrap_glOrthox },
    { "glPointParameterx",                     (void *)wrap_glPointParameterx },
    { "glPointParameterxv",                    (void *)wrap_glPointParameterxv },
    { "glPointSizex",                          (void *)wrap_glPointSizex },
    { "glPolygonOffsetx",                      (void *)wrap_glPolygonOffsetx },
    { "glRotatex",                             (void *)wrap_glRotatex },
    { "glSampleCoveragex",                     (void *)wrap_glSampleCoveragex },
    { "glScalex",                              (void *)wrap_glScalex },
    { "glTexEnvx",                             (void *)wrap_glTexEnvx },
    { "glTexEnvxv",                            (void *)wrap_glTexEnvxv },
    { "glTexParameterx",                       (void *)wrap_glTexParameterx },
    { "glTexParameterxv",                      (void *)wrap_glTexParameterxv },
    { "glTranslatex",                          (void *)wrap_glTranslatex },

    /* EGL Proc Address fallback */
    { "eglGetProcAddress",                     (void *)gl_get_proc },
    { "eglGetError",                           (void *)wrap_ret0 }
};

const gl_dispatch_entry_t *gl_wrap_get_dispatch_table(size_t *out_count) {
    if (out_count) *out_count = sizeof(g_gl_dispatch) / sizeof(g_gl_dispatch[0]);
    return g_gl_dispatch;
}

void *gl_get_proc(const char *name) {
    if (!name) return NULL;

    for (size_t i = 0; i < sizeof(g_gl_dispatch) / sizeof(g_gl_dispatch[0]); i++) {
        if (strcmp(g_gl_dispatch[i].name, name) == 0) {
            return g_gl_dispatch[i].fn;
        }
    }

    if (strncmp(name, "egl", 3) == 0) {
        return (void *)wrap_ret1;
    }

    printf("[GL] Unmapped function requested: %s\n", name);
    return (void *)wrap_stub;
}
