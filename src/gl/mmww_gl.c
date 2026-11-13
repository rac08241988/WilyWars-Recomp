/* OpenGL presenter for the Genesis runner.
 *
 * - Draws each finished frame with OpenGL 3.3 (own context on the window; the
 *   runner's SDL_Renderer is forced onto its OpenGL driver so both share the
 *   window's pixel format).
 * - Runs an optional librashader slang preset over the frame (librashader.dll
 *   is loaded at runtime from shaders/ beside the exe; default Off, see
 *   SHADERS.md).
 * - Esc / F12 / pad Guide opens a side panel (rpanel software canvas drawn as
 *   an overlay): Resume, Shader (Left/Right = live preview), Filter,
 *   Widescreen, Quit. The guest is frozen meanwhile. (No save states: the
 *   game's own EEPROM saves are the only saves.)
 * - Settings persist in mmww_video.ini beside the exe. renderer=sdl there (or
 *   MMWW_RENDERER=sdl) keeps the runner's original SDL_Renderer path. */
#include "mmww_gl.h"
int mmww_video_native_w(void);   /* game/mmww_video.c */
#include "host_present_hooks.h"
#include "gamepad.h"
#include "ui/rpanel.h"
#include "recomp_updater.h"

#define LIBRA_RUNTIME_OPENGL
#include "librashader_ld.h"

#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif


/* ------------------------------------------------------------------ GL */
typedef unsigned int GLu;
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA8 0x8058
#define GL_RGBA 0x1908
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE0 0x84C0
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_TRIANGLE_STRIP 0x0005
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define APIE SDLCALL

