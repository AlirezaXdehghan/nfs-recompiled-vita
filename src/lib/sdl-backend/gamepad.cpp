#include <lib/gamepad.h>
#include <winapi/user32.h>
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
     * correctly) and re-emit each button edge as a synthetic SDL key event,
     * reusing the engine's proven keyboard path. Cross/Start = Enter (confirm),
     * Circle/Select = Escape (back), D-pad = arrows, everything else = Space. */
    struct PadMap { unsigned int mask; SDL_Keycode key; SDL_Scancode sc; };
    static const PadMap s_map[] = {
        { SCE_CTRL_CROSS,    SDLK_RETURN, SDL_SCANCODE_RETURN },
        { SCE_CTRL_START,    SDLK_RETURN, SDL_SCANCODE_RETURN },
        { SCE_CTRL_CIRCLE,   SDLK_ESCAPE, SDL_SCANCODE_ESCAPE },
        { SCE_CTRL_SELECT,   SDLK_ESCAPE, SDL_SCANCODE_ESCAPE },
        { SCE_CTRL_UP,       SDLK_UP,     SDL_SCANCODE_UP     },
        { SCE_CTRL_DOWN,     SDLK_DOWN,   SDL_SCANCODE_DOWN   },
        { SCE_CTRL_LEFT,     SDLK_LEFT,   SDL_SCANCODE_LEFT   },
        { SCE_CTRL_RIGHT,    SDLK_RIGHT,  SDL_SCANCODE_RIGHT  },
        { SCE_CTRL_TRIANGLE, SDLK_SPACE,  SDL_SCANCODE_SPACE  },
        { SCE_CTRL_SQUARE,   SDLK_SPACE,  SDL_SCANCODE_SPACE  },
        { SCE_CTRL_LTRIGGER, SDLK_SPACE,  SDL_SCANCODE_SPACE  },
        { SCE_CTRL_RTRIGGER, SDLK_SPACE,  SDL_SCANCODE_SPACE  },
    };

    static unsigned int s_prevButtons = 0;
    s_state = getVitaState();

    for (const PadMap& m : s_map)
    {
        bool now  = false;
        switch (m.mask)
        {
        case SCE_CTRL_TRIANGLE: now = (s_state.buttons & (1ull << 0)) != 0; break;
        case SCE_CTRL_CIRCLE:   now = (s_state.buttons & (1ull << 1)) != 0; break;
        case SCE_CTRL_CROSS:    now = (s_state.buttons & (1ull << 2)) != 0; break;
        case SCE_CTRL_SQUARE:   now = (s_state.buttons & (1ull << 3)) != 0; break;
        case SCE_CTRL_LTRIGGER: now = (s_state.buttons & (1ull << 4)) != 0; break;
        case SCE_CTRL_RTRIGGER: now = (s_state.buttons & (1ull << 5)) != 0; break;
        case SCE_CTRL_DOWN:     now = (s_state.buttons & (1ull << 6)) != 0; break;
        case SCE_CTRL_LEFT:     now = (s_state.buttons & (1ull << 7)) != 0; break;
        case SCE_CTRL_UP:       now = (s_state.buttons & (1ull << 8)) != 0; break;
        case SCE_CTRL_RIGHT:    now = (s_state.buttons & (1ull << 9)) != 0; break;
        case SCE_CTRL_SELECT:   now = (s_state.buttons & (1ull << 10)) != 0; break;
        case SCE_CTRL_START:    now = (s_state.buttons & (1ull << 11)) != 0; break;
        }
        bool prev = (s_prevButtons  & m.mask) != 0;
        if (now == prev)
            continue;
        user32::SetVirtualKeyState(m.key, now);
        SDL_Event kevent;
        memset(&kevent, 0, sizeof(kevent));
        kevent.type = now ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        kevent.key.key = m.key;
        kevent.key.scancode = m.sc;
        kevent.key.down = now;
        SDL_PushEvent(&kevent);
    }
    s_prevButtons = 0;
    if (s_state.buttons & (1ull << 0)) s_prevButtons |= SCE_CTRL_TRIANGLE;
    if (s_state.buttons & (1ull << 1)) s_prevButtons |= SCE_CTRL_CIRCLE;
    if (s_state.buttons & (1ull << 2)) s_prevButtons |= SCE_CTRL_CROSS;
    if (s_state.buttons & (1ull << 3)) s_prevButtons |= SCE_CTRL_SQUARE;
    if (s_state.buttons & (1ull << 4)) s_prevButtons |= SCE_CTRL_LTRIGGER;
    if (s_state.buttons & (1ull << 5)) s_prevButtons |= SCE_CTRL_RTRIGGER;
    if (s_state.buttons & (1ull << 6)) s_prevButtons |= SCE_CTRL_DOWN;
    if (s_state.buttons & (1ull << 7)) s_prevButtons |= SCE_CTRL_LEFT;
    if (s_state.buttons & (1ull << 8)) s_prevButtons |= SCE_CTRL_UP;
    if (s_state.buttons & (1ull << 9)) s_prevButtons |= SCE_CTRL_RIGHT;
    if (s_state.buttons & (1ull << 10)) s_prevButtons |= SCE_CTRL_SELECT;
    if (s_state.buttons & (1ull << 11)) s_prevButtons |= SCE_CTRL_START;
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
