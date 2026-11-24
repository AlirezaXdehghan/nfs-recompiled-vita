#include <lib/socket.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#if defined(__vita__)
# include <psp2/net/net.h>
# include <psp2/net/netctl.h>
# include <psp2/sysmodule.h>
#endif

namespace win32
{
namespace
{

/* UDP port every instance listens on and broadcasts to. */
const uint16_t kUdpPort = 28901;
const size_t   kHeaderSize = 32;
const int      kMaxPayload = 1600;
const size_t   kMaxQueued = 256;

struct Session
{
    unsigned references = 0;
    std::unique_ptr<MemMap> text;
    std::vector<std::weak_ptr<Socket::State> > sockets;
};
std::map<WinApplication*, Session> sessions;

/* Every bound guest socket in the process, for demultiplexing. */
std::vector<std::weak_ptr<Socket::State> > boundSockets;

/* --- native transport: one shared non-blocking broadcast UDP socket ------- */
struct Transport
{
    bool started = false;
    bool ok = false;
#if defined(__vita__)
    int sock = -1;
    SceNetSockaddrIn broadcastTo;
#endif

    bool ensure()
    {
        if (started)
            return ok;
        started = true;
#if defined(__vita__)
        sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
        const int netMemory = 512 * 1024;
        SceNetInitParam init;
        init.memory = malloc(netMemory);
        init.size = netMemory;
        init.flags = 0;
        int r = sceNetInit(&init);
        if (r < 0)
            SDL_Log("[lan] sceNetInit returned 0x%08x (maybe already initialised)", unsigned(r));
        sceNetCtlInit();

        sock = sceNetSocket("nfs-ipx", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
        if (sock < 0)
        {
            SDL_Log("[lan] sceNetSocket failed 0x%08x", unsigned(sock));
            return ok = false;
        }
        int one = 1;
        sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &one, sizeof(one));
        sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &one, sizeof(one));
        sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &one, sizeof(one));

        SceNetSockaddrIn local;
        memset(&local, 0, sizeof(local));
        local.sin_len = sizeof(local);
        local.sin_family = SCE_NET_AF_INET;
        local.sin_addr.s_addr = 0;
        local.sin_port = sceNetHtons(kUdpPort);
        r = sceNetBind(sock, reinterpret_cast<SceNetSockaddr*>(&local), sizeof(local));
        if (r < 0)
        {
            SDL_Log("[lan] sceNetBind failed 0x%08x", unsigned(r));
            sceNetSocketClose(sock);
            sock = -1;
            return ok = false;
        }

        /* Directed broadcast (ip | ~netmask); fall back to the limited one. */
        memset(&broadcastTo, 0, sizeof(broadcastTo));
        broadcastTo.sin_len = sizeof(broadcastTo);
        broadcastTo.sin_family = SCE_NET_AF_INET;
        broadcastTo.sin_port = sceNetHtons(kUdpPort);
        broadcastTo.sin_addr.s_addr = 0xffffffffu;
        SceNetCtlInfo ip, mask;
        SceNetInAddr ipAddr, maskAddr;
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &ip) >= 0
            && sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK, &mask) >= 0
            && sceNetInetPton(SCE_NET_AF_INET, ip.ip_address, &ipAddr) > 0
            && sceNetInetPton(SCE_NET_AF_INET, mask.netmask, &maskAddr) > 0)
        {
            broadcastTo.sin_addr.s_addr = ipAddr.s_addr | ~maskAddr.s_addr;
            SDL_Log("[lan] up: ip=%s mask=%s udp=%u", ip.ip_address, mask.netmask, unsigned(kUdpPort));
        }
        else
        {
            SDL_Log("[lan] no Wi-Fi address yet; using limited broadcast");
        }
        return ok = true;
#else
        return ok = false;
#endif
    }

    bool send(const void* bytes, size_t length)
    {
#if defined(__vita__)
        if (!ok)
            return false;
        return sceNetSendto(sock, bytes, length, 0,
                            reinterpret_cast<const SceNetSockaddr*>(&broadcastTo),
                            sizeof(broadcastTo)) >= 0;
#else
        (void)bytes; (void)length;
        return false;
#endif
    }

    /* Returns the datagram size, or -1 if nothing is waiting. */
    int receive(void* buffer, size_t size)
    {
#if defined(__vita__)
        if (!ok)
            return -1;
        int n = sceNetRecvfrom(sock, buffer, size, 0, nullptr, nullptr);
        return n < 0 ? -1 : n;
#else
        (void)buffer; (void)size;
        return -1;
#endif
    }
};
Transport transport;

/* A virtual IPX node identifies this device. The Wi-Fi MAC is stable and unique
 * on the LAN; the locally-administered bit keeps it from looking like a vendor
 * address. */
