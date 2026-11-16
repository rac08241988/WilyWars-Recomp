cmake_minimum_required(VERSION 3.19)
project(WilyWarsGenesisRecomp VERSION 1.0.1 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Mega Man: The Wily Wars (Genesis) — native static recompilation.
# 68000 code -> C via segagenesisrecomp (pinned submodule), with its VDP,
# YM2612 (ymfm), SN76489, Z80 and Tier-3 interpreter floor. The player
# supplies the ROM; generated C lives only in the build tree.
# ---------------------------------------------------------------------------
set(RECOMP_ROOT "${CMAKE_SOURCE_DIR}/segagenesisrecomp")
set(RUNNER_ROOT "${RECOMP_ROOT}/runner")

if(WIN32)
    if(MSVC)
        set(SDL2_DIR "${RUNNER_ROOT}/external/SDL2/SDL2-2.28.5/cmake" CACHE PATH "" FORCE)
        set(SDL2_RUNTIME_DLL "${RUNNER_ROOT}/external/SDL2/SDL2-2.28.5/lib/x64/SDL2.dll")
    else()
        set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2-mingw-2.28.5/x86_64-w64-mingw32/lib/cmake/SDL2" CACHE PATH "" FORCE)
        set(SDL2_RUNTIME_DLL "${CMAKE_SOURCE_DIR}/third_party/SDL2-mingw-2.28.5/x86_64-w64-mingw32/bin/SDL2.dll")
    endif()
    find_package(SDL2 REQUIRED CONFIG NO_DEFAULT_PATH PATHS "${SDL2_DIR}")
else()
    find_package(SDL2 REQUIRED)
endif()

option(GEN_ENABLE_TRACE "TCP debug server + observability rings" OFF)
option(SONIC_REVERSE_DEBUG "Reverse-debug instrumentation (dev only)" OFF)
# Cross builds (llvm-mingw on Linux) run a host-built GenesisRecomp instead.
set(GENESIS_RECOMPILER_EXE "" CACHE FILEPATH "Prebuilt host GenesisRecomp (cross builds)")
if(GENESIS_RECOMPILER_EXE)
    add_executable(GenesisRecomp IMPORTED GLOBAL)
    set_target_properties(GenesisRecomp PROPERTIES IMPORTED_LOCATION "${GENESIS_RECOMPILER_EXE}")
endif()
include("${RECOMP_ROOT}/cmake/GenesisRecompGenerated.cmake")
genesisrecomp_runner_sources(MMWW_RUNNER_SOURCES
    TRACE ${GEN_ENABLE_TRACE} REVERSE_DEBUG ${SONIC_REVERSE_DEBUG})
genesisrecomp_add_generated_sources(MMWW_GENERATED mmww
    "${CMAKE_SOURCE_DIR}/game" mmww.bin REVERSE_DEBUG ${SONIC_REVERSE_DEBUG})

add_executable(WilyWarsGenesisRecomp
    ${MMWW_RUNNER_SOURCES} ${MMWW_GENERATED}
    game/mmww_spec.c game/mmww_eeprom.c game/mmww_video.c
    src/gl/mmww_gl.c src/frontend/ui/rpanel.c src/updater/recomp_updater.c)
add_dependencies(WilyWarsGenesisRecomp ${MMWW_GENERATED_TARGET})
genesisrecomp_runner_target(WilyWarsGenesisRecomp)
target_compile_definitions(WilyWarsGenesisRecomp PRIVATE
    INCLUDE_GENERATED_CODE=1 ENABLE_RECOMPILED_CODE=1 OWN_BACKEND=1
    DEFAULT_DEBUG_PORT=4410
    $<$<BOOL:${GEN_ENABLE_TRACE}>:GEN_ENABLE_TRACE=1>
    $<$<BOOL:${SONIC_REVERSE_DEBUG}>:SONIC_REVERSE_DEBUG=1>
    MMWW_BUILD_VERSION="${PROJECT_VERSION}"
    RECOMP_UPDATER_REPO_OWNER="rac08241988"
    RECOMP_UPDATER_REPO_NAME="WilyWars-Recomp")
target_include_directories(WilyWarsGenesisRecomp PRIVATE
    "${RUNNER_ROOT}" "${RUNNER_ROOT}/include" "${RUNNER_ROOT}/video"
    "${RUNNER_ROOT}/external/ymfm/src" "${RUNNER_ROOT}/external/superzazu"
    "${RUNNER_ROOT}/external/clowncommon" "${RECOMP_ROOT}/recompiler/src"
    "${CMAKE_SOURCE_DIR}/game" "${CMAKE_SOURCE_DIR}/src/gl" "${CMAKE_SOURCE_DIR}/src/frontend" "${CMAKE_SOURCE_DIR}/src/updater"
    "${CMAKE_SOURCE_DIR}/recomp-ui/src/third_party" "${CMAKE_SOURCE_DIR}/vendor/librashader/include")
if(WIN32 AND NOT MSVC)
    target_include_directories(WilyWarsGenesisRecomp PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/SDL2-mingw-2.28.5/x86_64-w64-mingw32/include")
endif()
target_link_libraries(WilyWarsGenesisRecomp PRIVATE SDL2::SDL2)
target_compile_definitions(WilyWarsGenesisRecomp PRIVATE SDL_MAIN_HANDLED)
if(WIN32)
    target_link_libraries(WilyWarsGenesisRecomp PRIVATE ws2_32 comdlg32 winhttp shell32)
    if(NOT MSVC)
        target_link_options(WilyWarsGenesisRecomp PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_custom_command(TARGET WilyWarsGenesisRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:WilyWarsGenesisRecomp>")
endif()

# Icon: USA Genesis cover (assets/icon). Embedded as resource 1 and staged
# beside the exe for the SDL window icon.
if(WIN32)
    enable_language(RC)
    target_sources(WilyWarsGenesisRecomp PRIVATE src/win32/app.rc)
endif()
add_custom_command(TARGET WilyWarsGenesisRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/icon/icon.png" "$<TARGET_FILE_DIR:WilyWarsGenesisRecomp>/icon.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/icon/icon.bmp" "$<TARGET_FILE_DIR:WilyWarsGenesisRecomp>/icon.bmp"
    COMMENT "WilyWarsGenesisRecomp: staging window icon beside the executable"
    VERBATIM)

option(MMWW_LAUNCHER "Build the recomp-ui pre-boot launcher" ON)
if(MMWW_LAUNCHER)
    include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)
    recomp_target_launcher_ui(WilyWarsGenesisRecomp CONSOLE genesis)
endif()
