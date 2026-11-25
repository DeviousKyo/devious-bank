#include "core/db_selftest.hpp"

#include "core/db_bank.hpp"
#include "core/db_fs.hpp"
#include "core/db_slot.hpp"
#include "core/db_storage.hpp"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <set>

namespace db::selftest
{
    namespace
    {
        const std::string TEST_DIR = std::string(fs::ROOT_DIR) + "/selftest";

        // ---- helpers -------------------------------------------------------------------

        // Deletes every file in the selftest folder, then the folder itself.
        void cleanup()
        {
            if (DIR* d = opendir(TEST_DIR.c_str()))
            {
                std::vector<std::string> names;
                while (dirent* e = readdir(d))
                {
                    std::string n = e->d_name;
                    if (n != "." && n != "..")
                    {
                        names.push_back(n);
                    }
                }
                closedir(d);
                for (const std::string& n : names)
                {
                    std::remove((TEST_DIR + "/" + n).c_str());
                }
            }
            rmdir(TEST_DIR.c_str());
        }

        std::string hex(uint32_t v)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)v);
            return buf;
        }

        std::string refStr(const SlotRef& r)
        {
            return std::string(r.pane == Pane::Bank ? "B" : "S") + std::to_string(r.box) + ":" +
                   std::to_string(r.slot);
        }

        // A slot of format `f` holding `len` bytes of a pattern derived from `seed`.
        Slot makeSlot(PkxFormat f, uint16_t len, uint8_t seed)
        {
            Slot s;
            s.format = f;
            s.length = len;
            for (size_t i = 0; i < len && i < SLOT_DATA_MAX; i++)
            {
                s.data[i] = (uint8_t)(seed + i * 7 + (i >> 3));
            }
            return s;
        }

        bool sameSlot(const Slot& a, const Slot& b)
        {
            return a.format == b.format && a.flags == b.flags && a.length == b.length &&
                   a.data == b.data;
        }

        // Fresh bank with DEFAULT_BOXES empty boxes (load() on a path that doesn't exist).
        std::string freshBank(Bank& bank)
        {
            const std::string path = TEST_DIR + "/does_not_exist.dbk";
            std::remove(path.c_str());
            std::remove((path + ".bak").c_str());
            std::string err;
            if (!bank.load(path, err))
            {
                return "fresh Bank::load failed: " + err;
            }
            if (bank.boxCount() != Bank::DEFAULT_BOXES)
            {
                return "fresh bank has " + std::to_string(bank.boxCount()) + " boxes, expected " +
                       std::to_string(Bank::DEFAULT_BOXES);
            }
            return "";
        }

        std::vector<Slot> snapshot(const Bank& bank)
        {
            std::vector<Slot> out;
            for (int b = 0; b < bank.boxCount(); b++)
            {
                for (const Slot& s : bank.box(b).slots)
                {
                    out.push_back(s);
                }
            }
            return out;
        }

        std::string compareSnapshots(const std::vector<Slot>& a, const std::vector<Slot>& b)
        {
            if (a.size() != b.size())
            {
                return "slot count changed";
            }
            for (size_t i = 0; i < a.size(); i++)
            {
                if (!sameSlot(a[i], b[i]))
                {
                    return "slot " + std::to_string(i / BOX_SLOTS) + ":" +
                           std::to_string(i % BOX_SLOTS) + " changed";
                }
            }
            return "";
        }

        // Sorted list of the non-empty slots' bytes, for multiset comparison.
        std::vector<std::string> contents(const Bank& bank)
        {
            std::vector<std::string> out;
            for (const Slot& s : snapshot(bank))
            {
                if (!s.empty())
                {
                    std::string k;
                    k.push_back((char)s.format);
                    k.push_back((char)s.flags);
                    k.append((const char*)s.data.data(), s.length);
                    out.push_back(std::move(k));
                }
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        void clearDirtyFlags(Bank& bank)
        {
            for (int b = 0; b < bank.boxCount(); b++)
            {
                bank.box(b).dirty.fill(false);
            }
        }

        // ---- tests: each returns "" on pass, else what went wrong ----------------------

        std::string testCrc32()
        {
            const char* s  = "123456789";
            const uint32_t c = fs::crc32((const uint8_t*)s, std::strlen(s));
            if (c != 0xCBF43926u)
            {
                return "got " + hex(c) + ", expected 0xCBF43926";
            }
            const uint32_t e = fs::crc32(nullptr, 0);
            if (e != 0)
            {
                return "empty input gave " + hex(e) + ", expected 0x00000000";
            }
            return "";
        }

        std::string testWriteVerified()
        {
            if (!fs::mkdirs(TEST_DIR))
            {
                return "mkdirs failed";
            }
            const std::string path = TEST_DIR + "/verified.bin";
            std::vector<uint8_t> data(5000);
            for (size_t i = 0; i < data.size(); i++)
            {
                data[i] = (uint8_t)(i * 31 + 5);
            }
            if (!fs::writeFileVerified(path, data.data(), data.size()))
            {
                return "writeFileVerified returned false";
            }
            auto back = fs::readFile(path);
            if (!back)
            {
                return "readFile failed";
            }
            if (*back != data)
            {
                return "read back " + std::to_string(back->size()) + " bytes, content differs";
            }
            // Zero-length file is also valid.
            if (!fs::writeFileVerified(path, nullptr, 0))
            {
                return "zero-length writeFileVerified failed";
            }
            back = fs::readFile(path);
            if (!back || !back->empty())
            {
                return "zero-length read back wrong";
            }
            return "";
        }

        std::string testWriteAtomic()
        {
            if (!fs::mkdirs(TEST_DIR))
            {
                return "mkdirs failed";
            }
            const std::string path = TEST_DIR + "/atomic.bin";
            std::remove(path.c_str());
            std::remove((path + ".bak").c_str());
            std::remove((path + ".tmp").c_str());

            const std::vector<uint8_t> v1 = {'v', '1', 0x00, 0x11, 0x22};
            const std::vector<uint8_t> v2 = {'v', '2', 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
            if (!fs::writeFileAtomic(path, v1.data(), v1.size()))
            {
                return "first writeFileAtomic failed";
            }
            if (!fs::writeFileAtomic(path, v2.data(), v2.size()))
            {
                return "second writeFileAtomic failed";
            }
            auto cur = fs::readFile(path);
            if (!cur || *cur != v2)
            {
                return "file does not hold v2";
            }
            auto bak = fs::readFile(path + ".bak");
            if (!bak || *bak != v1)
            {
                return ".bak does not hold v1";
            }
            if (fs::exists(path + ".tmp"))
            {
                return ".tmp left behind";
            }
            return "";
        }

        // Fills a bank with a mix of formats across a few boxes.
        void populate(Bank& bank)
        {
            bank.box(0).name = "Round Trip";
            bank.box(1).name = "Second \xC3\xA9"; // UTF-8 e-acute
            bank.box(Bank::DEFAULT_BOXES - 1).name = "Last box";

            bank.box(0).slots[0]  = makeSlot(PkxFormat::PK1, 69, 0x10);
            bank.box(0).slots[1]  = makeSlot(PkxFormat::PK1, 59, 0x11);
            bank.box(0).slots[2]  = makeSlot(PkxFormat::PK2, 73, 0x20);
            bank.box(0).slots[3]  = makeSlot(PkxFormat::PK2, 63, 0x21);
            bank.box(0).slots[4]  = makeSlot(PkxFormat::PK3, 80, 0x30);
            bank.box(0).slots[29] = makeSlot(PkxFormat::PK4, 136, 0x40);
            bank.box(1).slots[0]  = makeSlot(PkxFormat::PK5, 136, 0x50);
            bank.box(1).slots[15] = makeSlot(PkxFormat::PK6, 232, 0x60);
            bank.box(Bank::DEFAULT_BOXES - 1).slots[29] = makeSlot(PkxFormat::PK7, 232, 0x70);
        }

        std::string compareBanks(const Bank& a, const Bank& b)
        {
            if (a.boxCount() != b.boxCount())
            {
                return "box count " + std::to_string(b.boxCount()) + " != " +
                       std::to_string(a.boxCount());
            }
            for (int i = 0; i < a.boxCount(); i++)
            {
                if (a.box(i).name != b.box(i).name)
                {
                    return "box " + std::to_string(i) + " name '" + b.box(i).name + "' != '" +
                           a.box(i).name + "'";
                }
                for (int s = 0; s < BOX_SLOTS; s++)
                {
                    if (!sameSlot(a.box(i).slots[s], b.box(i).slots[s]))
                    {
                        return "slot " + std::to_string(i) + ":" + std::to_string(s) + " differs";
                    }
                }
            }
            return "";
        }

        std::string testBankRoundTrip()
        {
            Bank src;
            if (auto e = freshBank(src); !e.empty())
            {
                return e;
            }
            populate(src);

            const std::vector<uint8_t> img = src.serialize();
            const size_t expected = Bank::HEADER_SIZE + src.boxCount() * Bank::BOX_RECORD_SIZE;
            if (img.size() != expected)
            {
                return "serialized size " + std::to_string(img.size()) + " != " +
                       std::to_string(expected);
            }

            Bank dst;
            std::string err;
            if (!dst.deserialize(img, err))
            {
                return "deserialize failed: " + err;
            }
            if (auto e = compareBanks(src, dst); !e.empty())
            {
                return "memory round trip: " + e;
            }

            // Through the card as well: write the image, Bank::load it back.
            if (!fs::mkdirs(TEST_DIR))
            {
                return "mkdirs failed";
            }
            const std::string path = TEST_DIR + "/roundtrip.dbk";
            if (!fs::writeFileAtomic(path, img.data(), img.size()))
            {
                return "writing bank image failed";
            }
            Bank fromFile;
            if (!fromFile.load(path, err))
            {
                return "Bank::load of written image failed: " + err;
            }
            if (fromFile.readOnly())
            {
                return "loaded bank is read-only: " + fromFile.readOnlyReason();
            }
            if (auto e = compareBanks(src, fromFile); !e.empty())
            {
                return "file round trip: " + e;
            }
            return "";
        }

        std::string testBankRejects()
        {
            Bank src;
            if (auto e = freshBank(src); !e.empty())
            {
                return e;
            }
            populate(src);
            const std::vector<uint8_t> good = src.serialize();

            Bank dst;
            std::string err;
            if (!dst.deserialize(good, err))
            {
                return "control: valid image rejected: " + err;
            }
            const std::vector<Slot> before = snapshot(dst);
            const int boxesBefore          = dst.boxCount();

            struct Case
            {
                const char* what;
                std::vector<uint8_t> img;
            };
            std::vector<Case> cases;

            cases.push_back({"wrong magic", good});
            cases.back().img[0] ^= 0xFF;

            cases.push_back({"truncated by 1 byte", good});
            cases.back().img.pop_back();

            cases.push_back({"truncated to header", good});
            cases.back().img.resize(Bank::HEADER_SIZE);

            cases.push_back({"shorter than header", good});
            cases.back().img.resize(10);

            cases.push_back({"empty file", {}});

            cases.push_back({"flipped payload byte", good});
            cases.back().img[Bank::HEADER_SIZE + Bank::BOX_NAME_SIZE + 4 + 10] ^= 0x01;

            cases.push_back({"flipped last byte", good});
            cases.back().img.back() ^= 0x80;

            for (const Case& c : cases)
            {
                std::string e;
                if (dst.deserialize(c.img, e))
                {
                    return std::string(c.what) + ": accepted";
                }
                if (dst.boxCount() != boxesBefore || !compareSnapshots(before, snapshot(dst)).empty())
                {
                    return std::string(c.what) + ": bank modified on failure";
                }
            }
            return "";
        }

        std::string testStorageSwap()
        {
            Bank bank;
            if (auto e = freshBank(bank); !e.empty())
            {
                return e;
            }
            Storage st(bank, nullptr);
            const SlotRef a{Pane::Bank, 0, 0};
            const SlotRef b{Pane::Bank, 2, 7};
            const Slot sa = makeSlot(PkxFormat::PK3, 80, 0x31);
            const Slot sb = makeSlot(PkxFormat::PK6, 232, 0x61);
            st.at(a)      = sa;
            st.at(b)      = sb;
            clearDirtyFlags(bank);

            std::string why;
            if (!st.swap(a, b, why))
            {
                return "swap failed: " + why;
            }
            if (!sameSlot(st.at(a), sb) || !sameSlot(st.at(b), sa))
            {
                return "contents not exchanged";
            }
            if (!st.box(Pane::Bank, a.box).dirty[a.slot] || !st.box(Pane::Bank, b.box).dirty[b.slot])
            {
                return "slots not marked dirty";
            }
            if (!bank.dirty())
            {
                return "bank not marked dirty";
            }

            // Swap with an empty slot = plain move.
            const SlotRef c{Pane::Bank, 5, 3};
            if (!st.swap(a, c, why))
            {
                return "swap into empty failed: " + why;
            }
            if (!st.at(a).empty() || !sameSlot(st.at(c), sb))
            {
                return "swap into empty: wrong contents";
            }
            return "";
        }

        std::string testStorageMoveGroup()
        {
            Bank bank;
            if (auto e = freshBank(bank); !e.empty())
            {
                return e;
            }
            Storage st(bank, nullptr);
            const Slot p1 = makeSlot(PkxFormat::PK4, 136, 0x41);
            const Slot p2 = makeSlot(PkxFormat::PK5, 136, 0x51);
            const Slot p3 = makeSlot(PkxFormat::PK7, 232, 0x71);
            const std::vector<SlotRef> from = {
                {Pane::Bank, 0, 0}, {Pane::Bank, 0, 1}, {Pane::Bank, 0, 2}};
            const std::vector<SlotRef> to = {
                {Pane::Bank, 1, 10}, {Pane::Bank, 1, 11}, {Pane::Bank, 1, 12}};
            st.at(from[0]) = p1;
            st.at(from[1]) = p2;
            st.at(from[2]) = p3;

            std::string why;
            if (!st.moveGroup(from, to, why))
            {
                return "move into empty slots failed: " + why;
            }
            for (const SlotRef& r : from)
            {
                if (!st.at(r).empty())
                {
                    return "source " + refStr(r) + " not cleared";
                }
            }
            if (!sameSlot(st.at(to[0]), p1) || !sameSlot(st.at(to[1]), p2) ||
                !sameSlot(st.at(to[2]), p3))
            {
                return "targets hold wrong contents";
            }

            // Target occupied by a Pokemon outside the group: must fail and change nothing.
            const SlotRef blocker{Pane::Bank, 2, 4};
            st.at(blocker) = makeSlot(PkxFormat::PK3, 80, 0x99);
            const std::vector<SlotRef> to2 = {
                {Pane::Bank, 2, 3}, blocker, {Pane::Bank, 2, 5}};
            const std::vector<Slot> before = snapshot(bank);
            why.clear();
            if (st.moveGroup(to, to2, why))
            {
                return "move onto occupied slot succeeded";
            }
            if (auto e = compareSnapshots(before, snapshot(bank)); !e.empty())
            {
                return "failed move changed state: " + e;
            }

            // Mismatched sizes / empty group are refused.
            if (st.moveGroup(to, {to2[0]}, why) || st.moveGroup({}, {}, why))
            {
                return "malformed group accepted";
            }
            return "";
        }

        std::string testStorageShiftOverlap()
        {
            Bank bank;
            if (auto e = freshBank(bank); !e.empty())
            {
                return e;
            }
            Storage st(bank, nullptr);
            const Slot p0 = makeSlot(PkxFormat::PK1, 69, 0x12);
            const Slot p1 = makeSlot(PkxFormat::PK2, 73, 0x22);
            const Slot p2 = makeSlot(PkxFormat::PK6, 232, 0x62);
            st.at({Pane::Bank, 3, 0}) = p0;
            st.at({Pane::Bank, 3, 1}) = p1;
            st.at({Pane::Bank, 3, 2}) = p2;
            st.at({Pane::Bank, 7, 7}) = makeSlot(PkxFormat::PK3, 80, 0x33); // bystander

            const auto beforeSet = contents(bank);

            // Shift right by one: targets 1 and 2 are themselves sources.
            const std::vector<SlotRef> from = {
                {Pane::Bank, 3, 0}, {Pane::Bank, 3, 1}, {Pane::Bank, 3, 2}};
            const std::vector<SlotRef> to = {
                {Pane::Bank, 3, 1}, {Pane::Bank, 3, 2}, {Pane::Bank, 3, 3}};
            std::string why;
            if (!st.moveGroup(from, to, why))
            {
                return "right shift failed: " + why;
            }
            if (contents(bank) != beforeSet)
            {
                return "right shift lost or duplicated a Pokemon";
            }
            if (!st.at({Pane::Bank, 3, 0}).empty() || !sameSlot(st.at({Pane::Bank, 3, 1}), p0) ||
                !sameSlot(st.at({Pane::Bank, 3, 2}), p1) || !sameSlot(st.at({Pane::Bank, 3, 3}), p2))
            {
                return "right shift: wrong placement";
            }

            // Shift back left by one (targets overlap the other way) and reverse order.
            const std::vector<SlotRef> from2 = {
                {Pane::Bank, 3, 3}, {Pane::Bank, 3, 2}, {Pane::Bank, 3, 1}};
            const std::vector<SlotRef> to2 = {
                {Pane::Bank, 3, 2}, {Pane::Bank, 3, 1}, {Pane::Bank, 3, 0}};
            if (!st.moveGroup(from2, to2, why))
            {
                return "left shift failed: " + why;
            }
            if (contents(bank) != beforeSet)
            {
                return "left shift lost or duplicated a Pokemon";
            }
            if (!sameSlot(st.at({Pane::Bank, 3, 0}), p0) || !sameSlot(st.at({Pane::Bank, 3, 1}), p1) ||
                !sameSlot(st.at({Pane::Bank, 3, 2}), p2) || !st.at({Pane::Bank, 3, 3}).empty())
            {
                return "left shift: wrong placement";
            }

            // Rotation among the group's own slots.
            const std::vector<SlotRef> rot = {
                {Pane::Bank, 3, 1}, {Pane::Bank, 3, 2}, {Pane::Bank, 3, 0}};
            if (!st.moveGroup(from, rot, why))
            {
                return "rotation failed: " + why;
            }
            if (contents(bank) != beforeSet)
            {
                return "rotation lost or duplicated a Pokemon";
            }
            return "";
        }

        std::string testStorageFindEmpty()
        {
            Bank bank;
            if (auto e = freshBank(bank); !e.empty())
            {
                return e;
            }
            Storage st(bank, nullptr);
            const int last = st.boxCount(Pane::Bank) - 1;

            // Box 5 full except its last slot -> (5,29), (6,0), (6,1).
            for (int s = 0; s < BOX_SLOTS - 1; s++)
            {
                st.at({Pane::Bank, 5, s}) = makeSlot(PkxFormat::PK3, 80, (uint8_t)s);
            }
            auto got = st.findEmpty(Pane::Bank, 3, 5);
            const std::vector<SlotRef> want = {
                {Pane::Bank, 5, 29}, {Pane::Bank, 6, 0}, {Pane::Bank, 6, 1}};
            if (got != want)
            {
                std::string d = "start 5 got:";
                for (const SlotRef& r : got) d += " " + refStr(r);
                return d;
            }

            // Last box full, box 0 slot 0 corrupt-empty -> wraps to (0,1), (0,2).
            for (int s = 0; s < BOX_SLOTS; s++)
            {
                st.at({Pane::Bank, last, s}) = makeSlot(PkxFormat::PK4, 136, (uint8_t)s);
            }
            st.at({Pane::Bank, 0, 0}).flags = SLOT_CORRUPT; // empty but not writable
            got = st.findEmpty(Pane::Bank, 2, last);
            const std::vector<SlotRef> want2 = {{Pane::Bank, 0, 1}, {Pane::Bank, 0, 2}};
            if (got != want2)
            {
                std::string d = "wrap got:";
                for (const SlotRef& r : got) d += " " + refStr(r);
                return d;
            }

            // Large request: all distinct, all empty and writable, right count.
            const int total     = st.boxCount(Pane::Bank) * BOX_SLOTS;
            const int available = total - (BOX_SLOTS - 1) - BOX_SLOTS - 1;
            got = st.findEmpty(Pane::Bank, 40, 10);
            if ((int)got.size() != 40)
            {
                return "asked 40, got " + std::to_string(got.size());
            }
            got = st.findEmpty(Pane::Bank, total, 0);
            if ((int)got.size() != available)
            {
                return "asked all, got " + std::to_string(got.size()) + ", expected " +
                       std::to_string(available);
            }
            std::set<std::pair<int, int>> seen;
            for (const SlotRef& r : got)
            {
                if (r.pane != Pane::Bank || !seen.insert({r.box, r.slot}).second)
                {
                    return "duplicate or wrong-pane ref " + refStr(r);
                }
                const Slot& s = st.at(r);
                if (!s.empty() || !s.writable())
                {
                    return "returned non-empty/unwritable " + refStr(r);
                }
            }
            return "";
        }

        std::string testStorageCorrupt()
        {
            Bank bank;
            if (auto e = freshBank(bank); !e.empty())
            {
                return e;
            }
            Storage st(bank, nullptr);
            const SlotRef bad{Pane::Bank, 0, 0};
            const SlotRef good{Pane::Bank, 0, 1};
            const SlotRef empty{Pane::Bank, 0, 2};
            Slot corrupt = makeSlot(PkxFormat::PK5, 136, 0x55);
            corrupt.flags = SLOT_CORRUPT;
            st.at(bad)  = corrupt;
            st.at(good) = makeSlot(PkxFormat::PK6, 232, 0x66);

            const std::vector<Slot> before = snapshot(bank);
            std::string why;
            if (st.swap(bad, empty, why))
            {
                return "swap moved a corrupt slot";
            }
            if (st.swap(good, bad, why))
            {
                return "swap overwrote a corrupt slot";
            }
            if (st.moveGroup({bad}, {empty}, why))
            {
                return "moveGroup moved a corrupt slot";
            }
            if (st.moveGroup({good}, {bad}, why))
            {
                return "moveGroup overwrote a corrupt slot";
            }
            if (st.moveGroup({good, bad}, {empty, {Pane::Bank, 0, 3}}, why))
            {
                return "moveGroup moved a group containing a corrupt slot";
            }
            if (auto e = compareSnapshots(before, snapshot(bank)); !e.empty())
            {
                return "refused operation changed state: " + e;
            }
            return "";
        }

        std::string testValidLength()
        {
            struct Expect
            {
                PkxFormat f;
                std::set<size_t> lengths;
            };
            const std::vector<Expect> table = {
                {PkxFormat::Empty, {0}},
                {PkxFormat::PK1, {59, 69}},
                {PkxFormat::PK2, {63, 73}},
                {PkxFormat::PK3, {80}},
                {PkxFormat::PK4, {136}},
                {PkxFormat::PK5, {136}},
                {PkxFormat::PK6, {232}},
                {PkxFormat::PK7, {232}},
            };
            for (const Expect& e : table)
            {
                for (size_t len = 0; len <= 400; len++)
                {
                    const bool want = e.lengths.count(len) != 0;
                    if (validLength(e.f, len) != want)
                    {
                        return std::string(formatName(e.f)) + " length " + std::to_string(len) +
                               (want ? " rejected" : " accepted");
                    }
                }
            }
            if (validLength((PkxFormat)8, 232) || validLength((PkxFormat)0xFF, 0))
            {
                return "unknown format accepted";
            }
            return "";
        }

        void run(std::vector<Result>& out, const char* name, std::string (*fn)())
        {
            Result r{name, false, ""};
            try
            {
                r.detail = fn();
                r.pass   = r.detail.empty();
                if (r.pass)
                {
                    r.detail = "ok";
                }
            }
            catch (const std::exception& e)
            {
                r.detail = std::string("exception: ") + e.what();
            }
            catch (...)
            {
                r.detail = "unknown exception";
            }
            out.push_back(std::move(r));
        }
    }

    std::vector<Result> runAll()
    {
        std::vector<Result> out;
        try
        {
            cleanup();
        }
        catch (...)
        {
        }

        run(out, "fs::crc32 known vector", testCrc32);
        run(out, "fs::writeFileVerified round trip", testWriteVerified);
        run(out, "fs::writeFileAtomic v1->v2 + .bak", testWriteAtomic);
        run(out, "Bank serialize/deserialize round trip", testBankRoundTrip);
        run(out, "Bank deserialize rejects bad files", testBankRejects);
        run(out, "Storage swap", testStorageSwap);
        run(out, "Storage moveGroup + occupied target", testStorageMoveGroup);
        run(out, "Storage moveGroup overlapping shift", testStorageShiftOverlap);
        run(out, "Storage findEmpty order + wrap", testStorageFindEmpty);
        run(out, "Storage refuses corrupt slots", testStorageCorrupt);
        run(out, "validLength table", testValidLength);

        try
        {
            cleanup();
            if (fs::exists(TEST_DIR))
            {
                out.push_back({"selftest folder cleanup", false, TEST_DIR + " still exists"});
            }
        }
        catch (...)
        {
            out.push_back({"selftest folder cleanup", false, "exception"});
        }
        return out;
    }
}