const std::array<uint8_t, 6>& localNode()
{
    static const std::array<uint8_t, 6> node = []
    {
        std::array<uint8_t, 6> value;
        value.fill(0);
        bool got = false;
#if defined(__vita__)
        SceNetEtherAddr mac;
        if (sceNetGetMacAddress(&mac, 0) >= 0)
        {
            memcpy(value.data(), mac.data, 6);
            got = true;
        }
#endif
        if (!got)
        {
            uint64_t t = SDL_GetPerformanceCounter() ^ (SDL_GetTicksNS() << 7);
            for (size_t i = 0; i < value.size(); ++i)
                value[i] = uint8_t(((t >> (i * 8)) * 2654435761u) >> 24);
        }
        value[0] = (value[0] | 2) & ~1;
        return value;
    }();
    return node;
}

bool broadcastNode(const IPXAddress& address)
{
    return std::all_of(address.begin() + 4, address.begin() + 10,
                       [](uint8_t byte) { return byte == 255; });
}

bool matchesNetwork(const IPXAddress& destination, const IPXAddress& local)
{
    /* The original lobby sends discovery to FFFFFFFF:FFFFFFFFFFFF:0452. Its
     * all-FF network is a wildcard on our single virtual LAN. */
    return std::all_of(destination.begin(), destination.begin() + 4, [](uint8_t b) { return b == 0; })
        || (broadcastNode(destination)
            && std::all_of(destination.begin(), destination.begin() + 4, [](uint8_t b) { return b == 255; }))
        || std::equal(destination.begin(), destination.begin() + 4, local.begin());
}

unsigned guestPort(const IPXAddress& a) { return (a[10] << 8) | a[11]; }

/* Read everything waiting on the native socket and hand each datagram to the
 * guest sockets it is addressed to. */
void pumpTransport()
{
    static uint8_t wire[kHeaderSize + kMaxPayload + 64];
    for (unsigned iteration = 0; iteration < 256; ++iteration)
    {
        const int count = transport.receive(wire, sizeof(wire));
        if (count < 0)
            return;
        if (count < int(kHeaderSize) || memcmp(wire, "N3LX", 4) || wire[4] != 1)
            continue;
        const unsigned length = (wire[6] << 8) | wire[7];
        if (length > unsigned(kMaxPayload) || count != int(kHeaderSize + length))
            continue;
        IPXAddress destination, source;
        memcpy(destination.data(), wire + 8, 12);
        memcpy(source.data(), wire + 20, 12);
        if (memcmp(source.data() + 4, localNode().data(), 6) == 0)
            continue; /* our own broadcast */

        for (size_t i = 0; i < boundSockets.size(); ++i)
        {
            std::shared_ptr<Socket::State> state = boundSockets[i].lock();
            if (!state || state->closed || !state->bound)
                continue;
            if (memcmp(destination.data() + 10, state->address.data() + 10, 2))
                continue;
            if (!broadcastNode(destination) && memcmp(destination.data() + 4, state->address.data() + 4, 6))
                continue;
            if (!matchesNetwork(destination, state->address))
                continue;
            if (state->filterType >= 0 && wire[5] != state->filterType)
                continue;
            if (state->packets.size() >= kMaxQueued)
                continue;
            Socket::Packet packet;
            packet.source = source;
            packet.bytes.assign(wire + kHeaderSize, wire + count);
            state->packets.push_back(packet);
            if (state->receivedPackets++ < 8)
                SDL_Log("[lan] receive guest=%u source=%u bytes=%u",
                        guestPort(state->address), guestPort(source), length);
        }
    }
}

void compactBound()
{
    boundSockets.erase(std::remove_if(boundSockets.begin(), boundSockets.end(),
        [](const std::weak_ptr<Socket::State>& w)
        {
            std::shared_ptr<Socket::State> s = w.lock();
            return !s || s->closed;
        }), boundSockets.end());
}

}

Socket::Socket(WinApplication* app)
    :   m_state(std::make_shared<State>())
{
    if (transport.ensure())
    {
        m_state->descriptor = 0;
    }
    else
    {
        m_state->descriptor = -1;
        errno = ENETDOWN;
    }
    std::vector<std::weak_ptr<State> >& sockets = sessions[app].sockets;
    sockets.erase(std::remove_if(sockets.begin(), sockets.end(),
        [](const std::weak_ptr<State>& item) { return item.expired(); }), sockets.end());
    sockets.push_back(m_state);
}

Socket::~Socket()
{
    m_state->close();
}

Socket::State::~State()
{
}

void Socket::State::close()
{
    if (closed.exchange(true))
        return;
    if (bound)
        SDL_Log("[lan] IPX socket closed; guest port=%u sent=%u received=%u",
                guestPort(address), sentPackets, receivedPackets);
    packets.clear();
}

