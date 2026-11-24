#include <lib/gamepad.h>
#include <winapi/user32.h>
#include <lib/window.h>
#include <SDL3/SDL.h>
#include <cstring>
#if defined(__vita__)
# include <psp2/ctrl.h>
#endif


namespace win32
{

static x86::reg32 s_kbPoll = 3;

static GamepadState s_state;
static Gamepad* s_gp1;

#if defined(__vita__)
static void initVitaCtrl()
{
    static bool s_ctrlInit = false;
    if (!s_ctrlInit)
    {
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
        s_ctrlInit = true;
    }
}

static x86::sreg16 vitaAxis(x86::reg8 value)
{
    if (value == 0)
        return -32768;
    if (value == 255)
        return 32767;
    return (int(value) - 128) * 256;
}

static GamepadState getVitaState()
{
    initVitaCtrl();

    GamepadState result;
    memset(&result, 0, sizeof(result));
    memset(result.hats, 0xff, sizeof(result.hats));

    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
        return result;

    struct ButtonMap { unsigned int mask; x86::reg32 button; };
    static const ButtonMap s_buttons[] = {
        { SCE_CTRL_TRIANGLE, 0 },
        { SCE_CTRL_CIRCLE, 1 },
        { SCE_CTRL_CROSS, 2 },
        { SCE_CTRL_SQUARE, 3 },
        { SCE_CTRL_LTRIGGER, 4 },
        { SCE_CTRL_RTRIGGER, 5 },
        { SCE_CTRL_DOWN, 6 },
        { SCE_CTRL_LEFT, 7 },
        { SCE_CTRL_UP, 8 },
        { SCE_CTRL_RIGHT, 9 },
        { SCE_CTRL_SELECT, 10 },
        { SCE_CTRL_START, 11 },
    };
    for (const ButtonMap& b : s_buttons)
    {
        if (pad.buttons & b.mask)
            result.buttons |= 1ull << b.button;
    }

    result.axes[0] = vitaAxis(pad.lx);
    result.axes[1] = vitaAxis(pad.ly);
    result.axes[2] = vitaAxis(pad.rx);
    result.axes[3] = vitaAxis(pad.ry);

    x86::reg8 pov = 0xff;
    const bool up = (pad.buttons & SCE_CTRL_UP) != 0;
    const bool right = (pad.buttons & SCE_CTRL_RIGHT) != 0;
    const bool down = (pad.buttons & SCE_CTRL_DOWN) != 0;
    const bool left = (pad.buttons & SCE_CTRL_LEFT) != 0;
    if (up && right) pov = 1;
    else if (right && down) pov = 3;
    else if (down && left) pov = 5;
    else if (left && up) pov = 7;
    else if (up) pov = 0;
    else if (right) pov = 2;
    else if (down) pov = 4;
    else if (left) pov = 6;
    result.hats[0] = pov;
    return result;
}
#endif

void Input::poll()
{
    s_kbPoll = 3;
#if defined(__vita__)
    s_state = getVitaState();
    return;
#endif
    SDL_UpdateJoysticks();
}

void Gamepad::updateKeys()
{
#if defined(__vita__)
    /* SDL_UpdateJoysticks() triggers sceHid* calls that are unimplemented in
     * Vita3K and crash the host, so the SDL joystick path is unusable here.
     * Instead poll the pad directly through sceCtrl (which Vita3K emulates
     * correctly) and re-emit each edge as a synthetic SDL key event, reusing
     * the engine's proven keyboard path.
     *
     * Every physical input (12 buttons + 4 left-stick and 4 right-stick
     * directions) is a "source" that maps to one key per game:
     *  - NFS3 keeps the original coarse map (steering/throttle come from the
     *    DirectInput pad, keys are only for menus).
     *  - NFS2 SE has the pad hidden from DirectInput, so everything must be a
     *    key, with one distinct key per action (default NFS2 bindings:
     *    Up/Down accelerate/brake, Left/Right steer, A/Z shift, C camera,
     *    hold B look behind, H horn, Space handbrake, Esc pause). */
    enum Source
    {
        SRC_TRIANGLE, SRC_CIRCLE, SRC_CROSS, SRC_SQUARE, SRC_L, SRC_R,
        SRC_DOWN, SRC_LEFT, SRC_UP, SRC_RIGHT, SRC_SELECT, SRC_START,
        SRC_LS_LEFT, SRC_LS_RIGHT, SRC_LS_UP, SRC_LS_DOWN,
        SRC_RS_LEFT, SRC_RS_RIGHT, SRC_RS_UP, SRC_RS_DOWN,
        SRC_COUNT
    };
    struct KeyDef { SDL_Keycode key; SDL_Scancode sc; };
#define K(k) { SDLK_##k, SDL_SCANCODE_##k }
#define KNONE { 0, SDL_SCANCODE_UNKNOWN }
    static const KeyDef s_mapNFS3[SRC_COUNT] = {
        /* TRIANGLE */ K(SPACE),  /* CIRCLE */ K(ESCAPE), /* CROSS */ K(RETURN),
        /* SQUARE   */ K(SPACE),  /* L      */ K(SPACE),  /* R     */ K(SPACE),
        /* DOWN     */ K(DOWN),   /* LEFT   */ K(LEFT),   /* UP    */ K(UP),
        /* RIGHT    */ K(RIGHT),  /* SELECT */ K(ESCAPE), /* START */ K(RETURN),
        KNONE, KNONE, KNONE, KNONE,
        KNONE, KNONE, KNONE, KNONE,
    };
    static const KeyDef s_mapNFS2[SRC_COUNT] = {
        /* TRIANGLE: change camera  */ { SDLK_C, SDL_SCANCODE_C },
        /* CIRCLE:   back / cancel  */ K(ESCAPE),
        /* CROSS:    confirm        */ K(RETURN),
        /* SQUARE:   handbrake      */ K(SPACE),
        /* L:        brake          */ K(DOWN),
        /* R:        accelerate     */ K(UP),
        /* D-pad                    */ K(DOWN), K(LEFT), K(UP), K(RIGHT),
        /* SELECT:   horn           */ { SDLK_H, SDL_SCANCODE_H },
        /* START:    pause          */ K(ESCAPE),
        /* left stick: steer + throttle/brake (digital) */
        K(LEFT), K(RIGHT), K(UP), K(DOWN),
        /* right stick: left/right = downshift/upshift, down = look behind,
         * up = horn */
        { SDLK_Z, SDL_SCANCODE_Z }, { SDLK_A, SDL_SCANCODE_A },
        { SDLK_H, SDL_SCANCODE_H }, { SDLK_B, SDL_SCANCODE_B },
    };
#undef K
#undef KNONE
    const KeyDef* map = win32::g_isNFS3 ? s_mapNFS3 : s_mapNFS2;

    static uint32_t s_prevSources = 0;
    s_state = getVitaState();

    /* Digital sticks with hysteresis so a stick resting near the threshold
     * doesn't chatter. Vita Y axis: up is negative. */
    auto stick = [&](x86::sreg16 v, uint32_t bitNeg, uint32_t bitPos) -> uint32_t
    {
        uint32_t out = 0;
        const int onT = 18000, offT = 10000;
        out |= (v < (s_prevSources & bitNeg ? -offT : -onT)) ? bitNeg : 0;
        out |= (v > (s_prevSources & bitPos ?  offT :  onT)) ? bitPos : 0;
        return out;
    };
    uint32_t now = uint32_t(s_state.buttons & 0xfffull);
    now |= stick(s_state.axes[0], 1u << SRC_LS_LEFT, 1u << SRC_LS_RIGHT);
    now |= stick(s_state.axes[1], 1u << SRC_LS_UP,   1u << SRC_LS_DOWN);
    now |= stick(s_state.axes[2], 1u << SRC_RS_LEFT, 1u << SRC_RS_RIGHT);
    now |= stick(s_state.axes[3], 1u << SRC_RS_UP,   1u << SRC_RS_DOWN);

    /* A key is held if ANY source mapped to it is held (e.g. D-pad up and
     * left-stick up both -> Up), so releasing one source doesn't release a key
     * another still holds. */
    auto keyHeld = [&](SDL_Keycode key, uint32_t sources) -> bool
    {
        for (int j = 0; j < SRC_COUNT; ++j)
            if ((sources & (1u << j)) && map[j].key == key)
                return true;
        return false;
    };
    for (int i = 0; i < SRC_COUNT; ++i)
    {
        const uint32_t bit = 1u << i;
        if ((now & bit) == (s_prevSources & bit) || !map[i].key)
            continue;
        const bool down = keyHeld(map[i].key, now);
        const bool wasDown = keyHeld(map[i].key, s_prevSources);
        if (down == wasDown)
            continue;
        user32::SetVirtualKeyState(map[i].key, down);
        SDL_Event kevent;
        memset(&kevent, 0, sizeof(kevent));
        kevent.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        kevent.key.key = map[i].key;
        kevent.key.scancode = map[i].sc;
        kevent.key.down = down;
        SDL_PushEvent(&kevent);
    }
    s_prevSources = now;
    return;
#endif
    if (s_gp1)
    {
        SDL_UpdateJoysticks();
        if (s_kbPoll == 0)
        {
            GamepadState state = s_gp1->getState();
            SDL_Event event;
            for (x86::reg32 axis = 0; axis < 10; ++axis)
            {
                if (state.axes[axis] != s_state.axes[axis])
                {
                    event.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
                    event.jaxis.which = 0;
                    event.jaxis.axis = axis;
                    event.jaxis.value = state.axes[axis];
                    SDL_PushEvent(&event);
                }
            }
            for (x86::reg32 button = 0; button < 64; ++button)
            {
                x86::reg64 bMask = 1ll << button;
                if ((s_state.buttons & bMask) != (state.buttons & bMask))
                {
                    event.type = state.buttons & bMask ? SDL_EVENT_JOYSTICK_BUTTON_DOWN : SDL_EVENT_JOYSTICK_BUTTON_UP;
                    event.jbutton.which = 0;
                    event.jbutton.button = button;
                    event.jbutton.down = !!(state.buttons & bMask);
                    SDL_PushEvent(&event);
                }
            }
            s_state = state;
        }
        else
        {
            SDL_Event event;
            for (x86::reg32 button = 0; button < 64; ++button)
            {
                x86::reg64 bMask = 1ll << button;
                if ((s_state.buttons & bMask))
                {
                    event.type = SDL_EVENT_JOYSTICK_BUTTON_UP;
                    event.jbutton.which = 0;
                    event.jbutton.button = button;
                    event.jbutton.down = false;
                    SDL_PushEvent(&event);
                }
            }
            memset(&s_state, 0, sizeof(s_state));
            s_kbPoll--;
        }
    }
}

x86::reg32 Input::getButtonCount() const
{
    return 0;
}

x86::reg32 Input::getAxesCount() const
{
    return 0;
}

Gamepad::Gamepad(x86::reg32 gamepadIndex)
    :   m_joystick(nullptr)
{
#if defined(__vita__)
    /* On Vita3K, SDL_GetJoysticks/SDL_OpenJoystick trigger unimplemented HID
     * calls that crash the host process. The Vita's controller is handled via
     * sceCtrl directly; skip SDL joystick open here. */
    (void)gamepadIndex;
    if (!s_gp1) s_gp1 = this;
    return;
#endif
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    if (ids && (int)gamepadIndex < count)
        m_joystick = SDL_OpenJoystick(ids[gamepadIndex]);
    SDL_free(ids);
    SDL_SetJoystickEventsEnabled(false);
    if (!s_gp1)
        s_gp1 = this;
}

Gamepad::~Gamepad()
{
    /* Only relinquish the active-pad slot if WE are it — otherwise destroying
     * one (e.g. a transient DirectInput device) would silently kill input for
     * another live pad. Matters on Vita, where a boot-time pad coexists with
     * any pad the game itself opens. */
    if (s_gp1 == this)
        s_gp1 = nullptr;
    SDL_CloseJoystick(m_joystick);
}

x86::reg32 Gamepad::getCount()
{
#if defined(__vita__)
    return 1;
#endif
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    SDL_free(ids);
    return (x86::reg32)count;
}

x86::reg32 Gamepad::getButtonCount() const
{
#if defined(__vita__)
    return 12;
#endif
    if (!m_joystick) return 0;
    return SDL_GetNumJoystickButtons(m_joystick);
}

x86::reg32 Gamepad::getAxesCount() const
{
#if defined(__vita__)
    return 4;
#endif
    if (!m_joystick) return 0;
    return SDL_GetNumJoystickAxes(m_joystick);
}

GamepadState Gamepad::getState() const
{
    GamepadState result;
    memset(&result, 0, sizeof(result));
#if defined(__vita__)
    return getVitaState();
#endif
    if (!m_joystick) return result;
    for (x86::reg32 button = 0; button < getButtonCount(); ++button)
    {
        result.buttons |= (SDL_GetJoystickButton(m_joystick, button) ? 1 : 0) << button;
    }
    for (x86::reg32 axis = 0; axis < getAxesCount(); ++axis)
    {
        result.axes[axis] = SDL_GetJoystickAxis(m_joystick, axis);
    }
    return result;
}


}
