/* OpenGL presenter, side-panel menu and librashader shaders for the Genesis
 * runner. Installs itself into g_present_hooks at startup. */
#pragma once
#include <stdint.h>

/* Widescreen switch the panel drives (implemented by game/mmww_video.c). */
int  mmww_widescreen_get(void);
void mmww_widescreen_set(int on);
