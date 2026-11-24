// Local Wi-Fi networking for trades between two systems running Devious Bank.
//
// Both systems must be on the same Wi-Fi network (the 3DS's normal internet connection).
//   - Discovery: a hosting system broadcasts a small UDP beacon once a second on
//     BEACON_PORT; joining systems listen for beacons and list the hosts they hear.
//   - Session:   one TCP connection on TRADE_PORT, host listens, guest connects.
//   - Messages:  framed as  [u32 magic "DVBT"][u8 type][u16 length][length bytes payload],
//                little-endian. The trade protocol (db_trade) gives types meaning; this
//                layer only frames, sends and reassembles them.
// Everything is non-blocking and polled once per frame from the main loop.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace db::net
{
    inline constexpr uint16_t TRADE_PORT  = 47211;
    inline constexpr uint16_t BEACON_PORT = 47212;
    inline constexpr uint32_t MAGIC       = 0x54425644; // "DVBT"
    inline constexpr uint16_t MAX_PAYLOAD = 1024;

    // Starts / stops the SOC service (socket buffer allocated here, freed in stop()).
    // stop() is safe to call even if start() failed or was never called.
    bool start(std::string& err);
    void stop();
    [[nodiscard]] bool available();
    // This system's IP as "a.b.c.d", or "" if not connected to a network.
    [[nodiscard]] std::string localIp();

    struct Message
    {
        uint8_t type = 0;
        std::vector<uint8_t> payload;
    };

    // A host heard on the network.
    struct Peer
    {
        std::string ip;
        std::string name;   // trainer name sent in the beacon (may be empty)
        uint64_t lastSeen;  // osGetTime() ms
    };

    // Host side: broadcasts "DVBK-TRADE\0<name>" to BEACON_PORT about once a second.
    class Beacon
    {
    public:
        Beacon() = default;
        ~Beacon() { close(); }
        Beacon(const Beacon&)            = delete;
        Beacon& operator=(const Beacon&) = delete;

        bool open();
        void tick(const std::string& name); // call every frame; sends at most once a second
        void close();

    private:
        int sock          = -1;
        uint64_t lastSend = 0;
    };

    // Guest side: listens for beacons and keeps a list of hosts heard in the last 5 seconds.
    class Discovery
    {
    public:
        Discovery() = default;
        ~Discovery() { close(); }
        Discovery(const Discovery&)            = delete;
        Discovery& operator=(const Discovery&) = delete;

        bool open();
        void poll(); // call every frame
        [[nodiscard]] const std::vector<Peer>& peers() const { return found; }
        void close();

    private:
        int sock = -1;
        std::vector<Peer> found;
    };

    // One framed TCP connection.
    class Connection
    {
    public:
        Connection() = default;
        ~Connection() { close(); }
        Connection(const Connection&)            = delete;
        Connection& operator=(const Connection&) = delete;

        // Host: start listening on TRADE_PORT. Then call poll() each frame until connected().
        bool listen(std::string& err);
        // Guest: start a non-blocking connect to ip:TRADE_PORT. Gives up after 8 seconds.
        bool connect(const std::string& ip, std::string& err);

        // Pumps the socket: accepts a pending connection (host), finishes a pending
        // connect (guest), reads incoming bytes. Call every frame.
        void poll();
        [[nodiscard]] bool connected() const { return state == State::Connected; }
        // True once the connection is lost, refused, timed out, or sent garbage.
        [[nodiscard]] bool failed() const { return state == State::Failed; }
        [[nodiscard]] const std::string& error() const { return err; }
        [[nodiscard]] const std::string& peerIp() const { return peer; }
        // True while queued bytes haven't been handed to the network yet.
        [[nodiscard]] bool pendingSend() const { return !outBuf.empty(); }

        // Queues and sends a message (best effort non-blocking; partial writes are kept and
        // flushed on later poll() calls). False if not connected or payload too large.
        bool send(uint8_t type, const void* data, uint16_t len);
        // Pops the next complete received message. False if none is waiting.
        bool receive(Message& out);

        void close();

    private:
        enum class State
        {
            Idle,
            Listening,
            Connecting,
            Connected,
            Failed,
        };
        void fail(const std::string& why);
        void flush();

        State state      = State::Idle;
        int listenSock   = -1;
        int sock         = -1;
        uint64_t started = 0;
        uint32_t target  = 0; // guest: host address (network byte order), to re-check connect
        std::string err;
        std::string peer;
        std::vector<uint8_t> inBuf;
        std::vector<uint8_t> outBuf;
        std::vector<Message> inbox;
    };
}
