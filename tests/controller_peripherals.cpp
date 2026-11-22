#include "bus.hpp"
#include "controller.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
static uint32_t readBits(Bus& bus, uint32_t addr, int count, uint8_t mask = 1) {
    uint32_t v = 0;
    for (int i = 0; i < count; ++i) v = (v << 1) | ((bus.read(addr) & mask) ? 1u : 0u);
    return v;
}
int main() {
    {
        Bus bus; Controller tap;
        bus.attachController2(&tap); bus.reset();
        tap.setDeviceType(Controller::DeviceType::Multitap);
        tap.setMultitapButton(0, Controller::B, true);
        tap.setMultitapButton(1, Controller::Y, true);
        tap.setMultitapButton(2, Controller::Start, true);
        tap.setMultitapButton(3, Controller::A, true);
        bus.write(0x4016, 1);
        assert((bus.read(0x4017) & 0x02) != 0);
        bus.write(0x4016, 0);
        bus.write(0x4201, 0x80);
        uint16_t p2=0,p3=0;
        for(int i=0;i<16;++i){ uint8_t x=bus.read(0x4017); p2|=(x&1u)<<i; p3|=((x>>1)&1u)<<i; }
        assert(p2 & (1u<<Controller::B));
        assert(p3 & (1u<<Controller::Y));
        assert((bus.read(0x4017)&3)==3);
        bus.write(0x4201, 0x00);
        uint16_t p4=0,p5=0;
        for(int i=0;i<16;++i){ uint8_t x=bus.read(0x4017); p4|=(x&1u)<<i; p5|=((x>>1)&1u)<<i; }
        assert(p4 & (1u<<Controller::Start));
        assert(p5 & (1u<<Controller::A));
    }
    {
        Bus bus; Controller mouse;
        bus.attachController1(&mouse); bus.reset();
        mouse.setDeviceType(Controller::DeviceType::Mouse);
        mouse.addMouseMotion(-5, 3);
        mouse.setMouseButton(Controller::MouseButton::Left, true);
        bus.write(0x4016,1); bus.write(0x4016,0);
        const uint32_t packet = readBits(bus,0x4016,32);
        assert(packet == 0x00410385u);
        assert((bus.read(0x4016)&1)==1);
        bus.write(0x4016,1); (void)bus.read(0x4016); bus.write(0x4016,0);
        assert(mouse.mouseSensitivity()==1);
        const uint32_t sensitivityPacket=readBits(bus,0x4016,32);
        assert(((sensitivityPacket>>20)&3u)==1u);
    }
    {
        Bus bus; Controller scope;
        bus.attachController2(&scope); bus.reset();
        scope.setDeviceType(Controller::DeviceType::SuperScope);
        scope.setSuperScopeAim(40,30,true);
        assert(scope.lightGunLatchAt(27, 72));
        assert(!scope.lightGunLatchAt(27, 71));
        scope.setSuperScopeButton(Controller::ScopeButton::Fire,true);
        scope.setSuperScopeButton(Controller::ScopeButton::Cursor,true);
        bus.write(0x4016,1); bus.write(0x4016,0);
        const uint16_t packet=static_cast<uint16_t>(readBits(bus,0x4017,16));
        assert((packet & 0xC000)==0xC000);
        assert((packet & 0x00FF)==0x00FF);
        assert((bus.read(0x4017)&1)==1);
    }
    std::cout << "phase6 controller peripherals accuracy ok\n";
}
