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

    Slot Slot::fromPkx(const pksm::PKX& pk)
    {
        Slot s;
        PkxFormat f;
        if (pk.species() == pksm::Species::None || !formatFromGeneration(pk.generation(), f))
        {
            return s; // empty
        }

        auto raw = pk.rawData();
        if (pk.isEncrypted())
        {
            // PKSM-Core decrypts on construction; refuse to store anything else.
            s.format = f;
            s.flags  = SLOT_CORRUPT;
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
