#include "snes.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    assert(SNES::DRAM_REFRESH_MASTER_CLOCKS == 40);
    {
        SNES snes;
        snes.reset();
        assert(snes.dramRefreshCount() == 0);
        snes.runCycles(537);
        assert(snes.dramRefreshCount() == 0);
        snes.runCycles(1);
        assert(snes.dramRefreshCount() == 1);
        snes.runCycles(1359);
        assert(snes.dramRefreshCount() == 1);
        snes.runCycles(1);
        assert(snes.dramRefreshCount() == 2);
        snes.runCycles(1367);
        assert(snes.dramRefreshCount() == 2);
        snes.runCycles(1);
        assert(snes.dramRefreshCount() == 3);
    }
    {
        SNES snes;
        snes.reset();
        const uint64_t before = snes.dramRefreshCount();
        snes.runFrame();
        const uint64_t delta = snes.dramRefreshCount() - before;
        assert(delta >= 260 && delta <= 264);
    }
    std::cout << "phase19 sub-dot DRAM refresh timing accuracy ok\n";
    return 0;
}
