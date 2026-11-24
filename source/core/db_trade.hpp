// Local Wi-Fi trade protocol between two systems running Devious Bank (over db_net).
//
// Flow
//   1. Host waits (broadcasting a beacon); guest browses, picks the host, connects.
//      Both send HELLO (protocol version + trainer name).
//   2. Each side OFFERs one Pokemon: its raw decrypted bytes (the same record a bank slot
//      holds). The receiver validates them (format, length, parse, checksum) and checks it
//      could take that Pokemon in the slot it is offering from (generation rules).
//   3. Each side CONFIRMs the pair it sees: CONFIRM carries the ids of both offers, so a
//      confirmation only counts for that exact pair. An offer's id is CRC32 of its bytes
//      plus a per-offer counter, so re-offering the same Pokemon is still a new pair.
//      A confirmation can't be taken back; the only way out is to change or withdraw the
//      offer, which makes a new pair and clears both confirmations. (This is what stops a
//      stale READY still on the wire from ever matching a later pair.)
//   4. When a side holds both confirmations for the same pair it LOCKS: it writes both
//      Pokemon to the trade log on the SD card, then sends READY(pair). A locked side can't
//      change its offer. A side commits only when it is locked AND has the partner's
//      READY for the same pair, so a late offer change can never make the two sides
//      commit different trades.
//   5. On commit the UI swaps the received Pokemon into the offered slot and saves.
//
// The trade log (sdmc:/3ds/DB/trades/<time>/sent.pkN + received.pkN) exists because no
// network protocol can make two machines commit atomically: if the link drops in the
// instant between the two READYs, one side may commit and the other not. Whatever happens,
// both Pokemon are on the SD card of each system that locked, as standard .pkN files.
#pragma once

#include "core/db_net.hpp"
#include "core/db_slot.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace db::trade
{
    inline constexpr uint16_t PROTOCOL_VERSION = 1;
    inline constexpr const char* TRADE_LOG_DIR = "sdmc:/3ds/DB/trades";

    enum MsgType : uint8_t
    {
        MSG_HELLO     = 1, // u16 version, u8 nameLen, name
        MSG_OFFER     = 2, // u8 format, u8 0, u16 length, u32 offer counter, data[length]
        MSG_WITHDRAW  = 3, // -
        MSG_CONFIRM   = 4, // u32 id(sender's offer), u32 id(receiver's offer)
        // 5 was UNCONFIRM: removed on purpose (confirmations are final for a pair)
        MSG_READY     = 6, // same payload as CONFIRM
        MSG_BYE       = 7, // -
        MSG_PING      = 8, // - (heartbeat every 2 s; 15 s of silence ends the session)
    };

    class Session
    {
    public:
        enum class Role
        {
            Host,
            Guest,
        };
        enum class Phase
        {
            Waiting,    // host: waiting for a partner; guest: browsing for hosts
            Connecting, // TCP connect / waiting for HELLO
            Trading,    // connected, choosing and confirming offers
            Committed,  // both READY for the same pair: the UI must apply, then completeCommit()
            Ended,      // left, disconnected or failed: see endReason()
        };

        // Returns null (and sets err) if Wi-Fi isn't available.
        static std::unique_ptr<Session> host(const std::string& myName, std::string& err);
        static std::unique_ptr<Session> browse(const std::string& myName, std::string& err);
        ~Session();

        // Guest: connect to a host found by browsing, or typed in by hand.
        bool join(const std::string& ip, std::string& err);
        // Pumps the network and runs the protocol. Call once per frame.
        void update();
        // Says goodbye (if connected) and closes everything. Phase becomes Ended.
        void leave();

        [[nodiscard]] Role role() const { return myRole; }
        [[nodiscard]] Phase phase() const { return state; }
        [[nodiscard]] const std::string& endReason() const { return reason; }
        [[nodiscard]] const std::string& partnerName() const { return partner; }
        [[nodiscard]] std::string localIp() const { return net::localIp(); }
        [[nodiscard]] const std::vector<net::Peer>& peers() const;

        // ---- Offers (only while Trading and not locked)
        // `acceptCheck` answers: could the Pokemon in `incoming` go where my offer comes from?
        // Called whenever either offer changes.
        void setAcceptCheck(std::function<bool(const Slot& incoming, std::string& why)> f);
        bool offer(const Slot& mine, std::string& why);
        void withdraw();
        // Confirm the current pair. False (with why) if there's nothing to confirm or the
        // partner's offer can't be received here. Final for this pair: to back out, withdraw.
        bool confirm(std::string& why);

        [[nodiscard]] const std::optional<Slot>& myOffer() const { return mine; }
        [[nodiscard]] const std::optional<Slot>& partnerOffer() const { return theirs; }
        [[nodiscard]] bool partnerOfferOk() const { return theirsOk; }
        [[nodiscard]] const std::string& partnerOfferProblem() const { return theirsProblem; }
        [[nodiscard]] bool myConfirmed() const { return iConfirmed; }
        [[nodiscard]] bool partnerConfirmed() const { return theyConfirmed; }
        [[nodiscard]] bool locked() const { return isLocked; }
        // Folder of the trade log written when this trade locked.
        [[nodiscard]] const std::string& tradeLogPath() const { return logPath; }

        // After the UI has applied a Committed trade (or failed to): back to Trading with no
        // offers, ready for another trade.
        void completeCommit();

    private:
        Session() = default;
        void handle(const net::Message& m);
        void sendHello();
        void resetConfirmations();
        void reevaluatePartnerOffer();
        void tryLock();
        void tryCommit();
        void end(const std::string& why);
        [[nodiscard]] uint32_t crcMine() const;
        [[nodiscard]] uint32_t crcTheirs() const;

        Role myRole = Role::Host;
        Phase state = Phase::Waiting;
        std::string myName, partner, reason, logPath;

        std::unique_ptr<net::Beacon> beacon;
        std::unique_ptr<net::Discovery> discovery;
        net::Connection conn;
        bool helloSent = false, helloReceived = false;

        std::function<bool(const Slot&, std::string&)> acceptCheck;
        std::optional<Slot> mine, theirs;
        uint32_t mineCounter = 0, theirsCounter = 0; // per-offer counters (part of the id)
        uint32_t nextCounter = 0;
        bool theirsOk = false;
        std::string theirsProblem;
        bool iConfirmed = false, theyConfirmed = false;
        bool isLocked = false, partnerReady = false;
        std::string partnerLeft; // BYE that arrived after commit; ends the session afterwards
        uint64_t lastPingSent = 0, lastHeard = 0; // heartbeat (osGetTime ms)
    };
}
