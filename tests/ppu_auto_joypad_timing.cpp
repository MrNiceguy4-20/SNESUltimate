#include "bus.hpp"
#include "controller.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
class DualLinePeripheral : public Controller {
public:
    DualLinePeripheral(uint16_t d0, uint16_t d1) : d0_(d0), d1_(d1) {}
    void setStrobe(bool high) override { if (high || (strobe_ && !high)) index_ = 0; strobe_ = high; }
    uint8_t readDataLines() override {
        const int bit = 15 - (strobe_ ? 0 : (index_ < 16 ? index_ : 15));
        const uint8_t lines = static_cast<uint8_t>(((d0_ >> bit) & 1u) | (((d1_ >> bit) & 1u) << 1));
        if (!strobe_ && index_ < 16) ++index_;
        return lines;
    }
private: uint16_t d0_, d1_; int index_ = 0; bool strobe_ = false;
};
static uint16_t joy(Bus& bus, uint16_t lo) {
    return static_cast<uint16_t>(bus.read(lo) | (static_cast<uint16_t>(bus.read(lo + 1)) << 8));
}
static void tick(PPU& ppu, Bus& bus) {
    ppu.tick();
    bus.updateVideoTiming(ppu.scanline(), ppu.dot(), ppu.hblank(), ppu.vblank());
}
static void advanceDots(PPU& ppu, Bus& bus, int count) { while (count-- > 0) tick(ppu, bus); }
int main() {
    {
        Bus bus; PPU ppu; Controller c1, c2;
        ppu.attachBus(&bus); bus.attachPPU(&ppu); bus.attachController1(&c1); bus.attachController2(&c2);
        c1.setButton(Controller::B, true); c1.setButton(Controller::A, true);
        c2.setButton(Controller::Start, true); c2.setButton(Controller::R, true);
        bus.write(0x4200, 0x01); ppu.writeRegister(0x2100, 0x0F);
        while (!(ppu.scanline() == ppu.vblankStartLine() && ppu.dot() == 0)) ppu.tick();
        bus.updateVideoTiming(ppu.scanline(), ppu.dot(), ppu.hblank(), true);
        while (ppu.dot() < 33) tick(ppu, bus);
        assert((bus.read(0x4212) & 1) != 0);
        assert(joy(bus,0x4218) == 0);
        advanceDots(ppu,bus,32);
        assert(joy(bus,0x4218) == 0);
        advanceDots(ppu,bus,32);
        assert(joy(bus,0x4218) == 1);
        assert(joy(bus,0x421A) == 0);
        while (joy(bus,0x4218) != 0x8080 || joy(bus,0x421A) != 0x1010) tick(ppu,bus);
        assert(joy(bus,0x421C) == 0 && joy(bus,0x421E) == 0);
        assert((bus.read(0x4212) & 1) != 0);
        while ((bus.read(0x4212) & 1) != 0) tick(ppu,bus);
        assert((bus.read(0x4016) & 3) == 1);
        assert((bus.read(0x4017) & 3) == 1);
    }
    {
        Bus bus; PPU ppu; DualLinePeripheral p1(0x8001,0xA55A), p2(0x1234,0x5AA5);
        ppu.attachBus(&bus); bus.attachPPU(&ppu); bus.attachController1(&p1); bus.attachController2(&p2);
        bus.write(0x4200,1);
        while (!(ppu.scanline()==ppu.vblankStartLine() && ppu.dot()==0)) ppu.tick();
        bus.updateVideoTiming(ppu.scanline(),ppu.dot(),ppu.hblank(),true);
        while (ppu.dot()<33) tick(ppu,bus);
        while ((bus.read(0x4212)&1)!=0) tick(ppu,bus);
        assert(joy(bus,0x4218)==0x8001); assert(joy(bus,0x421A)==0x1234);
        assert(joy(bus,0x421C)==0xA55A); assert(joy(bus,0x421E)==0x5AA5);
    }
    {
        Bus bus; PPU ppu; Controller c1;
        ppu.attachBus(&bus); bus.attachPPU(&ppu); bus.attachController1(&c1);
        c1.setButton(Controller::B, true);
        c1.setButton(Controller::Y, false);
        bus.write(0x4016, 1);
        bus.write(0x4200, 1);
        while (!(ppu.scanline()==ppu.vblankStartLine() && ppu.dot()==0)) ppu.tick();
        bus.updateVideoTiming(ppu.scanline(),ppu.dot(),ppu.hblank(),true);
        while (ppu.dot()<33) tick(ppu,bus);
        while ((bus.read(0x4212)&1)!=0) tick(ppu,bus);
        assert(joy(bus,0x4218)==0xFFFF);
        bus.write(0x4016, 0);
    }
    std::cout << "phase3 progressive/interference auto-joypad timing accuracy ok\n";
}