static struct {
  void (APIE *GenTextures)(int, GLu *);
  void (APIE *DeleteTextures)(int, const GLu *);
  void (APIE *BindTexture)(unsigned, GLu);
  void (APIE *TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
  void (APIE *TexSubImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
  void (APIE *TexParameteri)(unsigned, unsigned, int);
  void (APIE *PixelStorei)(unsigned, int);
  void (APIE *GenFramebuffers)(int, GLu *);
  void (APIE *BindFramebuffer)(unsigned, GLu);
  void (APIE *FramebufferTexture2D)(unsigned, unsigned, unsigned, GLu, int);
  void (APIE *BlitFramebuffer)(int, int, int, int, int, int, int, int, unsigned, unsigned);
  void (APIE *ActiveTexture)(unsigned);
  void (APIE *BindSampler)(unsigned, GLu);
  void (APIE *GenVertexArrays)(int, GLu *);
  void (APIE *BindVertexArray)(GLu);
  void (APIE *BindBuffer)(unsigned, GLu);
  void (APIE *UseProgram)(GLu);
  void (APIE *Viewport)(int, int, int, int);
  void (APIE *Disable)(unsigned);
  void (APIE *Enable)(unsigned);
  void (APIE *BlendFunc)(unsigned, unsigned);
  void (APIE *ClearColor)(float, float, float, float);
  void (APIE *Clear)(unsigned);
  GLu  (APIE *CreateShader)(unsigned);
  void (APIE *ShaderSource)(GLu, int, const char *const *, const int *);
  void (APIE *CompileShader)(GLu);
  void (APIE *GetShaderiv)(GLu, unsigned, int *);
  void (APIE *GetShaderInfoLog)(GLu, int, int *, char *);
  GLu  (APIE *CreateProgram)(void);
  void (APIE *AttachShader)(GLu, GLu);
  void (APIE *LinkProgram)(GLu);
  void (APIE *GetProgramiv)(GLu, unsigned, int *);
  int  (APIE *GetUniformLocation)(GLu, const char *);
  void (APIE *Uniform1i)(int, int);
  void (APIE *Uniform4f)(int, float, float, float, float);
  void (APIE *DrawArrays)(unsigned, int, int);
} gl;

static int gl_load(void) {
#define L(f) *(void **)&gl.f = SDL_GL_GetProcAddress("gl" #f); \
  if (!gl.f) { fprintf(stderr, "[GL] missing gl" #f "\n"); return 0; }
  L(GenTextures) L(DeleteTextures) L(BindTexture) L(TexImage2D) L(TexSubImage2D)
  L(TexParameteri) L(PixelStorei) L(GenFramebuffers) L(BindFramebuffer)
  L(FramebufferTexture2D) L(BlitFramebuffer) L(ActiveTexture) L(BindSampler)
  L(GenVertexArrays) L(BindVertexArray) L(BindBuffer) L(UseProgram) L(Viewport)
  L(Disable) L(Enable) L(BlendFunc) L(ClearColor) L(Clear) L(CreateShader)
  L(ShaderSource) L(CompileShader) L(GetShaderiv) L(GetShaderInfoLog)
  L(CreateProgram) L(AttachShader) L(LinkProgram) L(GetProgramiv)
  L(GetUniformLocation) L(Uniform1i) L(Uniform4f) L(DrawArrays)
#undef L
  return 1;
}

/* --------------------------------------------------------------- state */
static struct {
  int want_gl, active, vsync;
  SDL_GLContext ctx;
  GLu prog, vao, frame_tex, panel_tex, out_tex, out_fbo;
  int u_rect, u_uv, u_tex, u_alpha;
  int frame_w, frame_h, out_w, out_h, panel_w, panel_h;
  uint32_t *last; int last_w, last_h;      /* last frame, tightly packed */
  /* settings */
  char shader_id[256];
  int linear, widescreen;
  /* librashader */
  libra_instance_t libra; int libra_state;
  libra_gl_filter_chain_t chain; char chain_id[256];
  size_t frame_count;
  char msg[200]; Uint32 msg_until;
} G;

typedef struct { char id[160], path[400], name[96]; } ShaderEntry;
static ShaderEntry *g_shaders; static int g_shader_count;

static const char *base_dir(void) {
  static char b[1024];
  if (!b[0]) { char *p = SDL_GetBasePath(); snprintf(b, sizeof b, "%s", p ? p : ""); SDL_free(p); }
  return b;
}

static void note(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vsnprintf(G.msg, sizeof G.msg, fmt, ap); va_end(ap);
  G.msg_until = SDL_GetTicks() + 4000;
  fprintf(stderr, "[PANEL] %s\n", G.msg);
}

/* ------------------------------------------------------------ settings */
static void ini_path(char *out, size_t n) { snprintf(out, n, "%smmww_video.ini", base_dir()); }

static void settings_load(void) {
  char p[1100]; ini_path(p, sizeof p);
  G.want_gl = 1;
  FILE *f = fopen(p, "r");
  if (f) {
    char line[512];
    while (fgets(line, sizeof line, f)) {
      char *eq = strchr(line, '='); if (!eq || line[0] == '#') continue;
      *eq = 0; char *v = eq + 1; v[strcspn(v, "\r\n")] = 0;
      if (!strcmp(line, "renderer")) G.want_gl = strcmp(v, "sdl") != 0;
      else if (!strcmp(line, "shader")) snprintf(G.shader_id, sizeof G.shader_id, "%s", v);
      else if (!strcmp(line, "filter")) G.linear = !strcmp(v, "linear");
      else if (!strcmp(line, "widescreen")) G.widescreen = !strcmp(v, "on");
    }
    fclose(f);
  }
  const char *e = getenv("MMWW_RENDERER");
  if (e && *e) G.want_gl = strcmp(e, "sdl") != 0;
  e = getenv("MMWW_SHADER");
  if (e) snprintf(G.shader_id, sizeof G.shader_id, "%s", e);
  e = getenv("MMWW_WIDESCREEN");
  if (e && *e) G.widescreen = !strcmp(e, "on") || !strcmp(e, "1");
}

static void settings_save(void) {
  char p[1100]; ini_path(p, sizeof p);
  FILE *f = fopen(p, "w"); if (!f) return;
  fprintf(f, "# Mega Man: The Wily Wars Recomp video settings (written by the in-game panel)\n");
  fprintf(f, "renderer=%s\nshader=%s\nfilter=%s\nwidescreen=%s\n",
          G.want_gl ? "opengl" : "sdl", G.shader_id, G.linear ? "linear" : "nearest",
          G.widescreen ? "on" : "off");
  fclose(f);
}

static void shaders_scan(void) {
  char p[1100]; snprintf(p, sizeof p, "%sshaders/slang/manifest.txt", base_dir());
  FILE *f = fopen(p, "r"); if (!f) return;
  char line[1024];
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#' || !strchr(line, '|')) continue;
    char *a = strtok(line, "|"), *b = strtok(NULL, "|"), *c = strtok(NULL, "|");
    if (!a || !b || !c) continue;
    while (*a == ' ') a++; while (*b == ' ') b++; while (*c == ' ') c++;
    ShaderEntry e; memset(&e, 0, sizeof e);
    snprintf(e.id, sizeof e.id, "%s", a); e.id[strcspn(e.id, " ")] = 0;
    snprintf(e.path, sizeof e.path, "%sshaders/slang/%s", base_dir(), b);
    e.path[strcspn(e.path, "\r\n")] = 0;
    { size_t n = strlen(e.path); while (n && e.path[n-1] == ' ') e.path[--n] = 0; }
    snprintf(e.name, sizeof e.name, "%s", c);
    { size_t n = strcspn(e.name, "\r\n"); e.name[n] = 0; while (n && e.name[n-1] == ' ') e.name[--n] = 0; }
    ShaderEntry *g = realloc(g_shaders, sizeof *g * (size_t)(g_shader_count + 1));
    if (!g) break; g_shaders = g; g_shaders[g_shader_count++] = e;
  }
  fclose(f);
  fprintf(stderr, "[GL] %d slang presets listed from %s\n", g_shader_count, p);
}

static int shader_index(void) {  /* -1 = Off */
  for (int i = 0; i < g_shader_count; i++) if (!strcmp(g_shaders[i].id, G.shader_id)) return i;
  return -1;
}

/* ----------------------------------------------------------- librashader */
static const void *gl_loader(const char *name) { return SDL_GL_GetProcAddress(name); }

static int libra_ok(void) {
  if (G.libra_state) return G.libra_state > 0;
#ifdef _WIN32
  const char *rel[] = {"shaders\\librashader.dll", "librashader.dll"};
  for (int i = 0; i < 2; i++) {
    char p[1200]; snprintf(p, sizeof p, "%s%s", base_dir(), rel[i]);
    if (LoadLibraryA(p)) { fprintf(stderr, "[GL] loaded %s\n", p); break; }
  }
#endif
  G.libra = librashader_load_instance();
  G.libra_state = G.libra.instance_loaded ? 1 : -1;
  if (G.libra_state < 0) fprintf(stderr, "[GL] librashader.dll not found: shaders unavailable\n");
  return G.libra_state > 0;
}

static void libra_err(const char *what, libra_error_t err) {
  char *m = NULL;
  if (err && G.libra.error_write(err, &m) == 0 && m) {
    note("Shader %s failed: %.120s", what, m); G.libra.error_free_string(&m);
  } else note("Shader %s failed", what);
  if (err) G.libra.error_free(&err);
}

static void chain_sync(void) {
  if (!strcmp(G.chain_id, G.shader_id) && (G.chain || !G.shader_id[0])) return;
  if (G.chain) { G.libra.gl_filter_chain_free(&G.chain); G.chain = NULL; }
  snprintf(G.chain_id, sizeof G.chain_id, "%s", G.shader_id);
  int i = shader_index();
  if (i < 0) return;
  if (!libra_ok()) { note("librashader.dll not found"); G.shader_id[0] = 0; G.chain_id[0] = 0; return; }
  libra_shader_preset_t preset = NULL;
  libra_error_t err = G.libra.preset_create(g_shaders[i].path, &preset);
  if (err) { libra_err("preset", err); G.shader_id[0] = G.chain_id[0] = 0; return; }
  filter_chain_gl_opt_t opt; memset(&opt, 0, sizeof opt);
  opt.version = LIBRASHADER_CURRENT_VERSION; opt.glsl_version = 330;
  err = G.libra.gl_filter_chain_create(&preset, gl_loader, &opt, &G.chain);
  if (preset) G.libra.preset_free(&preset);
  if (err) { G.chain = NULL; libra_err("compile", err); G.shader_id[0] = G.chain_id[0] = 0; return; }
  G.frame_count = 0;
  fprintf(stderr, "[GL] shader on: %s (%s)\n", g_shaders[i].name, g_shaders[i].path);
}

/* --------------------------------------------------------------- drawing */
static const char *VS =
  "#version 330 core\n"
  "uniform vec4 rect; uniform vec4 uv; out vec2 t;\n"
  "void main(){ vec2 c=vec2(float(gl_VertexID&1),float(gl_VertexID>>1));\n"
  " t=mix(uv.xy,uv.zw,c); gl_Position=vec4(mix(rect.xy,rect.zw,c),0.0,1.0);}\n";
static const char *FS =
  "#version 330 core\n"
  "uniform sampler2D tex; uniform vec4 alpha; in vec2 t; out vec4 o;\n"
  "void main(){ vec4 c=texture(tex,t); o=vec4(c.rgb, c.a*alpha.x);}\n";

static GLu compile(unsigned type, const char *src) {
  GLu s = gl.CreateShader(type); gl.ShaderSource(s, 1, &src, NULL); gl.CompileShader(s);
  int ok = 0; gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[512]; gl.GetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "[GL] shader: %s\n", log); }
  return s;
}

static int gl_setup(void) {
  if (!gl_load()) return 0;
  G.prog = gl.CreateProgram();
  gl.AttachShader(G.prog, compile(GL_VERTEX_SHADER, VS));
  gl.AttachShader(G.prog, compile(GL_FRAGMENT_SHADER, FS));
  gl.LinkProgram(G.prog);
  int ok = 0; gl.GetProgramiv(G.prog, GL_LINK_STATUS, &ok);
  if (!ok) { fprintf(stderr, "[GL] link failed\n"); return 0; }
  G.u_rect = gl.GetUniformLocation(G.prog, "rect");
  G.u_uv = gl.GetUniformLocation(G.prog, "uv");
  G.u_tex = gl.GetUniformLocation(G.prog, "tex");
  G.u_alpha = gl.GetUniformLocation(G.prog, "alpha");
  gl.GenVertexArrays(1, &G.vao);
  gl.GenTextures(1, &G.frame_tex);
  gl.GenTextures(1, &G.panel_tex);
  return 1;
}

static void tex_params(GLu t, int linear) {
  gl.BindTexture(GL_TEXTURE_2D, t);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
}

/* Quad in window pixels (top-left origin), texture uv top row first. */
static void quad(GLu tex, int dw, int dh, int x, int y, int w, int h, float a) {
  gl.UseProgram(G.prog);
  gl.BindVertexArray(G.vao);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindSampler(0, 0);
  gl.BindTexture(GL_TEXTURE_2D, tex);
  gl.Uniform1i(G.u_tex, 0);
  float x0 = 2.0f * x / dw - 1, x1 = 2.0f * (x + w) / dw - 1;
  float y0 = 1 - 2.0f * y / dh, y1 = 1 - 2.0f * (y + h) / dh;
  /* vertex c=(0,0) is the quad's top-left: uv (0,0) = texture row 0 */
  gl.Uniform4f(G.u_rect, x0, y0, x1, y1);
  gl.Uniform4f(G.u_uv, 0, 0, 1, 1);
  gl.Uniform4f(G.u_alpha, a, 0, 0, 0);
  gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void frame_rect(int dw, int dh, int fw, int fh, int *x, int *y, int *w, int *h) {
  /* the 4:3 picture is the native row (256 in H32, 320 in H40); widened rows
   * keep that pixel aspect */
  double ar = 4.0 / 3.0;
  int base = mmww_video_native_w();
  if (G.widescreen && base > 0 && fw > base) ar = 4.0 / 3.0 * fw / base;
  (void)fh;
  int W = dw, H = (int)(dw / ar + 0.5);
  if (H > dh) { H = dh; W = (int)(dh * ar + 0.5); }
  *w = W; *h = H; *x = (dw - W) / 2; *y = (dh - H) / 2;
}

static void draw_frame(int dw, int dh) {
  gl.Viewport(0, 0, dw, dh);
  gl.Disable(GL_SCISSOR_TEST); gl.Disable(GL_BLEND); gl.Disable(GL_DEPTH_TEST);
  gl.Disable(GL_CULL_FACE); gl.Disable(GL_FRAMEBUFFER_SRGB);
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.ClearColor(0, 0, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT);
  if (!G.last || G.last_w <= 0) return;
  int x, y, w, h; frame_rect(dw, dh, G.last_w, G.last_h, &x, &y, &w, &h);
  chain_sync();
  if (G.chain) {
    if (!G.out_fbo) { gl.GenFramebuffers(1, &G.out_fbo); gl.GenTextures(1, &G.out_tex); }
    if (w != G.out_w || h != G.out_h) {
      tex_params(G.out_tex, 1);
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      gl.BindFramebuffer(GL_FRAMEBUFFER, G.out_fbo);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, G.out_tex, 0);
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      G.out_w = w; G.out_h = h;
    }
    libra_image_gl_t in = {G.frame_tex, GL_RGBA8, (uint32_t)G.frame_w, (uint32_t)G.frame_h};
    libra_image_gl_t out = {G.out_tex, GL_RGBA8, (uint32_t)w, (uint32_t)h};
    libra_viewport_t vp = {0, 0, (uint32_t)w, (uint32_t)h};
    libra_error_t err = G.libra.gl_filter_chain_frame(&G.chain, G.frame_count++, in, out, &vp, NULL, NULL);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    for (unsigned u = 0; u < 16; u++) gl.BindSampler(u, 0);
    gl.Viewport(0, 0, dw, dh);
    if (!err) {
      /* chain output keeps the input's top-first rows: flip into the window */
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, G.out_fbo);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      gl.BlitFramebuffer(0, 0, w, h, x, dh - y, x + w, dh - y - h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      return;
    }
    libra_err("frame", err);
    G.libra.gl_filter_chain_free(&G.chain); G.chain = NULL;
    G.shader_id[0] = G.chain_id[0] = 0;
  }
  tex_params(G.frame_tex, G.linear);
  quad(G.frame_tex, dw, dh, x, y, w, h, 1.0f);
}

static void upload_frame(const uint32_t *px, int stride, int w, int h) {
  if (w != G.frame_w || h != G.frame_h) {
    tex_params(G.frame_tex, G.linear);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    G.frame_w = w; G.frame_h = h;
  }
  gl.BindTexture(GL_TEXTURE_2D, G.frame_tex);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  gl.PixelStorei(GL_UNPACK_ROW_LENGTH, stride);
  gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
  gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  if (G.last_w != w || G.last_h != h) {
    free(G.last); G.last = malloc((size_t)w * h * 4); G.last_w = w; G.last_h = h;
  }
  if (G.last) for (int r = 0; r < h; r++) memcpy(G.last + (size_t)r * w, px + (size_t)r * stride, (size_t)w * 4);
}

/* ------------------------------------------------------------------ hooks */
static void hk_pre_window(Uint32 *flags) {
  settings_load();
  if (!G.want_gl) { fprintf(stderr, "[GL] renderer=sdl: OpenGL presenter off\n"); return; }
  *flags |= SDL_WINDOW_OPENGL;
  SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "opengl", SDL_HINT_OVERRIDE);
  mmww_widescreen_set(G.widescreen);
}

