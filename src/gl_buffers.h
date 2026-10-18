#ifndef GL_BUFFERS_H
#define GL_BUFFERS_H

#include <windows.h>
#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CDECL
#define CDECL __attribute__((cdecl))
#endif

void gl_buffers_init(void);

void CDECL glGenBuffers_soloader(GLsizei n, GLuint *buffers);
void CDECL glBindBuffer_soloader(GLenum target, GLuint buffer);
void CDECL glDeleteBuffers_soloader(GLsizei n, const GLuint *buffers);
GLboolean CDECL glIsBuffer_soloader(GLuint buffer);

#ifdef __cplusplus
}
#endif

#endif /* GL_BUFFERS_H */
