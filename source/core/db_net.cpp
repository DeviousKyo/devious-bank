#include "core/db_net.hpp"

#include <3ds.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace db::net
{
    namespace
    {
        constexpr u32 SOC_BUFFER_SIZE        = 0x100000;
        constexpr uint64_t BEACON_INTERVAL   = 1000;
        constexpr uint64_t PEER_TIMEOUT      = 5000;
        constexpr uint64_t CONNECT_TIMEOUT   = 8000;
        constexpr size_t HEADER_SIZE         = 7; // magic(4) + type(1) + len(2)
        constexpr size_t MAX_NAME            = 32;
        constexpr char BEACON_TAG[]          = "DVBK-TRADE"; // sent with its terminating '\0'
        constexpr size_t BEACON_TAG_SIZE     = sizeof(BEACON_TAG);

        u32* socBuffer = nullptr;
        bool socReady  = false;

        bool wouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == EINTR; }

        bool setNonBlocking(int fd)
        {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags < 0)
            {
                flags = 0;
            }
            return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
        }

        void closeSock(int& fd)
        {
            if (fd >= 0)
            {
                ::close(fd);
                fd = -1;
            }
        }

        std::string ipString(in_addr addr)
        {
            // Formatted by hand: inet_ntoa uses a shared static buffer.
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&addr.s_addr);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            return buf;
        }

        in_addr broadcastAddress()
        {
            in_addr ip{}, mask{}, bcast{};
            if (SOCU_GetIPInfo(&ip, &mask, &bcast) == 0)
            {
                if (bcast.s_addr != 0)
                {
                    return bcast;
                }
                if (ip.s_addr != 0 && mask.s_addr != 0)
                {
                    bcast.s_addr = ip.s_addr | ~mask.s_addr;
                    return bcast;
                }
            }
            bcast.s_addr = htonl(INADDR_BROADCAST);
            return bcast;
        }

        void putU32(std::vector<uint8_t>& v, uint32_t x)
        {
            for (int i = 0; i < 4; i++)
            {
                v.push_back((uint8_t)(x >> (8 * i)));
            }
        }
    }

    bool start(std::string& err)
    {
        if (socReady)
        {
            return true;
        }
        socBuffer = static_cast<u32*>(memalign(0x1000, SOC_BUFFER_SIZE));
        if (!socBuffer)
        {
            err = "Wi-Fi unavailable (out of memory)";
            return false;
        }
        Result rc = socInit(socBuffer, SOC_BUFFER_SIZE);
        if (R_FAILED(rc))
        {
            free(socBuffer);
            socBuffer = nullptr;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Wi-Fi unavailable (0x%08lX)", (unsigned long)rc);
            err = buf;
            return false;
        }
        socReady = true;
        return true;
    }

    void stop()
    {
        if (socReady)
        {
            socExit();
            socReady = false;
        }
        if (socBuffer)
        {
            // Only safe to free once socExit() has returned the block to us.
            free(socBuffer);
            socBuffer = nullptr;
        }
    }

    bool available() { return socReady; }

    std::string localIp()
    {
        if (!socReady)
        {
            return "";
        }
        long id = gethostid();
        if (id == 0)
        {
            return "";
        }
        in_addr addr;
        addr.s_addr = (in_addr_t)id; // already network byte order
        return ipString(addr);
    }

    // ---------------------------------------------------------------- Beacon

    bool Beacon::open()
    {
        close();
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
            return false;
        }
        // No-op on 3DS (broadcast is always allowed) but harmless elsewhere.
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
        if (!setNonBlocking(sock))
        {
            closeSock(sock);
            return false;
        }
        lastSend = 0;
        return true;
    }

    void Beacon::tick(const std::string& name)
    {
        if (sock < 0)
        {
            return;
        }
        uint64_t now = osGetTime();
        if (lastSend != 0 && now - lastSend < BEACON_INTERVAL)
        {
            return;
        }
        lastSend = now;

        uint8_t packet[BEACON_TAG_SIZE + MAX_NAME];
        std::memcpy(packet, BEACON_TAG, BEACON_TAG_SIZE);
        size_t nameLen = name.size() < MAX_NAME ? name.size() : MAX_NAME;
        std::memcpy(packet + BEACON_TAG_SIZE, name.data(), nameLen);

        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port   = htons(BEACON_PORT);
        to.sin_addr   = broadcastAddress();
        // Best effort: a dropped beacon is simply resent next second.
        sendto(sock, packet, BEACON_TAG_SIZE + nameLen, 0, reinterpret_cast<sockaddr*>(&to),
               sizeof(to));
    }

    void Beacon::close() { closeSock(sock); }

    // ------------------------------------------------------------- Discovery

    bool Discovery::open()
    {
        close();
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
            return false;
        }
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(BEACON_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            !setNonBlocking(sock))
        {
            closeSock(sock);
            return false;
        }
        return true;
    }

    void Discovery::poll()
    {
        if (sock < 0)
        {
            return;
        }
        const std::string self = localIp();
        uint64_t now           = osGetTime();

        uint8_t packet[256];
        for (;;)
        {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            ssize_t n = recvfrom(sock, packet, sizeof(packet), 0,
                                 reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n < 0)
            {
                // EWOULDBLOCK means drained; anything else, try again next frame.
                break;
            }
            if ((size_t)n < BEACON_TAG_SIZE || std::memcmp(packet, BEACON_TAG, BEACON_TAG_SIZE) != 0)
            {
                continue;
            }
            std::string ip = ipString(from.sin_addr);
            if (ip == self)
            {
                continue;
            }
            // Name runs to the end of the packet (or an embedded '\0'), capped at MAX_NAME.
            size_t nameLen = (size_t)n - BEACON_TAG_SIZE;
            if (nameLen > MAX_NAME)
            {
                nameLen = MAX_NAME;
            }
            const char* namePtr = reinterpret_cast<const char*>(packet + BEACON_TAG_SIZE);
            std::string name(namePtr, strnlen(namePtr, nameLen));

            bool known = false;
            for (Peer& p : found)
            {
                if (p.ip == ip)
                {
                    p.name     = name;
                    p.lastSeen = now;
                    known      = true;
                    break;
                }
            }
            if (!known)
            {
                found.push_back(Peer{ip, name, now});
            }
        }

        std::erase_if(found, [now](const Peer& p) { return now - p.lastSeen > PEER_TIMEOUT; });
    }

    void Discovery::close()
    {
        closeSock(sock);
        found.clear();
    }

    // ------------------------------------------------------------ Connection

    bool Connection::listen(std::string& err)
    {
        close();
        listenSock = socket(AF_INET, SOCK_STREAM, 0);
        if (listenSock < 0)
        {
            err = "Couldn't create socket";
            return false;
        }
        int yes = 1;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(TRADE_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            err = "Couldn't open trade port";
            closeSock(listenSock);
            return false;
        }
        if (::listen(listenSock, 1) != 0 || !setNonBlocking(listenSock))
        {
            err = "Couldn't listen for partner";
            closeSock(listenSock);
            return false;
        }
        state = State::Listening;
        return true;
    }

    bool Connection::connect(const std::string& ip, std::string& err)
    {
        close();
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(TRADE_PORT);
        if (ip.empty() || inet_aton(ip.c_str(), &addr.sin_addr) == 0)
        {
            err = "Invalid address";
            return false;
        }

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            err = "Couldn't create socket";
            return false;
        }
        if (!setNonBlocking(sock))
        {
            err = "Couldn't create socket";
            closeSock(sock);
            return false;
        }
        peer   = ip;
        target = addr.sin_addr.s_addr;

        if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 ||
            errno == EISCONN)
        {
            state = State::Connected;
            return true;
        }
        if (errno == EINPROGRESS || errno == EALREADY || wouldBlock(errno))
        {
            state   = State::Connecting;
            started = osGetTime();
            return true;
        }
        err = "Couldn't reach " + ip + " (error " + std::to_string(errno) + ")";
        closeSock(sock);
        peer.clear();
        return false;
    }

    void Connection::poll()
    {
        if (state == State::Listening)
        {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            int s = accept(listenSock, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (s < 0)
            {
                if (!wouldBlock(errno))
                {
                    fail("Couldn't accept partner");
                }
                return;
            }
            if (!setNonBlocking(s))
            {
                ::close(s);
                fail("Couldn't accept partner");
                return;
            }
            closeSock(listenSock);
            sock  = s;
            peer  = ipString(from.sin_addr);
            state = State::Connected;
        }

        if (state == State::Connecting)
        {
            // Finishing a non-blocking connect. Asking the socket for SO_ERROR is unreliable
            // on the 3DS (a working connection can read as failed, which made the guest hang
            // up right after the host accepted). Instead, call connect() again: EISCONN (or
            // 0) means connected, EALREADY / EINPROGRESS means still connecting, anything
            // else is a real failure.
            // poll() isn't trusted either: the connect() re-check runs every frame, which is
            // harmless while connecting (it just reports EALREADY / EINPROGRESS).
            {
                sockaddr_in addr{};
                addr.sin_family      = AF_INET;
                addr.sin_port        = htons(TRADE_PORT);
                addr.sin_addr.s_addr = target;
                const int rc = ::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
                const int e  = errno;
                if (rc == 0 || e == EISCONN)
                {
                    state = State::Connected;
                }
                else if (e == EALREADY || e == EINPROGRESS || wouldBlock(e))
                {
                    // Not there yet: keep waiting (the timeout below still applies).
                }
                else
                {
                    fail("Couldn't reach " + peer + " (error " + std::to_string(e) + ")");
                    return;
                }
            }
            if (state == State::Connecting)
            {
                if (osGetTime() - started > CONNECT_TIMEOUT)
                {
                    fail("Couldn't reach " + peer + " (timed out)");
                }
                return;
            }
        }

        if (state != State::Connected)
        {
            return;
        }

        flush();
        if (state != State::Connected)
        {
            return;
        }

        bool closedByPeer = false;
        uint8_t chunk[2048];
        for (;;)
        {
            ssize_t n = recv(sock, chunk, sizeof(chunk), 0);
            if (n > 0)
            {
                inBuf.insert(inBuf.end(), chunk, chunk + n);
                continue;
            }
            if (n == 0)
            {
                closedByPeer = true;
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            if (wouldBlock(errno))
            {
                break;
            }
            // A reset/error: still split out the frames already received this poll (the
            // partner's last message may be among them), then fail below.
            closedByPeer = true;
            break;
        }

        // Split complete frames off the front of inBuf.
        size_t pos = 0;
        while (inBuf.size() - pos >= HEADER_SIZE)
        {
            const uint8_t* h = inBuf.data() + pos;
            uint32_t magic   = (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) |
                             ((uint32_t)h[3] << 24);
            uint16_t len = (uint16_t)(h[5] | (h[6] << 8));
            if (magic != MAGIC || len > MAX_PAYLOAD)
            {
                fail("Bad data from partner");
                return;
            }
            if (inBuf.size() - pos < HEADER_SIZE + len)
            {
                break;
            }
            Message m;
            m.type = h[4];
            m.payload.assign(h + HEADER_SIZE, h + HEADER_SIZE + len);
            inbox.push_back(std::move(m));
            pos += HEADER_SIZE + len;
        }
        inBuf.erase(inBuf.begin(), inBuf.begin() + (ptrdiff_t)pos);

        // Frames that arrived just before the close stay readable in the inbox.
        if (closedByPeer)
        {
            fail("Partner disconnected");
        }
    }

    bool Connection::send(uint8_t type, const void* data, uint16_t len)
    {
        if (state != State::Connected || len > MAX_PAYLOAD || (len > 0 && !data))
        {
            return false;
        }
        outBuf.reserve(outBuf.size() + HEADER_SIZE + len);
        putU32(outBuf, MAGIC);
        outBuf.push_back(type);
        outBuf.push_back((uint8_t)(len & 0xFF));
        outBuf.push_back((uint8_t)(len >> 8));
        const uint8_t* p = static_cast<const uint8_t*>(data);
        if (len > 0)
        {
            outBuf.insert(outBuf.end(), p, p + len);
        }
        flush();
        return state == State::Connected;
    }

    bool Connection::receive(Message& out)
    {
        if (inbox.empty())
        {
            return false;
        }
        out = std::move(inbox.front());
        inbox.erase(inbox.begin());
        return true;
    }

    void Connection::flush()
    {
        size_t sent = 0;
        while (sent < outBuf.size())
        {
            ssize_t n = ::send(sock, outBuf.data() + sent, outBuf.size() - sent, 0);
            if (n > 0)
            {
                sent += (size_t)n;
                continue;
            }
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n < 0 && wouldBlock(errno))
            {
                break;
            }
            fail("Partner disconnected");
            return;
        }
        outBuf.erase(outBuf.begin(), outBuf.begin() + (ptrdiff_t)sent);
    }

    void Connection::fail(const std::string& why)
    {
        closeSock(sock);
        closeSock(listenSock);
        outBuf.clear();
        state = State::Failed;
        err   = why;
    }

    void Connection::close()
    {
        closeSock(sock);
        closeSock(listenSock);
        inBuf.clear();
        outBuf.clear();
        inbox.clear();
        err.clear();
        peer.clear();
        started = 0;
        state   = State::Idle;
    }
}