/* Window icon: the USA Genesis cover staged beside the exe as icon.bmp
 * (the exe itself also carries it as resource 1 for Explorer/taskbar). */
static void set_window_icon(SDL_Window *win) {
  char p[1100] = "icon.bmp";
#ifdef _WIN32
  char exe[1024]; DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
  if (n && n < sizeof exe) { char *sl = strrchr(exe, '\\'); if (sl) { *sl = 0; snprintf(p, sizeof p, "%s\\icon.bmp", exe); } }
#endif
  SDL_Surface *ic = SDL_LoadBMP(p);
  if (ic) { SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic); }
}

static void hk_window_created(SDL_Window *win, SDL_Renderer *r) {
  (void)r;
  set_window_icon(win);
  if (!G.want_gl) return;
  SDL_GLContext prev = SDL_GL_GetCurrentContext();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  G.ctx = SDL_GL_CreateContext(win);
  if (!G.ctx) {
    fprintf(stderr, "[GL] no OpenGL 3.3 context (%s): SDL renderer kept\n", SDL_GetError());
    if (prev) SDL_GL_MakeCurrent(win, prev);
    return;
  }
  if (!gl_setup()) { SDL_GL_DeleteContext(G.ctx); G.ctx = NULL; return; }
  int turbo = 0;
#ifdef _WIN32
  const char *cl = GetCommandLineA();
  turbo = cl && (strstr(cl, "--turbo") || strstr(cl, "--benchmark"));
#endif
  G.vsync = !turbo;
  SDL_GL_SetSwapInterval(G.vsync ? 1 : 0);
  shaders_scan();
  if (G.widescreen && !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)) {
    int w, h; SDL_GetWindowSize(win, &w, &h);
    SDL_SetWindowSize(win, h * 16 / 9, h);   /* 16:9 window for the widened view */
  }
  G.active = 1;
  fprintf(stderr, "[GL] OpenGL presenter on (shader=%s, filter=%s, widescreen=%s)\n",
          G.shader_id[0] ? G.shader_id : "off", G.linear ? "linear" : "nearest",
          G.widescreen ? "on" : "off");
}

