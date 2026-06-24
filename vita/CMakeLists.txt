cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Toolchain bootstrap (vitasdk)
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.cmake" CACHE PATH "toolchain file")
    message(STATUS "Using VITASDK toolchain: ${CMAKE_TOOLCHAIN_FILE}")
  else()
    message(FATAL_ERROR "Define the VITASDK env var or pass -DCMAKE_TOOLCHAIN_FILE=<vitasdk>/share/vita.cmake")
  endif()
endif()

project(nfs_recompiled_vita LANGUAGES C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)
include(FetchContent)

# The engine source tree lives one level up from vita/.
set(NFS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SRC      "${NFS_ROOT}/src")

# ---------------------------------------------------------------------------
# sse2neon — provides the SSE/SSE2 intrinsic surface (__m128i, _mm_*) used by
# include/mmx.h on top of ARM NEON. Single header, fetched onto the include
# path. (mmx.h includes <sse2neon.h> on non-x86 hosts.)
# ---------------------------------------------------------------------------
FetchContent_Declare(sse2neon
  GIT_REPOSITORY https://github.com/DLTcollab/sse2neon.git
  GIT_TAG        master)
FetchContent_MakeAvailable(sse2neon)
set(SSE2NEON_INCLUDE_DIR "${sse2neon_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Core runtime library (mirrors the desktop CMake's nfs_core source set)
# ---------------------------------------------------------------------------
file(GLOB LIB_SOURCES
  "${SRC}/lib/*.cpp"
  "${SRC}/lib/sdl-backend/*.cpp"
  "${SRC}/lib/winapi/*.cpp"
  "${SRC}/lib/winapi/ddraw/*.cpp"
  "${SRC}/lib/winapi/dinput/*.cpp"
  "${SRC}/lib/winapi/dsound/*.cpp")
# x87.asm is x86 NASM (WITH_PEDANTIC_FPU only) — never built on Vita.

# Vita glue: heap sizing + boot clocks.
list(APPEND LIB_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/vita_runtime.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/vita_platform.c")

add_library(nfs_core STATIC ${LIB_SOURCES})
target_include_directories(nfs_core PRIVATE
  "${NFS_ROOT}/include"
  "${SSE2NEON_INCLUDE_DIR}")
target_compile_features(nfs_core PRIVATE cxx_std_17)
target_compile_definitions(nfs_core PRIVATE WITH_MMX)

# The recompiled code makes heavy use of type-punning through the simulated
# memory buffer; the desktop build's -Wstrict-aliasing=2/-Werror is too strict
# for a port bring-up, so we relax it (and never promote warnings to errors).
# -ffunction-sections/-fdata-sections (+ --gc-sections at link, below) lets the
# linker drop unreferenced translated stubs, shrinking the (huge) image so it
# fits the stock memory budget. Flip CODE_SIZE_OPT=ON to trade speed for an even
# smaller image (-Os) if NFS3 won't fit.
option(CODE_SIZE_OPT "Optimise the recompiled image for size (-Os) instead of speed (-O2)" OFF)
if(CODE_SIZE_OPT)
  set(NFS_OPT -Os)
else()
  set(NFS_OPT -O2)
endif()
set(NFS_COMMON_FLAGS
  ${NFS_OPT} -ffunction-sections -fdata-sections
  -fno-strict-aliasing -Wno-narrowing -Wno-write-strings -fsigned-char)

target_compile_options(nfs_core PRIVATE ${NFS_COMMON_FLAGS})

# ---------------------------------------------------------------------------
# Vita system / SDL3 / vitaGL libraries
# ---------------------------------------------------------------------------
#  SDL3 must be built and installed into $VITASDK for arm-vita-eabi (vitasdk
#  ships SDL2 by default — SDL3 has an upstream Vita backend; build it from the
#  SDL repo with the vita toolchain, see README_VITA.md §Toolchain).
#  vitaGL + vitaShaRK supply the GL path the Glide renderer draws through.
set(VITA_LIBS
  SDL3
  vitaGL vitashark SceShaccCgExt SceShaccCg_stub
  taihen_stub mathneon
  # The engine uses raw SDL3 audio (SDL_OpenAudioDeviceStream) and the game's
  # own recompiled decoders — no SDL3_mixer / vorbis needed.
  m
  # Sce stubs
  SceLibKernel_stub SceAppMgr_stub SceAppUtil_stub
  SceCommonDialog_stub SceDisplay_stub SceGxm_stub
  SceCtrl_stub SceTouch_stub SceMotion_stub SceHid_stub
  SceAudio_stub SceAudioIn_stub
  SceSysmodule_stub SceProcessmgr_stub
  ScePower_stub SceRtc_stub SceKernelDmacMgr_stub SceCamera_stub
  SceNet_stub SceNetCtl_stub
  SceFios2_stub SceIofilemgr_stub
  pthread)

# ---------------------------------------------------------------------------
# Per-game executable + .vpk
# ---------------------------------------------------------------------------
function(add_vita_game name titleid appname)
  file(GLOB GAME_SOURCES   "${SRC}/${name}/*.cpp")
  file(GLOB DISASM_SOURCES "${SRC}/${name}/disassembly/*.cpp")

  add_executable(${name} ${GAME_SOURCES} ${DISASM_SOURCES})
  target_include_directories(${name} PRIVATE
    "${SRC}/${name}"
    "${SRC}/${name}/disassembly"
    "${NFS_ROOT}/include"
    "${SSE2NEON_INCLUDE_DIR}")
  target_compile_features(${name} PRIVATE cxx_std_17)
  target_compile_definitions(${name} PRIVATE WITH_MMX)
  target_compile_options(${name} PRIVATE ${NFS_COMMON_FLAGS})
  # Drop unreferenced sections to shrink the recompiled image.
  target_link_options(${name} PRIVATE -Wl,--gc-sections)
  target_link_libraries(${name} PRIVATE nfs_core ${VITA_LIBS})

  # NOTE: we intentionally do NOT pass ATTRIBUTE2=12 (the extended memory
  # budget). Staying on the stock budget makes the kernel hard-cap this process
  # well under 300 MB — see the arithmetic in vita_runtime.c. Overflow is then
  # impossible by construction; the only failure mode is "image too big to fit",
  # which is fixed with CODE_SIZE_OPT=ON, never by widening the budget.
  vita_create_self(${name}.bin ${name} UNSAFE)

  # Package whatever sce_sys art is present (build still succeeds with none).
  set(VPK_FILES "")
  foreach(asset
      sce_sys/icon0.png
      sce_sys/livearea/contents/bg.png
      sce_sys/livearea/contents/startup.png
      sce_sys/livearea/contents/template.xml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
      list(APPEND VPK_FILES FILE "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "${asset}")
    endif()
  endforeach()

  vita_create_vpk(${name}.vpk ${titleid} ${name}.bin
    VERSION 01.00
    NAME    "${appname}"
    ${VPK_FILES})
endfunction()

add_vita_game(nfs3hp NFS3HP001 "Need for Speed III: Hot Pursuit")
add_vita_game(nfs2se NFS2SE001 "Need for Speed II: Special Edition")
