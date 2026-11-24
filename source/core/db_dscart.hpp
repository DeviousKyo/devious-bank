// Gen 4/5 DS cartridges in the 3DS game card slot, read and written over SPI
// (source/io/spi.*, from PKSM / TWLSaveTool).
//
// Safety rules:
//   - Only carts whose game code is a Gen 4/5 Pokemon game are probed. Probing some
//     non-Pokemon chip types (EEPROM) writes a byte and restores it; Pokemon carts all use
//     flash, which is identified with read-only commands.
//   - Writes only touch the 256-byte pages that differ from what was last read.
//   - Before writing, the cart is re-identified and re-read; if it isn't byte-for-byte the
//     save that was opened (cart swapped, or changed elsewhere), nothing is written.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace db::dscart
{
    struct CartInfo
    {
        std::string gameCode; // 4 characters from the ROM header, e.g. "IPKE"
        int chipType  = -1;   // CardType from spi.hpp
        uint32_t size = 0;    // save chip capacity in bytes
    };

    // Starts/stops the PXIDEV service used for SPI. Safe to call stop() if start() failed.
    bool start();
    void stop();
    [[nodiscard]] bool available();

    // True if a DS cart with a Gen 4/5 Pokemon game code is inserted and its save chip was
    // identified. Never writes to the cart.
    bool detect(CartInfo& out, std::string& err);

    bool read(const CartInfo& cart, std::vector<uint8_t>& out, std::string& err);

    // The inserted cart's 32x32 icon as the system provides it: RGB565 in the GPU's
    // 8x8-tile Morton order, 2048 bytes.
    bool readIcon(std::vector<uint8_t>& out);

    // Writes `image` over a save whose current contents must equal `expected`.
    bool write(const CartInfo& cart, const uint8_t* image, size_t len,
        const std::vector<uint8_t>& expected, std::string& err);
}
