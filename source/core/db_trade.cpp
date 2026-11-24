#include "core/db_trade.hpp"
#include "core/db_fs.hpp"

#include <3ds.h>
#include <cstring>

namespace db::trade
{
    namespace
    {
        void put16(std::vector<uint8_t>& v, uint16_t x)
        {
            v.push_back(x & 0xFF);
            v.push_back(x >> 8);
        }
        void put32(std::vector<uint8_t>& v, uint32_t x)
        {
            for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF);
        }
        uint16_t get16(const uint8_t* p) { return p[0] | (p[1] << 8); }
        uint32_t get32(const uint8_t* p)
        {
            return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
        }

        // Identifies one offer: its format, length, bytes and the offering side's counter.
        uint32_t offerId(const Slot& s, uint32_t counter)
        {
            std::vector<uint8_t> v;
            v.push_back((uint8_t)s.format);
            put16(v, s.length);
            put32(v, counter);
            v.insert(v.end(), s.data.begin(), s.data.begin() + s.length);
            return fs::crc32(v.data(), v.size());
        }

        const char* ext(PkxFormat f)
        {
            switch (f)
            {
                case PkxFormat::PK1: return "pk1";
                case PkxFormat::PK2: return "pk2";
                case PkxFormat::PK3: return "pk3";
                case PkxFormat::PK4: return "pk4";
                case PkxFormat::PK5: return "pk5";
                case PkxFormat::PK6: return "pk6";
                default:             return "pk7";
            }
        }

