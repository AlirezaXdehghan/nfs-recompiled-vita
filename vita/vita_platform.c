/* vita_platform.c — one-time PS Vita startup tuning.
 *
 * Runs before main() via a constructor, so no engine entry point has to call
 * it. The static recompilation is CPU-bound (it re-executes the original game
 * logic as native ARM), so we clock the device to its maximum, matching the
 * re3-vita approach.
 */
#include <psp2/power.h>

__attribute__((constructor))
static void vita_platform_init(void)
{
    /* Max out the SoC: 444 MHz CPU, 222 MHz bus, 222 MHz GPU core,
     * 166 MHz GPU crossbar — the standard "give me everything" homebrew clocks
     * (same set re3-vita pins at boot). */
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
}