static void note(const char *fmt, ...);
static int hk_present(SDL_Window *win, const uint32_t *px, int stride, int w, int h) {
  if (!G.active || !px || w <= 0 || h <= 0) return 0;
  if (SDL_GL_MakeCurrent(win, G.ctx)) return 0;
  upload_frame(px, stride, w, h);
  int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
  draw_frame(dw, dh);
  return 1;
}

static void hk_swap(SDL_Window *win) { SDL_GL_SwapWindow(win); }

static int hk_event(const SDL_Event *ev) {
  if (!G.active) return 0;
  if (ev->type == SDL_KEYDOWN && !ev->key.repeat &&
      (ev->key.keysym.sym == SDLK_ESCAPE || ev->key.keysym.sym == SDLK_F12)) return 1;
  if (ev->type == SDL_CONTROLLERBUTTONDOWN && ev->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) return 1;
  return 0;
}

/* ------------------------------------------------------------------ panel */
enum { R_RESUME, R_SHADER, R_FILTER, R_WIDE, R_UPDATE, R_QUIT, R_COUNT };
static const char *ROW_NAME[R_COUNT] = {"Resume", "Shader", "Filter", "Widescreen",
                                        "Check for updates", "Quit game"};

/* Check for updates: first Enter asks GitHub for the latest release; if a
 * newer one exists, a second Enter opens its page in the browser. */
