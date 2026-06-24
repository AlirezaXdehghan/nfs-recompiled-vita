#include <winapi/winmm.h>
#include <lib/timer.h>
#include <lib/gamepad.h>
#include <x86.h>
#include <SDL3/SDL.h>
#include <cstring>

namespace win32 { namespace winmm
{

UINT joyGetNumDevs(WinApplication* app, x86::CPU& cpu)
{
    NFS2_USE(app);
    NFS2_USE(cpu);
    SDL_Log("[winmm] joyGetNumDevs -> %u", unsigned(Gamepad::getCount()));
    return Gamepad::getCount();
}

MMRESULT timeBeginPeriod(WinApplication* app, x86::CPU& cpu,
                         UINT uPeriod)
{
    NFS2_USE(app);
    NFS2_USE(cpu);
    NFS2_USE(uPeriod);
    SDL_Log("[winmm] timeBeginPeriod %u", unsigned(uPeriod));
    return 0;
}

MMRESULT timeEndPeriod(WinApplication* app, x86::CPU& cpu,
                       UINT uPeriod)
{
    NFS2_USE(app);
    NFS2_USE(cpu);
    NFS2_USE(uPeriod);
    SDL_Log("[winmm] timeEndPeriod %u", unsigned(uPeriod));
    return 0;
}

MMRESULT timeGetDevCaps(WinApplication* app, x86::CPU& cpu,
                        LPTIMECAPS ptc, UINT cbtc)
{
    NFS2_USE(app);
    NFS2_USE(cpu);
    NFS2_ASSERT(cbtc == sizeof(*ptc));
    SDL_Log("[winmm] timeGetDevCaps size=%u", unsigned(cbtc));
    ptc->wPeriodMin = 1;
    ptc->wPeriodMax = 1000;
    return 0;
}

MMRESULT timeKillEvent(WinApplication* app, x86::CPU& cpu,
                       UINT uTimerID)
{
    NFS2_USE(cpu);
    SDL_Log("[winmm] timeKillEvent id=%u", unsigned(uTimerID));
    win32::Timer* timer = dynamic_cast<win32::Timer*>(app->getResource(uTimerID));
    if (timer)
    {
        timer->cancel();
        app->freeResource(uTimerID);
    }
    return 0;
}

MMRESULT timeSetEvent(WinApplication* app, x86::CPU& cpu,
                      UINT uDelay, UINT uResolution, LPTIMECALLBACK lpTimeProc,
                      x86::reg32 dwUser, UINT fuEvent)
{
    NFS2_USE(cpu);
    NFS2_ASSERT(fuEvent == 0);
    NFS2_USE(uResolution);
#if defined(__vita__)
    if (strstr(app->getAppNameRaw(), "nfs2se") && uDelay < 16)
        uDelay = 16;
#endif
    if (!cpu.terminate)
    {
        win32::Timer* timer = new win32::Timer(uDelay, app, lpTimeProc, dwUser);
        return app->allocateResource(timer);
    }
    else
        return 0;
}

}}
