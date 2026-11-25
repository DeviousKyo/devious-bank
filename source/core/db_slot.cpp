#include "core/db_slot.hpp"

#include "pkx/PK1.hpp"
#include "pkx/PK2.hpp"
#include "pkx/PK3.hpp"
#include "pkx/PK4.hpp"
#include "pkx/PK5.hpp"
#include "pkx/PK6.hpp"
#include "pkx/PK7.hpp"

#include <algorithm>

namespace db
{
    const char* formatName(PkxFormat f)
    {
        switch (f)
        {
            case PkxFormat::PK1: return "PK1";
            case PkxFormat::PK2: return "PK2";
            case PkxFormat::PK3: return "PK3";
            case PkxFormat::PK4: return "PK4";
            case PkxFormat::PK5: return "PK5";
            case PkxFormat::PK6: return "PK6";
            case PkxFormat::PK7: return "PK7";
            case PkxFormat::Empty: break;
        }
        return "---";
    }

    bool formatFromGeneration(pksm::Generation g, PkxFormat& out)
    {
        if (g == pksm::Generation::ONE)        out = PkxFormat::PK1;
        else if (g == pksm::Generation::TWO)   out = PkxFormat::PK2;
        else if (g == pksm::Generation::THREE) out = PkxFormat::PK3;
        else if (g == pksm::Generation::FOUR)  out = PkxFormat::PK4;
        else if (g == pksm::Generation::FIVE)  out = PkxFormat::PK5;
        else if (g == pksm::Generation::SIX)   out = PkxFormat::PK6;
        else if (g == pksm::Generation::SEVEN) out = PkxFormat::PK7;
        else return false; // LGPE, Gen 8+: out of scope
        return true;
    }

    pksm::Generation formatToGeneration(PkxFormat f)
    {
        switch (f)
        {
            case PkxFormat::PK1: return pksm::Generation::ONE;
            case PkxFormat::PK2: return pksm::Generation::TWO;
            case PkxFormat::PK3: return pksm::Generation::THREE;
            case PkxFormat::PK4: return pksm::Generation::FOUR;
            case PkxFormat::PK5: return pksm::Generation::FIVE;
            case PkxFormat::PK6: return pksm::Generation::SIX;
            default:             return pksm::Generation::SEVEN;
        }
    }

    bool validLength(PkxFormat f, size_t length)
    {
        switch (f)
        {
            case PkxFormat::PK1:
                return length == pksm::PK1::JP_LENGTH_WITH_NAMES ||
                       length == pksm::PK1::INT_LENGTH_WITH_NAMES;
            case PkxFormat::PK2:
                return length == pksm::PK2::JP_LENGTH_WITH_NAMES ||
                       length == pksm::PK2::INT_LENGTH_WITH_NAMES;
            case PkxFormat::PK3: return length == pksm::PK3::BOX_LENGTH;
            case PkxFormat::PK4: return length == pksm::PK4::BOX_LENGTH;
            case PkxFormat::PK5: return length == pksm::PK5::BOX_LENGTH;
            case PkxFormat::PK6: return length == pksm::PK6::BOX_LENGTH;
            case PkxFormat::PK7: return length == pksm::PK7::BOX_LENGTH;
            case PkxFormat::Empty: return length == 0;
        }
        return false;
    }

    static size_t boxLength(PkxFormat f, size_t current)
    {
        switch (f)
        {
            case PkxFormat::PK3: return pksm::PK3::BOX_LENGTH;
            case PkxFormat::PK4: return pksm::PK4::BOX_LENGTH;
            case PkxFormat::PK5: return pksm::PK5::BOX_LENGTH;
            case PkxFormat::PK6: return pksm::PK6::BOX_LENGTH;
            case PkxFormat::PK7: return pksm::PK7::BOX_LENGTH;
            default:             return current; // Gen 1/2: keep the "with names" record whole
        }
    }

    std::unique_ptr<pksm::PKX> Slot::toPkx() const
    {
        if (empty() || !validLength(format, length))
        {
            return nullptr;
        }
        // getPKM copies the buffer (directAccess = false), so the slot is never touched.
        std::array<uint8_t, SLOT_DATA_MAX> copy = data;
        return pksm::PKX::getPKM(formatToGeneration(format), copy.data(), length, false);
    }

    // Species None alone doesn't prove a slot is empty: a damaged record can decode to
    // species 0 too, and treating it as empty would let it be overwritten. A slot counts as
    // genuinely empty only if:
    //   - its bytes are all zero. That is what PKSM-Core hands back for real empty slots:
    //     Gen 1/2 return emptyPkm() (zero-filled) for slots past the box's count; Gen 3+
    //     empty box data is zeros, or blank data encrypted with key 0, which the PKX
    //     constructor decrypts back to zeros (all-zero data is not "encrypted" for any
    //     isEncrypted(), so it stays zeros).
    //   - or (Gen 3+ only) its checksum is valid: an intact record whose species is 0 is a
    //     blank the game itself treats as empty, not a damaged Pokemon.
    // Anything else (Gen 1/2 listed entries with an invalid species, Gen 3+ species 0 with a
    // bad checksum) keeps its bytes and is flagged SLOT_CORRUPT.
    static bool genuinelyEmpty(const pksm::PKX& pk, PkxFormat f)
    {
        auto raw = pk.rawData();
        if (std::all_of(raw.begin(), raw.end(), [](uint8_t b) { return b == 0; }))
        {
            return true;
        }
        if (f >= PkxFormat::PK3)
        {
            auto check = pk.clone();
            check->refreshChecksum();
            return check->checksum() == pk.checksum();
        }
        return false;
    }

    Slot Slot::fromPkx(const pksm::PKX& pk)
    {
        Slot s;
        PkxFormat f;
        if (!formatFromGeneration(pk.generation(), f))
        {
            return s; // empty
        }
        const bool noSpecies = pk.species() == pksm::Species::None;
        if (noSpecies && genuinelyEmpty(pk, f))
        {
            return s; // empty
        }
        if (noSpecies)
        {
            s.flags |= SLOT_CORRUPT; // damaged record: kept as is, never overwritten
        }

        auto raw = pk.rawData();
        if (pk.isEncrypted())
        {
            // PKSM-Core decrypts on construction; refuse to store anything else.
            s.format = f;
            s.flags |= SLOT_CORRUPT;
        }

        size_t len = boxLength(f, raw.size());
        if (len > SLOT_DATA_MAX || len > raw.size())
        {
            s.format = f;
            s.flags |= SLOT_CORRUPT;
            len      = std::min({raw.size(), SLOT_DATA_MAX});
        }

        s.format = f;
        s.length = (uint16_t)len;
        std::copy_n(raw.begin(), len, s.data.begin());

        // Checksum check (Gen 3+): recompute on a clone and compare with the stored one.
        if (f >= PkxFormat::PK3)
        {
            auto check = pk.clone();
            check->refreshChecksum();
            if (check->checksum() != pk.checksum())
            {
                s.flags |= SLOT_CORRUPT;
            }
        }
        return s;
    }
}