        // Rebuilds and checks a Slot received from the partner. Never trusts the bytes:
        // format, length, parse and checksum must all be right.
        bool parseOffer(const net::Message& m, Slot& out, uint32_t& counter, std::string& why)
        {
            const auto& p = m.payload;
            if (p.size() < 8)
            {
                why = "Bad offer from partner";
                return false;
            }
            const uint8_t fmt  = p[0];
            const uint16_t len = get16(&p[2]);
            if (fmt < (uint8_t)PkxFormat::PK1 || fmt > (uint8_t)PkxFormat::PK7 ||
                len > SLOT_DATA_MAX || p.size() != 8u + len ||
                !validLength((PkxFormat)fmt, len))
            {
                why = "Partner sent Pokemon data this version can't read";
                return false;
            }
            counter = get32(&p[4]);
            Slot s;
            s.format = (PkxFormat)fmt;
            s.length = len;
            std::copy(p.begin() + 8, p.end(), s.data.begin());

            std::unique_ptr<pksm::PKX> pk;
            try
            {
                pk = s.toPkx();
            }
            catch (...)
            {
            }
            if (!pk)
            {
                why = "Partner's Pokemon data is unreadable";
                return false;
            }
            // Same checks a save slot gets on load: species present, checksum good.
            const Slot check = Slot::fromPkx(*pk);
            if (check.empty() || (check.flags & SLOT_CORRUPT))
            {
                why = "Partner's Pokemon data is damaged";
                return false;
            }
            out = s;
            return true;
        }
    }

    // ---- construction ------------------------------------------------------------------

    std::unique_ptr<Session> Session::host(const std::string& myName, std::string& err)
    {
        if (!net::available() && !net::start(err))
        {
            return nullptr;
        }
        if (net::localIp().empty())
        {
            err = "Not connected to Wi-Fi. Connect in System Settings first.";
            return nullptr;
        }
        std::unique_ptr<Session> s(new Session());
        s->myRole = Role::Host;
        s->myName = myName;
        s->beacon = std::make_unique<net::Beacon>();
        if (!s->beacon->open() || !s->conn.listen(err))
        {
            if (err.empty()) err = "Couldn't start hosting";
            return nullptr;
        }
        s->state = Phase::Waiting;
        return s;
    }

    std::unique_ptr<Session> Session::browse(const std::string& myName, std::string& err)
    {
        if (!net::available() && !net::start(err))
        {
            return nullptr;
        }
        if (net::localIp().empty())
        {
            err = "Not connected to Wi-Fi. Connect in System Settings first.";
            return nullptr;
        }
        std::unique_ptr<Session> s(new Session());
        s->myRole    = Role::Guest;
        s->myName    = myName;
        s->discovery = std::make_unique<net::Discovery>();
        // If listening for beacons fails, joining by typed-in IP still works.
        s->discovery->open();
        s->state = Phase::Waiting;
        return s;
    }

    Session::~Session()
    {
        leave();
    }

    const std::vector<net::Peer>& Session::peers() const
    {
        static const std::vector<net::Peer> none;
        return discovery ? discovery->peers() : none;
    }

    bool Session::join(const std::string& ip, std::string& err)
    {
        if (myRole != Role::Guest || state != Phase::Waiting)
        {
            err = "Not browsing";
            return false;
        }
        if (!conn.connect(ip, err))
        {
            return false;
        }
        state = Phase::Connecting;
        return true;
    }

    void Session::leave()
    {
        if (conn.connected())
        {
            // Say goodbye, and give anything still queued (e.g. a READY that went out in
            // pieces) up to half a second to leave before the socket closes.
            conn.send(MSG_BYE, nullptr, 0);
            const uint64_t until = osGetTime() + 500;
            while (conn.connected() && conn.pendingSend() && osGetTime() < until)
            {
                conn.poll();
                svcSleepThread(10 * 1000 * 1000LL); // 10 ms
            }
        }
        conn.close();
        beacon.reset();
        discovery.reset();
        if (state != Phase::Ended)
        {
            state  = Phase::Ended;
            reason = reason.empty() ? "You left the trade" : reason;
        }
    }

    void Session::end(const std::string& why)
    {
        reason = why;
        conn.close();
        beacon.reset();
        discovery.reset();
        state = Phase::Ended;
    }

    // ---- per-frame -----------------------------------------------------------------------

    void Session::update()
    {
        if (state == Phase::Ended)
        {
            return;
        }
        if (state == Phase::Waiting && myRole == Role::Host && beacon)
        {
            beacon->tick(myName);
        }
        if (discovery)
        {
            discovery->poll();
        }

        conn.poll();
        if (conn.connected())
        {
            if (state == Phase::Waiting) // host: a guest just connected
            {
                beacon.reset();
                state = Phase::Connecting;
            }
            if (!helloSent)
            {
                sendHello();
                lastHeard = osGetTime(); // start the silence clock at connect
            }
            // Heartbeat: a partner that loses power or Wi-Fi never closes the socket, so
            // silence is the only sign. Ending is safe even while locked: the trade log is
            // already on the SD card.
            const uint64_t now = osGetTime();
            if (now - lastPingSent >= 2000)
            {
                conn.send(MSG_PING, nullptr, 0);
                lastPingSent = now;
            }
            if (state != Phase::Committed && now - lastHeard > 15000)
            {
                end("Lost contact with your partner");
                return;
            }
        }
        // Read everything that arrived, even if the link just dropped: the partner's READY
        // may be among the last messages, and then this side must commit too so both sides
        // end up with the same trade.
        net::Message m;
        // Stop at a commit: later messages (e.g. the partner's next offer) wait in the inbox
        // until the UI has applied this trade, so they can't overwrite what is being applied.
        while (state != Phase::Ended && state != Phase::Committed && conn.receive(m))
        {
            lastHeard = osGetTime();
            handle(m);
            if (state == Phase::Trading)
            {
                tryCommit(); // a READY followed by BYE in one batch must still commit
            }
        }
        if (state == Phase::Trading)
        {
            if (conn.connected())
            {
                tryLock(); // never lock on a dead link: the partner couldn't get our READY
            }
            tryCommit();
        }
        if (conn.failed() && state != Phase::Committed && state != Phase::Ended)
        {
            end(conn.error().empty() ? "Connection lost" : conn.error());
        }
    }

    void Session::sendHello()
    {
        std::vector<uint8_t> p;
        put16(p, PROTOCOL_VERSION);
        const std::string name = myName.substr(0, 16);
        p.push_back((uint8_t)name.size());
        p.insert(p.end(), name.begin(), name.end());
        conn.send(MSG_HELLO, p.data(), (uint16_t)p.size());
        helloSent = true;
    }

    void Session::handle(const net::Message& m)
    {
        switch (m.type)
        {
            case MSG_HELLO:
            {
                if (m.payload.size() < 3 || get16(m.payload.data()) != PROTOCOL_VERSION)
                {
                    conn.send(MSG_BYE, nullptr, 0);
                    end("Your partner is running a different version of Devious Bank");
                    return;
                }
                const size_t n = std::min<size_t>(m.payload[2], m.payload.size() - 3);
                partner.assign((const char*)m.payload.data() + 3, n);
                helloReceived = true;
                if (state == Phase::Connecting)
                {
                    state = Phase::Trading;
                }
                break;
            }
            case MSG_OFFER:
            {
                Slot s;
                uint32_t counter = 0;
                std::string why;
                resetConfirmations(); // the pair changed (also unlocks: we can't commit it)
                if (parseOffer(m, s, counter, why))
                {
                    theirs        = s;
                    theirsCounter = counter;
                    reevaluatePartnerOffer();
                }
                else
                {
                    theirs.reset();
                    theirsOk      = false;
                    theirsProblem = why;
                }
                break;
            }
            case MSG_WITHDRAW:
                resetConfirmations();
                theirs.reset();
                theirsOk = false;
                theirsProblem.clear();
                break;
            case MSG_CONFIRM:
            case MSG_READY:
            {
                if (m.payload.size() != 8 || !mine || !theirs)
                {
                    break;
                }
                // Sender's view: (their offer, my offer). Only counts for the pair we see.
                const bool samePair = get32(&m.payload[0]) == crcTheirs() &&
                                      get32(&m.payload[4]) == crcMine();
                if (!samePair)
                {
                    break; // stale: an offer changed after they sent it
                }
                theyConfirmed = true;
                if (m.type == MSG_READY)
                {
                    partnerReady = true;
                }
                break;
            }
            case MSG_BYE:
            {
                const std::string why =
                    partner.empty() ? "Your partner left the trade" : partner + " left the trade";
                if (state == Phase::Committed)
                {
                    partnerLeft = why; // finish applying this trade first; end afterwards
                }
                else
                {
                    end(why);
                }
                break;
            }
            default:
                break; // unknown types are ignored
        }
    }

    // ---- offers ----------------------------------------------------------------------------

    void Session::setAcceptCheck(std::function<bool(const Slot&, std::string&)> f)
    {
        acceptCheck = std::move(f);
        reevaluatePartnerOffer();
    }

    void Session::reevaluatePartnerOffer()
    {
        theirsProblem.clear();
        theirsOk = false;
        if (!theirs)
        {
            return;
        }
        if (acceptCheck && !acceptCheck(*theirs, theirsProblem))
        {
            return;
        }
        theirsOk = true;
    }

    bool Session::offer(const Slot& s, std::string& why)
    {
        if (state != Phase::Trading)
        {
            why = "Not connected to a partner";
            return false;
        }
        if (isLocked)
        {
            why = "Locked in: waiting for your partner";
            return false;
        }
        if (s.empty() || !s.movable() || !validLength(s.format, s.length))
        {
            why = "That Pokemon can't be traded";
            return false;
        }
        resetConfirmations();
        mine        = s;
        mineCounter = ++nextCounter;
        std::vector<uint8_t> p;
        p.push_back((uint8_t)s.format);
        p.push_back(0);
        put16(p, s.length);
        put32(p, mineCounter);
        p.insert(p.end(), s.data.begin(), s.data.begin() + s.length);
        conn.send(MSG_OFFER, p.data(), (uint16_t)p.size());
        reevaluatePartnerOffer(); // my slot changed, so what I can receive may change
        return true;
    }

    void Session::withdraw()
    {
        if (state != Phase::Trading || isLocked || !mine)
        {
            return;
        }
        resetConfirmations();
        mine.reset();
        conn.send(MSG_WITHDRAW, nullptr, 0);
    }

    bool Session::confirm(std::string& why)
    {
        if (state != Phase::Trading)
        {
            why = "Not connected to a partner";
            return false;
        }
        if (!mine || !theirs)
        {
            why = "Both of you need to offer a Pokemon first";
            return false;
        }
        if (!theirsOk)
        {
            why = theirsProblem.empty() ? "You can't receive that Pokemon here" : theirsProblem;
            return false;
        }
        if (iConfirmed)
        {
            return true;
        }
        iConfirmed = true;
        std::vector<uint8_t> p;
        put32(p, crcMine());
        put32(p, crcTheirs());
        conn.send(MSG_CONFIRM, p.data(), (uint16_t)p.size());
        return true;
    }

    void Session::resetConfirmations()
    {
        iConfirmed    = false;
        theyConfirmed = false;
        partnerReady  = false;
        isLocked      = false;
    }

    uint32_t Session::crcMine() const { return mine ? offerId(*mine, mineCounter) : 0; }
    uint32_t Session::crcTheirs() const { return theirs ? offerId(*theirs, theirsCounter) : 0; }

    // ---- lock and commit -------------------------------------------------------------------

    void Session::tryLock()
    {
        if (isLocked || !iConfirmed || !theyConfirmed || !mine || !theirs || !theirsOk)
        {
            return;
        }

        // Write both Pokemon to the SD card BEFORE promising anything to the partner.
        std::string dir = std::string(TRADE_LOG_DIR) + "/" + fs::timestamp() + "_" +
                          fs::sanitize(partner.empty() ? "partner" : partner);
        const bool ok = fs::mkdirs(dir) &&
                        fs::writeFileVerified(dir + "/sent." + ext(mine->format),
                            mine->data.data(), mine->length) &&
                        fs::writeFileVerified(dir + "/received." + ext(theirs->format),
                            theirs->data.data(), theirs->length);
        if (!ok)
        {
            // Without the safety copy, don't trade. Confirmations are final for a pair, so
            // back out the only safe way: withdraw the offer (a new pair if re-offered).
            withdraw();
            theirsProblem = "Couldn't write the trade log to the SD card; offer withdrawn";
            return;
        }
        logPath  = dir;
        isLocked = true;
        std::vector<uint8_t> p;
        put32(p, crcMine());
        put32(p, crcTheirs());
        conn.send(MSG_READY, p.data(), (uint16_t)p.size());
    }

    void Session::tryCommit()
    {
        if (isLocked && partnerReady)
        {
            state = Phase::Committed;
        }
    }

    void Session::completeCommit()
    {
        mine.reset();
        theirs.reset();
        theirsOk = false;
        theirsProblem.clear();
        resetConfirmations();
        if (state == Phase::Committed)
        {
            state = Phase::Trading;
            if (!partnerLeft.empty())
            {
                end(partnerLeft);
            }
            else if (conn.failed())
            {
                end(conn.error().empty() ? "Connection lost" : conn.error());
            }
        }
    }
}
