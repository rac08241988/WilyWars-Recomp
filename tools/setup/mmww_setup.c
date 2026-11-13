/* MMWW-Setup.exe: one-click builder for Mega Man: The Wily Wars Recomp.
 *
 * The release zip carries no game code and no ROM. On the player's PC this:
 *   1. asks for their own ROM with the Windows file picker and checks it
 *      (2 MB, CRC32 0CD405DB: the USA Mega Drive Mini / Genesis Mini dump),
 *   2. downloads a portable toolchain into toolchain\ (llvm-mingw, CMake,
 *      Ninja; pinned versions) unless it is already there,
 *   3. runs tools\GenesisRecomp.exe (through CMake) to translate the ROM's
 *      68000 code to C and builds the game into build\,
 *   4. copies the game, SDL2.dll, the shaders and the ROM into game\ and
 *      starts it.
 * Next time it simply starts game\WilyWarsGenesisRecomp.exe (pass --rebuild to
 * build again, e.g. after an update).
 *
 * Automation: MMWW_SETUP_ROM=<path> skips the picker; MMWW_SETUP_NO_LAUNCH=1
 * stops after the build. Everything is logged to setup.log beside the exe. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <urlmon.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/miniz/miniz.h"

#define GAME_TITLE "Mega Man: The Wily Wars Recomp"
#define ROM_SIZE 0x200000u
#define ROM_CRC 0x0CD405DBu

static const struct { const char *name, *url, *dir, *probe; } TOOLS[] = {
  {"llvm-mingw 20261006 (portable C compiler)",
   "https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-x86_64.zip",
   "llvm-mingw", "llvm-mingw\\llvm-mingw-20261006-ucrt-x86_64\\bin\\clang.exe"},
  {"CMake 3.31.8", "https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip",
   "cmake", "cmake\\cmake-3.31.8-windows-x86_64\\bin\\cmake.exe"},
  {"Ninja 1.12.1", "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip",
   "ninja", "ninja\\ninja.exe"},
};
enum { N_TOOLS = sizeof TOOLS / sizeof TOOLS[0] };

static char g_base[MAX_PATH];
static FILE *g_log;

static void say(const char *fmt, ...) {
  char line[2048]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
  printf("%s\n", line); fflush(stdout);
  if (g_log) { fprintf(g_log, "%s\n", line); fflush(g_log); }
}
static int fail(const char *fmt, ...) {
  char msg[2048]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
  say("ERROR: %s", msg);
  if (!getenv("MMWW_SETUP_ROM")) MessageBoxA(NULL, msg, GAME_TITLE " setup", MB_OK | MB_ICONERROR);
  return 1;
}
static void path(char *out, const char *rel) { snprintf(out, MAX_PATH, "%s\\%s", g_base, rel); }
static int exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
static void mkdirs(const char *p) {
  char t[MAX_PATH]; snprintf(t, sizeof t, "%s", p);
  for (char *c = t + 3; *c; c++) if (*c == '\\' || *c == '/') { char k = *c; *c = 0; CreateDirectoryA(t, NULL); *c = k; }
  CreateDirectoryA(t, NULL);
}

static uint32_t crc32_buf(const uint8_t *d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
  return ~c;
}

/* Run a command line, streaming its output to the console and setup.log. */
static int run(const char *cmdline, const char *cwd) {
  say("> %s", cmdline);
  SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
  HANDLE rd, wr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi;
  char *cl = _strdup(cmdline);
  BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
  free(cl); CloseHandle(wr);
  if (!ok) { CloseHandle(rd); say("could not start (Windows error %lu)", GetLastError()); return -1; }
  char buf[4096]; DWORD n;
  while (ReadFile(rd, buf, sizeof buf - 1, &n, NULL) && n) {
    buf[n] = 0; fputs(buf, stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
  }
  CloseHandle(rd);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  return (int)code;
}

static int unzip(const char *zip, const char *dest) {
  mz_zip_archive za; memset(&za, 0, sizeof za);
  if (!mz_zip_reader_init_file(&za, zip, 0)) return 0;
  int n = (int)mz_zip_reader_get_num_files(&za), ok = 1;
  for (int i = 0; i < n && ok; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&za, (mz_uint)i, &st)) { ok = 0; break; }
    if (strstr(st.m_filename, "..")) continue;
    char out[MAX_PATH * 2]; snprintf(out, sizeof out, "%s\\%s", dest, st.m_filename);
    for (char *c = out; *c; c++) if (*c == '/') *c = '\\';
    if (mz_zip_reader_is_file_a_directory(&za, (mz_uint)i)) { mkdirs(out); continue; }
    char dir[MAX_PATH * 2]; snprintf(dir, sizeof dir, "%s", out);
    char *slash = strrchr(dir, '\\'); if (slash) { *slash = 0; mkdirs(dir); }
    ok = mz_zip_reader_extract_to_file(&za, (mz_uint)i, out, 0);
  }
  mz_zip_reader_end(&za);
  return ok;
}

