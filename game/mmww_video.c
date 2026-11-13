/* Widescreen switch (placeholder until the GameVideo adapter lands). */
#include "../src/gl/mmww_gl.h"
static int s_on;
int mmww_widescreen_get(void) { return s_on; }
void mmww_widescreen_set(int on) { s_on = on != 0; }
