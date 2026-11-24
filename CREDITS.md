# Credits

Devious Bank is licensed under the GNU GPL v3 (see `LICENSE`). It is not affiliated with
or endorsed by FlagBrew, the PKHeX project, Nintendo, Game Freak or Creatures Inc.

## PKSM-Core — `lib/PKSM-Core`
Copyright (C) 2016-2022 Bernardo Giordano, Admiral Fish, piepie62.
<https://github.com/FlagBrew/PKSM-Core> — GPLv3 with additional terms 7(b) and 7(c).

Its sources are used unmodified. Devious Bank only supplies its build configuration
(`include/PKSMCORE_CONFIG.h`) and copies its `strings/` and `personals/` data into `romfs/`.

PKSM-Core credits:
- @BernardoGiordano for the original PKSM editing code
- @Admiral-Fish and @piepie62 for the C++ rewrite
- @kwsch for PKHeX, from which most of the code is translated
- @SciresM for Pokémon format research and memecrypto
- @Epicpkmn11 for most of the Generation 3 work

## memecrypto — `lib/PKSM-Core/memecrypto`
By SciresM, FlagBrew fork: <https://github.com/FlagBrew/memecrypto> — GPLv3.

## pcg-cpp — `lib/PKSM-Core/pcg-cpp`
By Melissa O'Neill: <https://github.com/imneme/pcg-cpp> — MIT / Apache-2.0. Not compiled in.

## DS cartridge SPI access — `source/io/spi.hpp`, `source/io/spi.cpp`
From PKSM (<https://github.com/FlagBrew/PKSM>, `3ds/include/io/spi.hpp`,
`3ds/source/io/spi.cpp`), Copyright (C) 2016-2022 Bernardo Giordano, Admiral Fish,
piepie62 — GPLv3 with additional terms 7(b) and 7(c).
Based on TWLSaveTool, Copyright (C) 2015-2016 TuxSH (GPLv2+), and savegame-manager,
Copyright (C) Pokedoc 2010 (GPLv2+).

**Modified:** PKSM's logging calls are compiled to no-ops (its logging module is not part of
this project). The SPI logic is unchanged. Both files carry a "MODIFIED for Devious Bank"
notice.

## PKHeX
By kwsch and contributors: <https://github.com/kwsch/PKHeX> — GPLv3.
Format research behind PKSM-Core, and the Pokémon box sprites.

## Game icons — `assets/gameart/`, `romfs/gfx/gameart/`
From the SteamGridDB "Pokemon Games" icon collection
(<https://www.steamgriddb.com/collection/12146/icons>, pages 1 and 2), one consistent
uploader's set for Red through Ultra Moon, plus FireRed (icon 5489) from another uploader in
the same style. Black 2 uses an icon supplied by the project owner. Scaled to 84x84 by
`tools/convert_gameart.ps1`. Artwork is © its respective owners.

## Trademarks
Pokémon and all related names and images are trademarks and © of Nintendo, Game Freak and
Creatures Inc.
