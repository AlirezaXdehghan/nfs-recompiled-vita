#include <winapi/wsock32.h>
#include <lib/socket.h>
#include <SDL3/SDL.h>
#include <cerrno>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vector>

namespace win32::wsock32
{
namespace
{
thread_local int socketError = 0;
int fail(int error) { socketError = error; return -1; }
std::shared_ptr<Socket::State> stateFor(WinApplication* app, SOCKET handle)
{
    if (!Socket::initialized(app)) { socketError = 10093; return {}; }
    auto socket = dynamic_cast<Socket*>(app->getResource(handle));
    if (!socket || socket->state()->closed) { socketError = 10038; return {}; }
    return socket->state();
}
bool readAddress(const sockaddr* name, int length, IPXAddress& address)
{
    if (!name || length < 14) { socketError = 10014; return false; }
    if (name->sa_family != 6) { socketError = 10047; return false; }
    memcpy(address.data(), name->sa_data, address.size());
    return true;
}
bool writeAddress(const IPXAddress& address, sockaddr* name, int* length)
{
    if (!name || !length || *length < 14) { socketError = 10014; return false; }
    name->sa_family = 6;
    memcpy(name->sa_data, address.data(), address.size());
    *length = 14;
    return true;
}
int waitNative(WinApplication* app, x86::CPU& cpu, int milliseconds)
{
    // The transport is polled (non-blocking). Release the guest execution lock
    // while sleeping so the UI, the send path and closesocket can keep running.
    app->unlockContext(cpu);
    SDL_Delay(Uint32(milliseconds));
    app->lockContext(cpu);
    return 0;
}
int unsupported(WinApplication* app, SOCKET handle)
{
    return stateFor(app, handle) ? fail(10045) : -1;
}
}

int select(WinApplication* app, x86::CPU& cpu, int, fd_set* readfds,
           fd_set* writefds, fd_set* exceptfds, const timeval* timeout)
{
    if (!Socket::initialized(app)) return fail(10093);
    if ((!readfds || !readfds->fd_count) && (!writefds || !writefds->fd_count) && (!exceptfds || !exceptfds->fd_count)) return fail(10022);
    if (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000)) return fail(10022);
    struct Entry { SOCKET handle; std::shared_ptr<Socket::State> state; };
    std::vector<Entry> reads, writes, exceptions;
    auto capture = [&](fd_set* set, std::vector<Entry>& entries) {
        if (!set) return true;
        if (set->fd_count > FD_SETSIZE_WSA) { socketError = 10022; return false; }
        for (unsigned i = 0; i < set->fd_count; ++i)
        {
            auto state = stateFor(app, set->fd_array[i]);
            if (!state) return false;
            entries.push_back({set->fd_array[i], state});
        }
        return true;
    };
    if (!capture(readfds, reads) || !capture(writefds, writes) || !capture(exceptfds, exceptions)) return -1;
    const auto deadline = timeout ? std::chrono::steady_clock::now() + std::chrono::seconds(timeout->tv_sec) +
        std::chrono::microseconds(timeout->tv_usec) : std::chrono::steady_clock::time_point::max();
    for (;;)
    {
        if (cpu.terminate) return fail(10004);
        for (auto* entries : {&reads, &writes, &exceptions})
            for (const auto& entry : *entries) if (entry.state->closed) return fail(10038);
        fd_set readyRead{}, readyWrite{};
        for (const auto& entry : reads)
        {
            if (int error = entry.state->pump()) return fail(error);
            if (!entry.state->packets.empty()) readyRead.fd_array[readyRead.fd_count++] = entry.handle;
        }
        // Datagram sends never wait for an IPX connection.
        for (const auto& entry : writes) readyWrite.fd_array[readyWrite.fd_count++] = entry.handle;
        unsigned ready = readyRead.fd_count + readyWrite.fd_count;
        if (ready || std::chrono::steady_clock::now() >= deadline)
        {
            if (readfds) *readfds = readyRead;
            if (writefds) *writefds = readyWrite;
            if (exceptfds) *exceptfds = {};
            return int(ready);
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        int wait = timeout ? int(std::clamp<int64_t>(remaining, 1, 4)) : 4;
        waitNative(app, cpu, wait);
    }
}
int sendto(WinApplication* app, x86::CPU&, SOCKET handle, const char* bytes, int length,
           int flags, const sockaddr* destination, int destinationLength)
{
    auto state = stateFor(app, handle);
    if (!state) return -1;
    if (length < 0) return fail(10022);
    if (length && !bytes) return fail(10014);
    if (flags) return fail(10045);
    IPXAddress address;
    if (!readAddress(destination, destinationLength, address)) return -1;
    int error = state->send(bytes, length, address);
    return error ? fail(error) : length;
}
int recvfrom(WinApplication* app, x86::CPU& cpu, SOCKET handle, char* bytes, int length,
             int flags, sockaddr* source, int* sourceLength)
{
    auto state = stateFor(app, handle);
    if (!state) return -1;
    if (!state->bound || length < 0) return fail(10022);
    if ((length && !bytes) || (source && (!sourceLength || *sourceLength < 14))) return fail(10014);
    if (flags & ~2) return fail(10045); // MSG_PEEK
    for (;;)
    {
        if (cpu.terminate) return fail(10004);
        if (int error = state->pump()) return fail(error);
        if (!state->packets.empty())
        {
            const auto& packet = state->packets.front();
            size_t size = packet.bytes.size();
            if (source && !writeAddress(packet.source, source, sourceLength)) return -1;
            if (length) memcpy(bytes, packet.bytes.data(), std::min<size_t>(length, size));
            if (!(flags & 2)) state->packets.pop_front();
            return size > size_t(length) ? fail(10040) : int(size);
        }
        if (state->nonblocking) return fail(10035);
        waitNative(app, cpu, 2);
    }
}
int getsockname(WinApplication* app, x86::CPU&, SOCKET handle, sockaddr* name, int* length)
{
    auto state = stateFor(app, handle);
    if (!state) return -1;
    if (!state->bound) return fail(10022);
    return writeAddress(state->address, name, length) ? 0 : -1;
}
int bind(WinApplication* app, x86::CPU&, SOCKET handle, const sockaddr* name, int length)
{
    auto state = stateFor(app, handle);
    if (!state) return -1;
    IPXAddress address;
    if (!readAddress(name, length, address)) return -1;
    int error = state->bind(address);
    return error ? fail(error) : 0;
}
unsigned short (htons)(WinApplication*, x86::CPU&, unsigned short value) { return (value >> 8) | (value << 8); }
unsigned short (ntohs)(WinApplication*, x86::CPU&, unsigned short value) { return (value >> 8) | (value << 8); }
unsigned long inet_addr(WinApplication*, x86::CPU&, const char* text)
{
    unsigned a, b, c, d; char extra;
    if (!text || sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0xffffffffu;
    return a | (b << 8) | (c << 16) | (d << 24);
}
Packed<char> inet_ntoa(WinApplication* app, x86::CPU&, x86::reg32 value)
{
    if (!Socket::initialized(app)) { fail(10093); return Packed<char>(); }
    return Packed<char>(Socket::netToA(app, value));
}
int gethostname(WinApplication* app, x86::CPU&, char* name, int length)
{
    if (!Socket::initialized(app)) return fail(10093);
    if (!name || length < 5) return fail(10014);
    memcpy(name, "NFS3", 5);
    return 0;
}
int setsockopt(WinApplication* app, x86::CPU&, SOCKET handle, int level, int option, const char* bytes, int length)
{
    auto state = stateFor(app, handle);
    if (!state) return -1;
    if (!bytes || length < 4) return fail(10014);
    int value; memcpy(&value, bytes, 4);
    if ((level == 0xffff || level == 1000) && option == 0x400f) return 0; // IPX_RECEIVE_BROADCAST compatibility
    if (level == 1000)
    {
        if (option == 0x4000) { state->packetType = uint8_t(value); return 0; }
        if (option == 0x4001) { state->filterType = uint8_t(value); return 0; }
        if (option == 0x4003) { state->filterType = -1; return 0; }
    }
    if (level != 0xffff) return fail(10042);
    if (option == 0x20) { state->broadcast = value != 0; return 0; }
    if (option == 4) return 0; // Guest ports do not map to fixed native UDP ports.
    return fail(10042);
}
int closesocket(WinApplication* app, x86::CPU&, SOCKET handle)
{
    if (!stateFor(app, handle)) return -1;
    return app->freeResourceIfPresent(handle) ? 0 : fail(10038);
}
SOCKET socket(WinApplication* app, x86::CPU&, int family, int type, int protocol)
{
    if (!Socket::initialized(app)) { fail(10093); return SOCKET(-1); }
    if (family != 6) { fail(10047); return SOCKET(-1); }
    if (type != 2) { fail(10044); return SOCKET(-1); }
    if (protocol != 0 && protocol != 1000) { fail(10043); return SOCKET(-1); }
    auto socket = std::make_unique<Socket>(app);
    if (socket->state()->descriptor < 0) { fail(Socket::winsockError(errno)); return SOCKET(-1); }
    return app->allocateResource(socket.release());
}
int WSACleanup(WinApplication* app, x86::CPU&) { return Socket::fini(app) ? 0 : fail(10093); }
int WSAStartup(WinApplication* app, x86::CPU&, WORD requested, LPWSADATA data)
{
    if (!data) return 10014;
    if ((requested & 255) < 1) return 10092;
    memset(data, 0, sizeof(*data));
    data->wVersion = requested == 1 ? 1 : 0x0101;
    data->wHighVersion = 0x0101;
    data->iMaxSockets = 64;
    data->iMaxUdpDg = 1600;
    strcpy(data->szDescription, "NFS IPX over UDP broadcast");
    strcpy(data->szSystemStatus, "Ready");
    Socket::init(app);
    return 0;
}
Packed<hostent> gethostbyname(WinApplication*, x86::CPU&, const char*) { fail(11001); return Packed<hostent>(); }
int ioctlsocket(WinApplication* app, x86::CPU&, SOCKET handle, x86::sreg32 command, x86::reg32* value)
{
    auto state = stateFor(app, handle);
    if (!state) return -1;
    if (!value) return fail(10014);
    if (uint32_t(command) == 0x8004667e) { state->nonblocking = *value != 0; return 0; }
    if (uint32_t(command) == 0x4004667f)
    {
        if (int error = state->pump()) return fail(error);
        *value = state->packets.empty() ? 0 : uint32_t(state->packets.front().bytes.size());
        return 0;
    }
    return fail(10022);
}
int __WSAFDIsSet(WinApplication*, x86::CPU&, SOCKET handle, fd_set* set)
{
    if (!set || set->fd_count > FD_SETSIZE_WSA) return 0;
    return std::find(set->fd_array, set->fd_array + set->fd_count, handle) != set->fd_array + set->fd_count;
}
int listen(WinApplication* app, x86::CPU&, SOCKET handle, int) { return unsupported(app, handle); }
int shutdown(WinApplication* app, x86::CPU&, SOCKET handle, int) { return unsupported(app, handle); }
int connect(WinApplication* app, x86::CPU&, SOCKET handle, const sockaddr*, int) { return unsupported(app, handle); }
SOCKET accept(WinApplication* app, x86::CPU&, SOCKET handle, sockaddr*, int*) { unsupported(app, handle); return SOCKET(-1); }
int send(WinApplication* app, x86::CPU&, SOCKET handle, const char*, int, int) { return unsupported(app, handle); }
int recv(WinApplication* app, x86::CPU&, SOCKET handle, char*, int, int) { return unsupported(app, handle); }
int WSAGetLastError(WinApplication*, x86::CPU&) { return socketError; }
}
