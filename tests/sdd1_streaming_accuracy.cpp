#include "cartridge.hpp"
#include "cartridge_device.hpp"
#include "sdd1.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
namespace {
int failures = 0;
void expect(bool ok, const char* msg) { if (!ok) { std::cerr << "FAIL: " << msg << '\n'; ++failures; } }
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(4 * 1024 * 1024);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = uint8_t((i * 29 + 7) & 0xff);
    rom[0] = 0x30;
    rom[1] = 0xA6;
    rom[0x4802] = 0x19;
    rom[0x4803] = 0xE7;
    rom[0x4808] = 0x4D;
    rom[0x480F] = 0xB2;
    return rom;
}
void programDma(CartridgeDevice& d, int ch, uint32_t address, uint16_t size) {
    d.dmaRegisterWrite(ch, 2, uint8_t(address));
    d.dmaRegisterWrite(ch, 3, uint8_t(address >> 8));
    d.dmaRegisterWrite(ch, 4, uint8_t(address >> 16));
    d.dmaRegisterWrite(ch, 5, uint8_t(size));
    d.dmaRegisterWrite(ch, 6, uint8_t(size >> 8));
}
std::vector<uint8_t> referenceBytes(const std::vector<uint8_t>& rom) {
    std::vector<uint8_t> ram(0x8000); bool dirty = false;
    auto d = createSDD1Device(rom, ram, dirty);
    programDma(*d, 0, 0xC00000, 8);
    d->write(0x004800, 1);
    d->write(0x004801, 1);
    d->clock(200);
    d->dmaBegin(0, 0xC00000, 8, 0x08);
    std::vector<uint8_t> out(8);
    for (auto& v : out) expect(d->dmaRead(0, 0xC00000, v), "reference transformed DMA byte available");
    return out;
}
}
int main() {
    const auto rom = makeRom();
    const auto ref = referenceBytes(rom);
    std::vector<uint8_t> ram(0x8000); bool dirty = false;
    auto d = createSDD1Device(rom, ram, dirty);
    uint8_t v = 0;
    expect(d->read(0x004802, v) && v == 0x19, "$4802 register hole falls through to ROM");
    expect(d->read(0x804803, v) && v == 0xE7, "$4803 mirror falls through to ROM");
    expect(d->read(0x004808, v) && v == 0x4D, "$4808 register hole falls through to ROM");
    expect(d->read(0x80480F, v) && v == 0xB2, "$480F mirror falls through to ROM");
    programDma(*d, 0, 0xC00000, 8);
    d->write(0x004800, 0x02);
    d->write(0x004801, 0x02);
    d->clock(200);
    d->dmaBegin(0, 0xC00000, 8, 0x08);
    expect(!d->dmaRead(0, 0xC00000, v), "non-selected DMA channel is not decompressed");
    d->dmaEnd(0);
    expect(d->read(0x004801, v) && v == 0x02, "non-decompression DMA does not auto-clear $4801");
    d->reset();
    programDma(*d, 0, 0xC00000, 5);
    d->write(0x004800, 1);
    d->write(0x004801, 1);
    d->clock(30);
    d->dmaBegin(0, 0xC00000, 5, 0x08);
    uint8_t early = 0xFF;
    expect(d->dmaRead(0, 0xC00000, early), "early selected DMA beat is owned by S-DD1");
    d->clock(1);
    uint8_t b0=0,b1=0;
    expect(d->dmaRead(0, 0xC00000, b0) && b0 == ref[0], "first decoded byte appears at measured 31-clock boundary");
    expect(d->dmaRead(0, 0xC00000, b1) && b1 == ref[1], "paired second decoded byte is ready with first planar row");
    d->clock(16);
    uint8_t b2=0,b3=0;
    expect(d->dmaRead(0, 0xC00000, b2) && b2 == ref[2], "next planar pair appears 16 clocks later");
    expect(d->dmaRead(0, 0xC00000, b3) && b3 == ref[3], "second byte of next planar pair is ready together");
    expect(d->read(0x004801, v) && v == 0, "selected channel auto-clears after snooped DMA length expires");
    d->reset();
    programDma(*d, 0, 0xC00000, 4);
    d->write(0x004800, 1); d->write(0x004801, 1); d->clock(31);
    d->dmaBegin(0, 0xC00000, 4, 0x08);
    uint8_t first=0; expect(d->dmaRead(0, 0xC00000, first) && first == ref[0], "checkpoint setup consumes first produced byte");
    const auto state = d->saveState();
    uint8_t second=0; expect(d->dmaRead(0, 0xC00000, second) && second == ref[1], "checkpoint reference second byte");
    d->clock(64);
    expect(d->loadState(state), "S-DD1 restores in-flight streaming state");
    uint8_t replay=0; expect(d->dmaRead(0, 0xC00000, replay) && replay == second, "restored FIFO position/timing replays byte exactly");
    if (failures) { std::cerr << failures << " S-DD1 streaming regressions failed\n"; return 1; }
    std::cout << "phase31 S-DD1 streaming/timing accuracy ok\n";
    return 0;
}