static RecompUpdaterResult g_upd; static int g_upd_state;
static void update_row(void) {
  if (g_upd_state && g_upd.status == RECOMP_UPDATER_UPDATE_AVAILABLE) {
    note(recomp_updater_open_url(g_upd.release_url) == 0 ? "Opened the release page" : "Couldn't open the browser");
    return;
  }
  memset(&g_upd, 0, sizeof g_upd);
  recomp_updater_check(RECOMP_UPDATER_REPO_OWNER, RECOMP_UPDATER_REPO_NAME, MMWW_BUILD_VERSION, &g_upd);
  g_upd_state = 1;
  if (g_upd.status == RECOMP_UPDATER_UPDATE_AVAILABLE) note("Update available: v%s (Enter opens the page)", g_upd.latest);
  else if (g_upd.status == RECOMP_UPDATER_UP_TO_DATE) note("Up to date (v%s)", g_upd.current);
  else note("Couldn't check for updates: %s", g_upd.error[0] ? g_upd.error : "no connection");
  fprintf(stderr, "[UPDATE] status=%d current=%s latest=%s err=%s\n", g_upd.status, g_upd.current, g_upd.latest, g_upd.error);
}

static void row_value(int r, char *out, size_t n) {
  out[0] = 0;
  switch (r) {
  case R_SHADER: { int i = shader_index();
    if (!g_shader_count) snprintf(out, n, "Off (no shaders folder)");
    else snprintf(out, n, "%s", i < 0 ? "Off" : g_shaders[i].name); break; }
  case R_FILTER: snprintf(out, n, "%s", G.linear ? "Smooth" : "Sharp"); break;
  case R_WIDE: snprintf(out, n, "%s", G.widescreen ? "16:9" : "Off"); break;
  case R_UPDATE: snprintf(out, n, "%s", g_upd_state == 0 ? "v" MMWW_BUILD_VERSION
                                       : g_upd.status == RECOMP_UPDATER_UPDATE_AVAILABLE ? "Open release page"
                                       : g_upd.status == RECOMP_UPDATER_UP_TO_DATE ? "Up to date" : "Couldn't check"); break;
  }
}

