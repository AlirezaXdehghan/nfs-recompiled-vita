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

void Input::poll()
{
    s_kbPoll = 3;
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
    static bool s_ctrlInit = false;
    if (!s_ctrlInit)
    {
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
        s_ctrlInit = true;
    }

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
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
        return;

    for (const PadMap& m : s_map)
    {
        bool now  = (pad.buttons    & m.mask) != 0;
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
    s_prevButtons = pad.buttons;
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
    /* Return 0 so the game uses keyboard-only input path.
     * Vita controller input is synthesized as key events in window.cpp. */
    return 0;
#endif
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    SDL_free(ids);
    return (x86::reg32)count;
}

x86::reg32 Gamepad::getButtonCount() const
{
    if (!m_joystick) return 0;
    return SDL_GetNumJoystickButtons(m_joystick);
}

x86::reg32 Gamepad::getAxesCount() const
{
    if (!m_joystick) return 0;
    return SDL_GetNumJoystickAxes(m_joystick);
}

GamepadState Gamepad::getState() const
{
    GamepadState result;
    memset(&result, 0, sizeof(result));
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
