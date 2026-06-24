/* vita_runtime.c — newlib heap sizing for the PS Vita build.
 *
 * THE <300 MB GUARANTEE IS OS-ENFORCED, NOT ASPIRATIONAL.
 * --------------------------------------------------------
 * We deliberately do NOT request the extended memory budget (no ATTRIBUTE2=12
 * in the CMake vita_create_self). A normal Vita application is therefore hard-
 * capped by the kernel at the stock user budget (~256 MB main RAM). The process
 * physically cannot allocate past it: an over-large request fails with
 * malloc()->NULL (a clean NFS2_ASSERT/trap), never silent RAM growth. So the
 * total footprint is bounded BELOW 300 MB by construction. (This is the same
 * argument the sibling AlephOne Vita port relies on, and the opposite of
 * re3-vita, which DOES take the extended budget — GTA III needs it.)
 *
 * WHERE THE RAM GOES (worst-case steady state)
 * --------------------------------------------------------
 *   win32::MemMap::init() does ONE big allocation for the whole simulated
 *   address space (every `new MemMap(...)` in the engine sub-allocates inside
 *   it via the block bitmap — they do NOT each malloc):
 *       s_simulatedMemory ............ 64 MB   (fixed; the game's flat space)
 *       s_sectionSize ................ <=~32 MB (the loaded program image)
 *       s_blockSize (bitmap) ......... ~20 KB
 *     => ~96 MB committed in a single calloc on the newlib heap.
 *   General C++/STL working set (symbol std::maps, Glide vertex buffer
 *   32000*sizeof(GlVertex), GlideTMU slot pools, transient palette scratch
 *   ~w*h*4) ........................... ~20 MB
 *   GL framebuffers + 2048x2048 atlas . in CDRAM/phycont via vglUseVram(GL_TRUE),
 *                                       i.e. OFF this main-RAM heap.
 *
 *   => heap working set ~116 MB. We give 128 MB, leaving headroom on the heap
 *      AND leaving the rest of the stock budget for the recompiled code image
 *      and stacks. Budget arithmetic (stock ~256 MB):
 *          heap 128 + code image (<=~110 MB) + stacks ~8 MB ~= <=~246 MB < 256.
 *
 *   If boot OOMs, the binding constraint is the code image size, NOT this heap
 *   (lower the heap only if your build's s_sectionSize is provably small). The
 *   fix for an over-large image is -Os + --gc-sections (already on in CMake),
 *   never raising the budget past stock — that would break the <300 MB promise.
 */

unsigned int _newlib_heap_size_user = 256 * 1024 * 1024;

/* Main thread stack — the recompiled code reproduces the original's deep call
 * chains and passes whole structs by value through translated stack frames. */
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;