static void panel_draw(RpCanvas *c, int sel) {
  rp_clear(c);
  float W = (float)c->w, H = (float)c->h, s = H / 560.0f;
  rp_fill_rrect(c, 0, 0, W, H, 0, 0xE0121620);
  rp_fill_rrect(c, 0, 0, 4 * s, H, 0, 0xFF3A7BD5);
  float x = 32 * s, y = 40 * s;
  rp_text(c, RP_BOLD, 30 * s, x, y, "Mega Man: The Wily Wars", 0xFFFFFFFF, W - 2 * x);
  y += 40 * s;
  rp_text(c, RP_REGULAR, 18 * s, x, y, "Game paused", 0xFF9AA4B2, 0);
  y += 48 * s;
  for (int r = 0; r < R_COUNT; r++) {
    float rh = 54 * s;
    if (r == sel) rp_fill_rrect(c, x - 12 * s, y, W - 2 * x + 24 * s, rh, 10 * s, 0xFF2B3A55);
    rp_text(c, RP_BOLD, 21 * s, x, y + 14 * s, ROW_NAME[r], r == sel ? 0xFFFFFFFF : 0xFFC9D1DC, 0);
    char v[160]; row_value(r, v, sizeof v);
    if (v[0]) {
      char shown[200];
      int arrows = (r == R_SHADER || r == R_FILTER || r == R_WIDE);
      snprintf(shown, sizeof shown, arrows && r == sel ? "<  %s  >" : "%s", v);
      float tw = rp_text_width(RP_REGULAR, 19 * s, shown);
      float maxw = W * 0.55f;
      if (tw > maxw) tw = maxw;
      rp_text(c, RP_REGULAR, 19 * s, W - x - tw, y + 15 * s, shown, 0xFF8FC1FF, maxw);
    }
    y += rh + 6 * s;
  }
  y += 18 * s;
  const char *hint = "Up/Down select   Left/Right change   Enter apply   Esc resume";
  rp_text(c, RP_REGULAR, 15 * s, x, y, hint, 0xFF7F8A99, W - 2 * x);
  if (G.msg[0] && SDL_TICKS_PASSED(G.msg_until, SDL_GetTicks()) == 0) {
    rp_fill_rrect(c, x - 12 * s, H - 90 * s, W - 2 * x + 24 * s, 56 * s, 10 * s, 0xFF1E2A3C);
    rp_text(c, RP_REGULAR, 17 * s, x, H - 74 * s, G.msg, 0xFFFFFFFF, W - 2 * x);
  }
  rp_finish(c);
}

