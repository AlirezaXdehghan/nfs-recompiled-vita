#ifndef LIB_SOCKET_H_
#define LIB_SOCKET_H_

#include <lib/winapp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace win32
{

/* Network (4), node (6) and socket (2) in the original IPX wire byte order. */
typedef std::array<uint8_t, 12> IPXAddress;

/* Guest-visible IPX datagram sockets.
 *
 * The games speak IPX through Winsock 1. There is no IPX on a modern network,
 * so every IPX packet is wrapped in a small header ("N3LX") and carried in a
 * UDP broadcast on one shared native socket; each process filters what it
 * receives by IPX destination (node, socket) and demultiplexes it to the
 * matching guest socket. No peer discovery or addressing is needed: everyone
 * on the same Wi-Fi segment sees everyone. */
class Socket : public GenericResource
{
public:
    struct Packet
    {
        IPXAddress           source;
        std::vector<uint8_t> bytes;
    };

    struct State : public std::enable_shared_from_this<State>
    {
        int                 descriptor = -1;    /* >= 0 once the transport is up */
        std::atomic<bool>   closed{false};
        bool                bound = false;
        bool                nonblocking = false;
        bool                broadcast = false;
        uint8_t             packetType = 0;
        int                 filterType = -1;
        unsigned            sentPackets = 0, receivedPackets = 0;
        IPXAddress          address{};
        std::deque<Packet>  packets;

        ~State();
        void close();
        int bind(const IPXAddress& requested);
        int send(const void* bytes, int length, const IPXAddress& destination);
        int pump();
    };

    explicit Socket(WinApplication* app);
    ~Socket();
    std::shared_ptr<State> state() const { return m_state; }

    static void init(WinApplication* app);
    static bool fini(WinApplication* app);
    static void cleanup(WinApplication* app);
    static bool initialized(WinApplication* app);
    static x86::reg32 netToA(WinApplication* app, x86::reg32 address);
    static int winsockError(int nativeError);

private:
    std::shared_ptr<State> m_state;
};

}

#endif
