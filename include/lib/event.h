#ifndef LIB_EVENT_H_
#define LIB_EVENT_H_
#include <lib/winapp.h>
#include <string>
#include <vector>

struct SDL_Condition;

namespace win32
{

class Event : public GenericResource
{
public:
    Event(const char* name, bool manualReset, bool initialState);
    ~Event();

    void set();
    const std::string& getName() const { return m_name; }
    x86::reg32 pulse();
    static x86::reg32 wait(x86::CPU& cpu, Event** begin, Event** end, bool waitAll, x86::sreg32 timeout);
    static void broadcastTerminate();

    static void init();
    static void fini();

private:
    std::string             m_name;
    std::vector<x86::reg32> m_waitingThreads;
    bool                    m_manualReset;
    bool                    m_isSet;
    bool                    m_pulse;
    unsigned                m_id;
    SDL_Condition*          m_cond;   // per-event CV: a signal wakes only its own waiters
};

}

#endif /*!EVENT_H_*/