static void present_menu(SDL_Window *win, RpCanvas *c, int sel) {
  int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
  SDL_GL_MakeCurrent(win, G.ctx);
  draw_frame(dw, dh);                       /* live preview behind the panel */
  int pw = (int)(dh * 0.80f); if (pw > dw) pw = dw; if (pw < 240) pw = dw < 240 ? dw : 240;
  int ph = dh;
  if (c->w != pw || c->h != ph) {
    free(c->px); c->px = calloc((size_t)pw * ph, 4); c->w = pw; c->h = ph;
    rp_canvas_noclip(c);
    tex_params(G.panel_tex, 1);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pw, ph, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
  }
  if (!c->px) return;
  panel_draw(c, sel);
  gl.BindTexture(GL_TEXTURE_2D, G.panel_tex);
  gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, c->px);
  gl.Enable(GL_BLEND); gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  quad(G.panel_tex, dw, dh, dw - pw, 0, pw, ph, 1.0f);
  gl.Disable(GL_BLEND);
  SDL_GL_SwapWindow(win);
}

static void change(int row, int d, int *running) {
  switch (row) {
  case R_SHADER: {
    int i = shader_index() + d, n = g_shader_count;
    if (!n) { note("No shaders folder beside the exe"); break; }
    if (i < -1) i = n - 1; if (i >= n) i = -1;
    snprintf(G.shader_id, sizeof G.shader_id, "%s", i < 0 ? "" : g_shaders[i].id);
    break; }
  case R_FILTER: G.linear = !G.linear; break;
  case R_WIDE: G.widescreen = !G.widescreen; mmww_widescreen_set(G.widescreen); break;
  case R_UPDATE: if (!d) update_row(); break;
  case R_QUIT: *running = 0; break;
  }
}