static int ensure_toolchain(void) {
  for (int i = 0; i < N_TOOLS; i++) {
    char probe[MAX_PATH], rel[MAX_PATH];
    snprintf(rel, sizeof rel, "toolchain\\%s", TOOLS[i].probe); path(probe, rel);
    if (exists(probe)) { say("toolchain: %s ready", TOOLS[i].name); continue; }
    char zip[MAX_PATH], dir[MAX_PATH];
    snprintf(rel, sizeof rel, "toolchain\\%s.zip", TOOLS[i].dir); path(zip, rel);
    snprintf(rel, sizeof rel, "toolchain\\%s", TOOLS[i].dir); path(dir, rel);
    mkdirs(dir);
    say("toolchain: downloading %s ...", TOOLS[i].name);
    HRESULT hr = URLDownloadToFileA(NULL, TOOLS[i].url, zip, 0, NULL);
    if (hr != S_OK) return fail("Download failed (0x%08lX):\n%s\nCheck the internet connection and run setup again.", (unsigned long)hr, TOOLS[i].url);
    say("toolchain: extracting %s ...", TOOLS[i].name);
    if (!unzip(zip, dir)) return fail("Could not extract %s", zip);
    DeleteFileA(zip);
    if (!exists(probe)) return fail("%s is missing after extraction", probe);
  }
  return 0;
}

static int pick_rom(char *out) {
  const char *env = getenv("MMWW_SETUP_ROM");
  if (env && *env) { snprintf(out, MAX_PATH, "%s", env); return 1; }
  MessageBoxA(NULL,
    "Pick your own Mega Man: The Wily Wars ROM (the USA Mega Drive Mini / Genesis Mini dump, 2 MB).\n\n"
    "Setup then downloads a portable compiler (about 300 MB download, 900 MB on disk, first time only), translates the game "
    "on this PC and starts it. Nothing from the ROM leaves your computer.",
    GAME_TITLE " setup", MB_OK | MB_ICONINFORMATION);
  OPENFILENAMEA of; memset(&of, 0, sizeof of); out[0] = 0;
  of.lStructSize = sizeof of; of.lpstrFile = out; of.nMaxFile = MAX_PATH;
  of.lpstrFilter = "Genesis / Mega Drive ROM (*.md;*.bin;*.gen)\0*.md;*.bin;*.gen\0All files\0*.*\0";
  of.lpstrTitle = "Pick your Mega Man: The Wily Wars ROM";
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  return GetOpenFileNameA(&of);
}

static int check_and_copy_rom(const char *rom) {
  FILE *f = fopen(rom, "rb"); if (!f) return fail("Cannot open %s", rom);
  uint8_t *d = malloc(ROM_SIZE + 1); size_t n = d ? fread(d, 1, ROM_SIZE + 1, f) : 0; fclose(f);
  if (!d) return fail("Out of memory");
  uint32_t crc = crc32_buf(d, n);
  say("ROM: %s (%zu bytes, CRC32 %08X)", rom, n, crc);
  if (n != ROM_SIZE || crc != ROM_CRC) {
    free(d);
    return fail("This ROM isn't the supported dump.\n\nExpected: 2,097,152 bytes, CRC32 0CD405DB\n(Mega Man - The Wily Wars (USA) (Mega Drive Mini, Genesis Mini)).\nGot: %zu bytes, CRC32 %08X.", n, crc);
  }
  const char *dst[] = {"src\\game\\mmww.bin", "game\\mmww.bin"};
  for (int i = 0; i < 2; i++) {
    char p[MAX_PATH]; path(p, dst[i]);
    char dir[MAX_PATH]; snprintf(dir, sizeof dir, "%s", p); *strrchr(dir, '\\') = 0; mkdirs(dir);
    FILE *o = fopen(p, "wb"); if (!o || fwrite(d, 1, n, o) != n) { if (o) fclose(o); free(d); return fail("Cannot write %s", p); }
    fclose(o);
  }
  free(d);
  return 0;
}

