// On-console self-test for the core logic (fs helpers, bank file format, storage moves,
// slot length table). There is no desktop build, so these run on the 3DS itself.
#pragma once

#include <string>
#include <vector>

namespace db::selftest
{
    struct Result
    {
        std::string name;
        bool pass;
        std::string detail;
    };

    // Runs every test. Never throws (catch everything per test). Touches no user data:
    // file tests use only sdmc:/3ds/DB/selftest/ and delete it afterwards.
    std::vector<Result> runAll();
}