static void hk_run_menu(SDL_Window *win, int *running) {
  if (!G.active) return;
  RpCanvas c; memset(&c, 0, sizeof c);
  int sel = 0, open = 1;
  fprintf(stderr, "[PANEL] open\n");
  while (open && *running) {
    SDL_Event ev;
    if (SDL_WaitEventTimeout(&ev, 16)) do {
      gamepad_handle_event(&ev);
      int k = -1;  /* 0 up 1 down 2 left 3 right 4 accept 5 back */
      if (ev.type == SDL_QUIT) { *running = 0; break; }
      if (ev.type == SDL_KEYDOWN) switch (ev.key.keysym.sym) {
        case SDLK_UP: k = 0; break; case SDLK_DOWN: k = 1; break;
        case SDLK_LEFT: k = 2; break; case SDLK_RIGHT: k = 3; break;
        case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: if (!ev.key.repeat) k = 4; break;
        case SDLK_ESCAPE: case SDLK_F12: case SDLK_BACKSPACE: if (!ev.key.repeat) k = 5; break;
      }
      if (ev.type == SDL_CONTROLLERBUTTONDOWN) switch (ev.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: k = 0; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: k = 1; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: k = 2; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: k = 3; break;
        case SDL_CONTROLLER_BUTTON_A: k = 4; break;
        case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_GUIDE:
        case SDL_CONTROLLER_BUTTON_START: k = 5; break;
      }
      if (k == 0) sel = (sel + R_COUNT - 1) % R_COUNT;
      else if (k == 1) sel = (sel + 1) % R_COUNT;
      else if (k == 2 || k == 3) change(sel, k == 2 ? -1 : 1, running);
      else if (k == 4) {
        if (sel == R_RESUME) open = 0;
        else if (sel == R_SHADER || sel == R_FILTER || sel == R_WIDE) change(sel, sel == R_SHADER ? 1 : 0, running);
        else change(sel, 0, running);
      } else if (k == 5) open = 0;
    } while (open && *running && SDL_PollEvent(&ev));
    if (open && *running) present_menu(win, &c, sel);
  }
  free(c.px);
  settings_save();
  fprintf(stderr, "[PANEL] closed (shader=%s filter=%s widescreen=%s)\n",
          G.shader_id[0] ? G.shader_id : "off", G.linear ? "linear" : "nearest",
          G.widescreen ? "on" : "off");
}

__attribute__((constructor)) static void mmww_gl_install(void) {
  g_present_hooks.pre_window = hk_pre_window;
  g_present_hooks.window_created = hk_window_created;
  g_present_hooks.present = hk_present;
  g_present_hooks.swap = hk_swap;
  g_present_hooks.event = hk_event;
  g_present_hooks.run_menu = hk_run_menu;
}