static int build(void) {
  char tc[MAX_PATH], src[MAX_PATH], bld[MAX_PATH], rc[MAX_PATH], cmd[8192];
  path(tc, "toolchain"); path(src, "src"); path(bld, "build"); path(rc, "tools\\GenesisRecomp.exe");
  char clang[MAX_PATH], clangxx[MAX_PATH], cmake[MAX_PATH], ninja[MAX_PATH], bin[MAX_PATH];
  snprintf(bin, sizeof bin, "%s\\%s", tc, "llvm-mingw\\llvm-mingw-20261006-ucrt-x86_64\\bin");
  snprintf(clang, sizeof clang, "%s\\x86_64-w64-mingw32-clang.exe", bin);
  snprintf(clangxx, sizeof clangxx, "%s\\x86_64-w64-mingw32-clang++.exe", bin);
  snprintf(cmake, sizeof cmake, "%s\\%s", tc, "cmake\\cmake-3.31.8-windows-x86_64\\bin\\cmake.exe");
  snprintf(ninja, sizeof ninja, "%s\\%s", tc, "ninja\\ninja.exe");
  /* CMake wants forward slashes in -D paths ("C:\m..." is an escape there) */
  char rcexe[MAX_PATH]; snprintf(rcexe, sizeof rcexe, "%s\\llvm-rc.exe", bin);
  char *fix[] = {clang, clangxx, ninja, rcexe, rc};
  for (int i = 0; i < 5; i++) for (char *c = fix[i]; *c; c++) if (*c == '\\') *c = '/';
  { /* the compiler finds its own tools (lld, llvm-rc) on PATH */
    char p[32768]; DWORD n = GetEnvironmentVariableA("PATH", p, sizeof p);
    char np[33000]; snprintf(np, sizeof np, "%s;%s", bin, n ? p : "");
    SetEnvironmentVariableA("PATH", np);
  }
  say("build: configuring (CMake) ...");
  snprintf(cmd, sizeof cmd,
    "\"%s\" -S \"%s\" -B \"%s\" -G Ninja -DCMAKE_BUILD_TYPE=Release "
    "\"-DCMAKE_MAKE_PROGRAM=%s\" \"-DCMAKE_C_COMPILER=%s\" \"-DCMAKE_CXX_COMPILER=%s\" "
    "\"-DCMAKE_RC_COMPILER=%s\" \"-DGENESIS_RECOMPILER_EXE=%s\" -DMMWW_LAUNCHER=OFF",
    cmake, src, bld, ninja, clang, clangxx, rcexe, rc);
  if (run(cmd, g_base)) return fail("CMake configure failed. See setup.log beside MMWW-Setup.exe.");
  say("build: translating the ROM to C and compiling (several minutes) ...");
  snprintf(cmd, sizeof cmd, "\"%s\" --build \"%s\"", cmake, bld);
  if (run(cmd, g_base)) return fail("The build failed. See setup.log beside MMWW-Setup.exe.");
  return 0;
}

static int install_game(void) {
  char from[MAX_PATH], to[MAX_PATH];
  const char *files[][2] = {{"build\\WilyWarsGenesisRecomp.exe", "game\\WilyWarsGenesisRecomp.exe"},
                            {"build\\SDL2.dll", "game\\SDL2.dll"},
                            {"build\\icon.png", "game\\icon.png"},
                            {"build\\icon.bmp", "game\\icon.bmp"}};
  path(to, "game"); mkdirs(to);
  for (int i = 0; i < 4; i++) {
    path(from, files[i][0]); path(to, files[i][1]);
    if (!CopyFileA(from, to, FALSE)) return fail("Cannot copy %s", from);
  }
  /* shaders\ beside the game (librashader.dll + slang presets) */
  char sh[MAX_PATH], dst[MAX_PATH], cmd[2048];
  path(sh, "shaders"); path(dst, "game\\shaders");
  if (exists(sh) && !exists(dst)) {
    snprintf(cmd, sizeof cmd, "cmd.exe /c xcopy /e /i /q /y \"%s\" \"%s\"", sh, dst);
    run(cmd, g_base);
  }
  return 0;
}

static int launch(void) {
  if (getenv("MMWW_SETUP_NO_LAUNCH")) return 0;
  char exe[MAX_PATH], dir[MAX_PATH], cmd[MAX_PATH * 2];
  path(exe, "game\\WilyWarsGenesisRecomp.exe"); path(dir, "game");
  snprintf(cmd, sizeof cmd, "\"%s\" --no-launcher mmww.bin", exe);
  STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
    return fail("Cannot start %s", exe);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  return 0;
}

int main(int argc, char **argv) {
  GetModuleFileNameA(NULL, g_base, sizeof g_base);
  *strrchr(g_base, '\\') = 0;
  int rebuild = argc > 1 && !strcmp(argv[1], "--rebuild");
  char game[MAX_PATH]; path(game, "game\\WilyWarsGenesisRecomp.exe");
  if (!rebuild && exists(game)) return launch();

  char logp[MAX_PATH]; path(logp, "setup.log"); g_log = fopen(logp, "w");
  SetConsoleTitleA(GAME_TITLE " setup");
  say("%s setup: building the game on this PC.", GAME_TITLE);
  char rom[MAX_PATH];
  if (!pick_rom(rom)) { say("No ROM picked; nothing to do."); return 1; }
  if (check_and_copy_rom(rom)) return 1;
  if (ensure_toolchain()) return 1;
  if (build()) return 1;
  if (install_game()) return 1;
  say("Done. The game is in game\\. Next time just open MMWW-Setup.exe (or game\\WilyWarsGenesisRecomp.exe).");
  return launch();
}
