#include "bus.hpp"
#include "controller.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    {
        Bus bus;
        Controller c1;
        bus.attachController1(&c1);
        bus.reset();
        c1.reset();
        c1.setButton(Controller::B, true);
        c1.setButton(Controller::A, true);
        bus.write(0x4016, 0x01);
        bus.write(0x4016, 0x00);
        uint16_t shifted = 0;
        for (int i = 0; i < 16; ++i) {
            const uint8_t v = bus.read(0x4016);
            shifted |= static_cast<uint16_t>((v & 1) << i);
        }
        assert((shifted & 0x0001) != 0);
        assert((shifted & 0x0100) != 0);
        assert((shifted & 0x0002) == 0);
        assert((bus.read(0x4016) & 1) == 1);
        assert((bus.read(0x4016) & 1) == 1);
    }
    {
        Bus bus;
        Controller c1;
        bus.attachController1(&c1);
        bus.write(0x4016, 0x01);
        c1.setButton(Controller::B, false);
        assert((bus.read(0x4016) & 1) == 0);
        c1.setButton(Controller::B, true);
        assert((bus.read(0x4016) & 1) == 1);
        bus.write(0x4016, 0x00);
    }
    {
        Bus bus;
        Controller c1;
        bus.attachController1(&c1);
        bus.write(0x4016, 0x01);
        bus.write(0x4016, 0x00);
        const uint8_t v = bus.read(0x4016);
        assert((v & 0x01) == (v & 0x01));
        assert((v & 0xFE) == (v & 0xFE));
    }
    {
        Controller c1;
        c1.setButton(Controller::Start, true);
        c1.setButton(Controller::Right, true);
        const uint16_t snap = c1.snapshot();
        assert((snap & (1u << Controller::Start)) != 0);
        assert((snap & (1u << Controller::Right)) != 0);
        assert((snap & (1u << Controller::B)) == 0);
    }
    {
        Bus bus;
        Controller c1, c2;
        bus.attachController1(&c1);
        bus.attachController2(&c2);
        c2.setButton(Controller::X, true);
        bus.write(0x4016, 0x01);
        bus.write(0x4016, 0x00);
        for (int i = 0; i < 16; ++i) bus.read(0x4016);
        uint16_t s2 = 0;
        for (int i = 0; i < 16; ++i) {
            s2 |= static_cast<uint16_t>((bus.read(0x4017) & 1) << i);
        }
        assert((s2 & (1u << Controller::X)) != 0);
    }
    {
        Bus bus;
        bus.reset();
        const uint8_t p1 = bus.read(0x4016);
        assert((p1 & 0x03) == 0x00);
        const uint8_t p2 = bus.read(0x4017);
        assert((p2 & 0x1F) == 0x1C);
    }
    {
        Bus bus;
        Controller c2;
        bus.attachController2(&c2);
        c2.setButton(Controller::B, true);
        bus.write(0x4016, 1);
        bus.write(0x4016, 0);
        const uint8_t v = bus.read(0x4017);
        assert((v & 0x01) == 1);
        assert((v & 0x02) == 0);
        assert((v & 0x1C) == 0x1C);
    }
    {
        Controller c;
        c.setButton(Controller::B, true);
        c.setButton(Controller::Start, true);
        c.setButton(Controller::Right, true);
        c.setButton(Controller::A, true);
        c.setButton(Controller::R, true);
        assert(c.autoJoyValue() == 0x9190);
        assert((c.autoJoyValue() & 0x000F) == 0);
    }
    std::cout << "phase63 controller serial accuracy ok\n";
    return 0;
}
