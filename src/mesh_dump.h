#ifndef MESH_DUMP_H
#define MESH_DUMP_H

#include <windows.h>
#include <SDL2/SDL_opengl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_mesh_dump_enabled;

void mesh_dump_init(const char *out_dir);
void mesh_dump_shutdown(void);

void mesh_dump_draw_arrays(GLenum mode, GLint first, GLsizei count);
void mesh_dump_draw_elements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

#ifdef __cplusplus
}
#endif

#endif /* MESH_DUMP_H */