int Socket::State::bind(const IPXAddress& requested)
{
    if (bound)
        return 10022;
    /* Only the unspecified network, and our own (or unspecified) node. */
    if (std::any_of(requested.begin(), requested.begin() + 4, [](uint8_t b) { return b != 0; })
        || (!std::all_of(requested.begin() + 4, requested.begin() + 10, [](uint8_t b) { return b == 0; })
            && !std::equal(localNode().begin(), localNode().end(), requested.begin() + 4)))
        return 10049;

    IPXAddress wanted = requested;
    std::copy(localNode().begin(), localNode().end(), wanted.begin() + 4);

    compactBound();
    if (!wanted[10] && !wanted[11])
    {
        /* Dynamic socket: pick a free guest port. */
        static unsigned next = 0x4000;
        for (unsigned tries = 0; tries < 0x4000; ++tries)
        {
            const unsigned port = next;
            next = next >= 0x7fff ? 0x4000 : next + 1;
            bool used = false;
            for (size_t i = 0; i < boundSockets.size() && !used; ++i)
            {
                std::shared_ptr<State> s = boundSockets[i].lock();
                used = s && s->bound && guestPort(s->address) == port;
            }
            if (!used)
            {
                wanted[10] = uint8_t(port >> 8);
                wanted[11] = uint8_t(port);
                break;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < boundSockets.size(); ++i)
        {
            std::shared_ptr<State> s = boundSockets[i].lock();
            if (s && s.get() != this && s->bound && guestPort(s->address) == guestPort(wanted))
                return 10048; /* WSAEADDRINUSE */
        }
    }
    address = wanted;
    bound = true;
    boundSockets.push_back(shared_from_this());
    SDL_Log("[lan] bound IPX guest port=%u", guestPort(address));
    return 0;
}

int Socket::State::send(const void* bytes, int length, const IPXAddress& destination)
{
    if (length > kMaxPayload)
        return 10040;
    if (!bound)
        return 10022;
    const bool all = broadcastNode(destination);
    if (all && !broadcast)
        return 10013;
    if (descriptor < 0)
        return 10050;

    std::vector<uint8_t> wire(kHeaderSize + length);
    memcpy(wire.data(), "N3LX", 4);
    wire[4] = 1;
    wire[5] = packetType;
    wire[6] = uint8_t(length >> 8);
    wire[7] = uint8_t(length);
    memcpy(wire.data() + 8, destination.data(), 12);
    memcpy(wire.data() + 20, address.data(), 12);
    if (length)
        memcpy(wire.data() + kHeaderSize, bytes, length);

    const int error = transport.send(wire.data(), wire.size()) ? 0 : 10051;
    if (sentPackets++ < 8)
        SDL_Log("[lan] send guest=%u destination=%u broadcast=%d bytes=%d error=%d type=%u",
                guestPort(address), guestPort(destination), int(all), length, error, packetType);
    return error;
}

int Socket::State::pump()
{
    if (closed)
        return 10038;
    pumpTransport();
    return 0;
}

void Socket::init(WinApplication* app)
{
    Session& session = sessions[app];
    if (!session.references++)
        session.text.reset(new MemMap(4096));
}

bool Socket::initialized(WinApplication* app)
{
    std::map<WinApplication*, Session>::iterator found = sessions.find(app);
    return found != sessions.end() && found->second.references;
}

bool Socket::fini(WinApplication* app)
{
    if (!initialized(app))
        return false;
    if (!--sessions[app].references)
        cleanup(app);
    return true;
}

void Socket::cleanup(WinApplication* app)
{
    std::map<WinApplication*, Session>::iterator found = sessions.find(app);
    if (found == sessions.end())
        return;
    for (size_t i = 0; i < found->second.sockets.size(); ++i)
        if (std::shared_ptr<State> socket = found->second.sockets[i].lock())
            socket->close();
    sessions.erase(found);
    compactBound();
}

x86::reg32 Socket::netToA(WinApplication* app, x86::reg32 value)
{
    std::unique_ptr<MemMap>& text = sessions.at(app).text;
    char* buffer = &app->getMemory<char>(text->getBlockStart());
    snprintf(buffer, 16, "%u.%u.%u.%u", value & 255, (value >> 8) & 255, (value >> 16) & 255, value >> 24);
    return text->getBlockStart();
}

int Socket::winsockError(int error)
{
    switch (error)
    {
    case 0: return 0;
    case EINTR: return 10004;
    case EACCES: case EPERM: return 10013;
    case EFAULT: return 10014;
    case EINVAL: return 10022;
    case EMFILE: case ENFILE: return 10024;
    case EAGAIN: return 10035;
    case EBADF: case ENOTSOCK: return 10038;
    case EMSGSIZE: return 10040;
    case EOPNOTSUPP: return 10045;
    case EAFNOSUPPORT: return 10047;
    case EADDRINUSE: return 10048;
    case EADDRNOTAVAIL: return 10049;
    case ENETDOWN: return 10050;
    case ENETUNREACH: return 10051;
    case ENOBUFS: case ENOMEM: return 10055;
    case ETIMEDOUT: return 10060;
    case ECONNREFUSED: return 10061;
    case EHOSTUNREACH: return 10065;
    default: return 10022;
    }
}

}
