#include <lib/event.h>
#include <lib/thread.h>
#include <SDL3/SDL.h>
#include <algorithm>

namespace win32
{

static SDL_Mutex* s_mutex = nullptr;
static SDL_Condition* s_condition = nullptr;   // shared CV: multi-object waits + terminate
static std::vector<Event*>* s_allEvents = nullptr;  // registry for broadcastTerminate

static const x86::sreg32 WAIT_TIMEOUT = 0x102;

static unsigned s_eventIndex = 0;

Event::Event(const char *name, bool manualReset, bool initialState)
    :   m_name(name ? name : "")
    ,   m_manualReset(manualReset)
    ,   m_isSet(initialState)
    ,   m_pulse(false)
    ,   m_id(s_eventIndex++)
    ,   m_cond(SDL_CreateCondition())
{
    SDL_LockMutex(s_mutex);
    s_allEvents->push_back(this);
    SDL_UnlockMutex(s_mutex);
}

Event::~Event()
{
    SDL_LockMutex(s_mutex);
    s_allEvents->erase(std::remove(s_allEvents->begin(), s_allEvents->end(), this), s_allEvents->end());
    SDL_UnlockMutex(s_mutex);
    SDL_DestroyCondition(m_cond);
}

void Event::init()
{
    s_mutex = SDL_CreateMutex();
    s_condition = SDL_CreateCondition();
    s_allEvents = new std::vector<Event*>();
}

void Event::fini()
{
    SDL_DestroyCondition(s_condition);
    SDL_DestroyMutex(s_mutex);
    delete s_allEvents;
    s_allEvents = nullptr;
}

void Event::broadcastTerminate()
{
    SDL_LockMutex(s_mutex);
    SDL_BroadcastCondition(s_condition);
    /* Single-object waiters sleep on their own per-event CV, so wake those too. */
    for (Event* e : *s_allEvents)
        SDL_BroadcastCondition(e->m_cond);
    SDL_UnlockMutex(s_mutex);
}

void Event::set()
{
    SDL_LockMutex(s_mutex);
    //SDL_Log("Event set: %s<%u>", m_name.c_str(), m_id);
    m_pulse = false;
    m_isSet = true;
    SDL_BroadcastCondition(m_cond);         // wake this event's single-object waiters
    SDL_BroadcastCondition(s_condition);    // wake any multi-object waiters (rare)
    SDL_UnlockMutex(s_mutex);
}

x86::reg32 Event::pulse()
{
    SDL_LockMutex(s_mutex);
    //SDL_Log("Event pulse: %s<%u>", m_name.c_str(), m_id);
    if (!m_waitingThreads.empty())
    {
        m_pulse = true;
        m_isSet = true;
        SDL_BroadcastCondition(m_cond);
        SDL_BroadcastCondition(s_condition);
    }
    SDL_UnlockMutex(s_mutex);
    return 1;
}

x86::reg32 Event::wait(x86::CPU& cpu, Event** begin, Event** end, bool waitAll, x86::sreg32 timeout)
{
    NFS2_ASSERT(!waitAll);
    SDL_LockMutex(s_mutex);
    x86::reg32 result = WAIT_TIMEOUT;
    for (Event** e = begin; e < end; ++e)
    {
        //SDL_Log("Event wait[%u]: %s<%u>", static_cast<unsigned>(e - begin), (*e)->m_name.c_str(), (*e)->m_id);
        (*e)->m_waitingThreads.push_back(Thread::currentThreadId());
    }
    for (;;)
    {
        for (Event** e = begin; e < end; ++e)
        {
            if ((*e)->m_isSet)
            {
                //SDL_Log("Event wait finished[%u]: %s<%u>", static_cast<unsigned>(e - begin), (*e)->m_name.c_str(), (*e)->m_id);
                if (result == WAIT_TIMEOUT)
                    result = static_cast<x86::reg32>(e - begin);
                if (!(*e)->m_manualReset)
                    (*e)->m_isSet = false;
            }
        }

        if (result != WAIT_TIMEOUT || timeout == 0)
        {
            break;
        }
        if (cpu.terminate)
        {
            result = x86::reg32(-1);
            break;
        }
        /* Single-object wait (the hot path) sleeps on that event's own CV, so a
         * SetEvent on an unrelated event no longer wakes this thread (no more
         * thundering herd). Multi-object waits fall back to the shared CV. */
        SDL_Condition* cv = (end - begin == 1) ? begin[0]->m_cond : s_condition;
        bool event = SDL_WaitConditionTimeout(cv, s_mutex, timeout);
        if (!event)
        {
            break;
        }
    }
    for (Event** e = begin; e < end; ++e)
    {
        (*e)->m_waitingThreads.erase(std::remove((*e)->m_waitingThreads.begin(),
                                                 (*e)->m_waitingThreads.end(),
                                                 Thread::currentThreadId()),
                                     (*e)->m_waitingThreads.end());
        if ((*e)->m_pulse && !(*e)->m_waitingThreads.empty())
            (*e)->m_isSet = false;
    }
    //SDL_Log("Event wait result: %d", result);
    SDL_UnlockMutex(s_mutex);
    return result;
}

}
