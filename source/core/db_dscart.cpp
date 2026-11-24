#include "core/db_dscart.hpp"
#include "core/db_fs.hpp"
#include "io/spi.hpp"

#include <3ds.h>
#include <cstring>

namespace db::dscart
{
    namespace
    {
        bool serviceUp = false;

        // First three characters of the game code; the fourth is the region.
        constexpr const char* POKEMON_GEN45[] = {
            "ADA", // Diamond
            "APA", // Pearl
            "CPU", // Platinum
            "IPK", // HeartGold
            "IPG", // SoulSilver
            "IRB", // Black
            "IRA", // White
            "IRE", // Black 2
            "IRD", // White 2
        };

        bool isPokemonGen45(const std::string& code)
        {
            for (const char* p : POKEMON_GEN45)
            {
                if (code.compare(0, 3, p) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        bool readGameCode(std::string& code)
        {
            bool inserted = false;
            if (R_FAILED(FSUSER_CardSlotIsInserted(&inserted)) || !inserted)
            {
                return false;
            }
            FS_CardType type;
            if (R_FAILED(FSUSER_GetCardType(&type)) || type != CARD_TWL)
            {
                return false; // empty slot or a 3DS cart
            }
            u8 header[0x3B4] = {};
            if (R_FAILED(FSUSER_GetLegacyRomHeader(MEDIATYPE_GAME_CARD, 0, header)))
            {
                return false;
            }
            code.assign((const char*)header + 0x0C, 4);
            return true;
        }
    }

    bool start()
    {
        serviceUp = R_SUCCEEDED(pxiDevInit());
        return serviceUp;
    }

    void stop()
    {
        if (serviceUp)
        {
            pxiDevExit();
            serviceUp = false;
        }
    }

    bool available()
    {
        return serviceUp;
    }

    bool detect(CartInfo& out, std::string& err)
    {
        if (!serviceUp)
        {
            err = "DS cart access unavailable (pxi:dev)";
            return false;
        }
        std::string code;
        if (!readGameCode(code))
        {
            err = "No DS cart inserted";
            return false;
        }
        if (!isPokemonGen45(code))
        {
            err = "DS cart " + code + " is not a Gen 4/5 Pokemon game";
            return false;
        }

        // Every Gen 4/5 game code starting with 'I' (HGSS, BW, B2W2) has infrared hardware.
        CardType type = NO_CHIP;
        if (R_FAILED(SPIGetCardType(&type, code[0] == 'I' ? 1 : 0)) || type == NO_CHIP)
        {
            err = "Could not identify the save chip on " + code;
            return false;
        }
        if (type < FLASH_256KB_1)
        {
            // Pokemon carts are all flash; an EEPROM answer means something is off.
            err = "Unexpected save chip type on " + code;
            return false;
        }

        out.gameCode = code;
        out.chipType = (int)type;
        out.size     = SPIGetCapacity(type);
        return true;
    }

    bool read(const CartInfo& cart, std::vector<uint8_t>& out, std::string& err)
    {
        if (cart.size == 0 || cart.size > 0x100000)
        {
            err = "Bad DS save size";
            return false;
        }
        out.assign(cart.size, 0);
        if (R_FAILED(SPIReadSaveData((CardType)cart.chipType, 0, out.data(), cart.size)))
        {
            err = "Reading the DS cart failed";
            return false;
        }
        return true;
    }

    bool write(const CartInfo& cart, const uint8_t* image, size_t len,
        const std::vector<uint8_t>& expected, std::string& err)
    {
        // 1. Same cart, same chip?
        CartInfo now;
        if (!detect(now, err))
        {
            err = "Cart check failed, nothing written: " + err;
            return false;
        }
        if (now.gameCode != cart.gameCode || now.chipType != cart.chipType ||
            now.size != cart.size || len != cart.size || expected.size() != len)
        {
            err = "A different cart is inserted; nothing written";
            return false;
        }

        // 2. Same save contents as when it was opened (and backed up)?
        std::vector<uint8_t> current;
        if (!read(cart, current, err))
        {
            return false;
        }
        if (current != expected)
        {
            err = "The cart's save changed since it was opened; nothing written";
            return false;
        }

        // 3. Write only the pages that differ, in contiguous runs.
        const CardType type = (CardType)cart.chipType;
        const u32 page      = SPIGetPageSize(type);
        for (u32 pos = 0; pos < len;)
        {
            if (std::memcmp(image + pos, current.data() + pos, page) == 0)
            {
                pos += page;
                continue;
            }
            u32 end = pos + page;
            while (end < len && std::memcmp(image + end, current.data() + end, page) != 0)
            {
                end += page;
            }
            if (R_FAILED(SPIWriteSaveData(type, pos, (void*)(image + pos), end - pos)))
            {
                err = "Writing the DS cart failed";
                return false;
            }
            pos = end;
        }

        // 4. Read everything back and compare.
        std::vector<uint8_t> check;
        if (!read(cart, check, err))
        {
            return false;
        }
        if (check.size() != len || std::memcmp(check.data(), image, len) != 0)
        {
            err = "DS cart verification failed after writing";
            return false;
        }
        return true;
    }
}
