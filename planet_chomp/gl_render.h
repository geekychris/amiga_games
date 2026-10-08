/* Planet Chomp OpenGL renderer (gl_render.c): OSMesa into an ARGB buffer */
#ifndef GL_RENDER_H
#define GL_RENDER_H

/* The logical screen is 320 x 256 (an Amiga low-res PAL screen); the GL
 * buffer is that times `scale`. Rows run top to bottom, 4 bytes a pixel
 * in A R G B order (PIXF_A8R8G8B8). */
#define LOGICAL_W 320
#define LOGICAL_H 256

int            gl_open(int scale);
void           gl_close(void);
unsigned char *gl_pixels(void);
int            gl_scale(void);
const char    *gl_renderer_name(void);

extern unsigned long long (*rd_clock)(void);
extern unsigned long long rd_prof[5];

#endif
