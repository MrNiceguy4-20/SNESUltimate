#include "bus.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static uint16_t read16(Bus& bus, uint16_t lo) {
    return static_cast<uint16_t>(bus.read(lo) | (static_cast<uint16_t>(bus.read(lo + 1)) << 8));
}
static void cycles(Bus& bus, int count) { while (count-- > 0) bus.completeCpuMachineCycle(); }
int main() {
    assert(Bus::MATH_MUL_CPU_CYCLES == 8);
    assert(Bus::MATH_DIV_CPU_CYCLES == 16);
    {
        Bus bus; bus.reset(); bus.setMasterClock(0);
        bus.write(0x4202, 0x80);
        bus.write(0x4203, 3);
        bus.setMasterClock(1000);
        assert(read16(bus, 0x4216) == 0);
        cycles(bus, 7);
        assert(read16(bus, 0x4216) == 0);
        cycles(bus, 1);
        assert(read16(bus, 0x4216) == 0x0180);
        assert(read16(bus, 0x4214) == 3);
    }
    {
        Bus bus; bus.reset();
        bus.write(0x4204, 0xE8); bus.write(0x4205, 0x03); bus.write(0x4206, 7);
        assert(read16(bus, 0x4216) == 1000);
        cycles(bus, 15);
        assert(read16(bus, 0x4214) != 142 || read16(bus, 0x4216) != 6);
        cycles(bus, 1);
        assert(read16(bus, 0x4214) == 142);
        assert(read16(bus, 0x4216) == 6);
    }
    {
        Bus bus; bus.reset();
        bus.write(0x4204, 0x34); bus.write(0x4205, 0x12); bus.write(0x4206, 0);
        cycles(bus, 16);
        assert(read16(bus, 0x4214) == 0xFFFF);
        assert(read16(bus, 0x4216) == 0x1234);
    }
    {
        Bus bus; bus.reset();
        bus.write(0x4202, 10); bus.write(0x4203, 20);
        cycles(bus, 2);
        bus.write(0x4204, 0xE8); bus.write(0x4205, 0x03);
        bus.write(0x4206, 7);
        assert(read16(bus, 0x4216) == 1000);
        cycles(bus, 6);
        assert(read16(bus, 0x4216) == 0x0488);
        assert(read16(bus, 0x4216) != 200);
    }
    {
        Bus bus; bus.reset();
        bus.write(0x4204, 0xE8); bus.write(0x4205, 0x03); bus.write(0x4206, 7);
        cycles(bus, 4);
        bus.write(0x4202, 9); bus.write(0x4203, 11);
        assert(read16(bus, 0x4216) == 0);
        cycles(bus, 12);
        assert(read16(bus, 0x4214) != 142 || read16(bus, 0x4216) != 6);
    }
    std::cout << "phase3 CPU-cycle-synchronous/overlap 5A22 math accuracy ok\n";
    return 0;
}
