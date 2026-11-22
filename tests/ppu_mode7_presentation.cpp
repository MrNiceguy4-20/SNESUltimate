#include "bus.hpp"
#include "ppu.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
static uint16_t colorFor(int index) {
    return static_cast<uint16_t>((index * 37 + 5) & 0x7FFF);
}
static uint32_t expectedRgba(uint16_t bgr555) {
    const uint32_t r5 = bgr555 & 0x1F;
    const uint32_t g5 = (bgr555 >> 5) & 0x1F;
    const uint32_t b5 = (bgr555 >> 10) & 0x1F;
    return ((r5 * 255u + 15u) / 31u) | (((g5 * 255u + 15u) / 31u) << 8) |
           (((b5 * 255u + 15u) / 31u) << 16) | 0xFF000000u;
}
static void writeM7(PPU& ppu, uint16_t reg, uint16_t value) {
    ppu.writeRegister(reg, static_cast<uint8_t>(value));
    ppu.writeRegister(reg, static_cast<uint8_t>(value >> 8));
}
struct Rig {
    Bus bus;
    PPU ppu;
    Rig(uint8_t bgMode, uint8_t setini, bool widescreen, int hdScale) {
        ppu.attachBus(&bus);
        bus.attachPPU(&ppu);
        ppu.setMode7Widescreen(widescreen);
        ppu.setMode7HdScale(hdScale);
        ppu.writeRegister(0x2100, 0x80);
        ppu.writeRegister(0x2105, bgMode);
        ppu.writeRegister(0x2133, setini);
        ppu.writeRegister(0x212C, bgMode == 7 ? 0x01 : 0x00);
        ppu.writeRegister(0x212D, 0x00);
        writeM7(ppu, 0x211B, 0x0100);
        writeM7(ppu, 0x211C, 0x0000);
        writeM7(ppu, 0x211D, 0x0000);
        writeM7(ppu, 0x211E, 0x0100);
        writeM7(ppu, 0x211F, 0x0000);
        writeM7(ppu, 0x2120, 0x0000);
        writeM7(ppu, 0x210D, 0x0000);
        writeM7(ppu, 0x210E, 0x0000);
        ppu.writeRegister(0x2121, 0);
        for (int i = 0; i < 256; ++i) {
            const uint16_t c = colorFor(i);
            ppu.writeRegister(0x2122, static_cast<uint8_t>(c & 0xFF));
            ppu.writeRegister(0x2122, static_cast<uint8_t>(c >> 8));
        }
        ppu.writeRegister(0x2115, 0x80);
        ppu.writeRegister(0x2116, 0x00);
        ppu.writeRegister(0x2117, 0x00);
        for (int w = 0; w < 16384; ++w) {
            ppu.writeRegister(0x2118, static_cast<uint8_t>(w & 127));
            ppu.writeRegister(0x2119, static_cast<uint8_t>(((w >> 6) & 0xFF) + 1));
        }
        ppu.writeRegister(0x2100, 0x0F);
    }
    void runFrame() {
        const uint64_t target = ppu.frameCounter() + 1;
        while (ppu.frameCounter() < target) ppu.tick();
    }
};
static uint32_t at(const PPU& ppu, int x, int row) {
    return ppu.framebuffer()[static_cast<size_t>(row) * ppu.framebufferStride() + x];
}
int main() {
    {
        auto off = std::make_unique<Rig>(7, 0x00, false, 1);
        off->runFrame();
        off->runFrame();
        assert(!off->ppu.presentationActive());
        assert(off->ppu.width() == 256 && off->ppu.height() == 224 && off->ppu.framebufferStride() == 512);
        assert(off->ppu.logicalWidth() == 256);
        assert(at(off->ppu, 100, 99) == expectedRgba(colorFor(13)));
        auto wide = std::make_unique<Rig>(7, 0x00, true, 1);
        wide->runFrame();
        wide->runFrame();
        wide->runFrame();
        const PPU& p = wide->ppu;
        const int pad = PPU::WIDESCREEN_PAD;
        assert(p.presentationActive());
        assert(p.width() == 256 + 2 * pad && p.height() == 224 && p.framebufferStride() == p.width());
        assert(p.logicalWidth() == p.width() && p.logicalHeight() == 224);
        for (int row = 0; row < 224; ++row)
            for (int x = 0; x < 256; ++x)
                assert(at(p, pad + x, row) == at(off->ppu, x, row));
        for (int row = 0; row < 224; row += 37) {
            for (int px = 0; px < pad; ++px) {
                const int x = px - pad;
                const int col = (x >> 3) & 127;
                assert(at(p, px, row) == expectedRgba(colorFor(col + 1)));
            }
            for (int px = 0; px < pad; ++px) {
                const int x = 256 + px;
                const int col = (x >> 3) & 127;
                assert(at(p, pad + 256 + px, row) == expectedRgba(colorFor(col + 1)));
            }
        }
        wide->ppu.setMode7Widescreen(false);
        assert(!wide->ppu.presentationActive());
        assert(wide->ppu.width() == 256 && wide->ppu.framebufferStride() == 512);
    }
    {
        auto wide = std::make_unique<Rig>(1, 0x00, true, 1);
        wide->runFrame();
        const PPU& p = wide->ppu;
        const int pad = PPU::WIDESCREEN_PAD;
        assert(p.width() == 256 + 2 * pad);
        for (int row = 0; row < 224; row += 50) {
            for (int px = 0; px < pad; ++px) {
                assert(at(p, px, row) == expectedRgba(colorFor(0)));
                assert(at(p, pad + 256 + px, row) == expectedRgba(colorFor(0)));
            }
            assert(at(p, pad + 10, row) == expectedRgba(colorFor(0)));
        }
    }
    {
        auto tiles = std::make_unique<Rig>(1, 0x00, true, 1);
        PPU& q = tiles->ppu;
        q.writeRegister(0x2100, 0x80);
        q.writeRegister(0x2115, 0x80);
        q.writeRegister(0x2116, 0x00);
        q.writeRegister(0x2117, 0x00);
        for (int w = 0; w < 0x800; ++w) {
            q.writeRegister(0x2118, static_cast<uint8_t>(w < 8 ? 0xFF : 0x00));
            q.writeRegister(0x2119, 0x00);
        }
        q.writeRegister(0x2107, 0x04);
        q.writeRegister(0x210B, 0x00);
        q.writeRegister(0x212C, 0x01);
        q.writeRegister(0x2100, 0x0F);
        tiles->runFrame();
        tiles->runFrame();
        const int pad = PPU::WIDESCREEN_PAD;
        assert(q.width() == 256 + 2 * pad);
        for (int row = 20; row < 200; row += 60) {
            for (int px = 0; px < pad; ++px) {
                assert(at(q, px, row) == expectedRgba(colorFor(1)));
                assert(at(q, pad + 256 + px, row) == expectedRgba(colorFor(1)));
            }
            assert(at(q, pad, row) == expectedRgba(colorFor(1)));
        }
    }
    {
        auto hd = std::make_unique<Rig>(7, 0x00, false, 3);
        hd->runFrame();
        const PPU& p = hd->ppu;
        assert(p.presentationActive());
        assert(p.width() == 768 && p.height() == 672 && p.framebufferStride() == 768);
        assert(p.logicalWidth() == 256 && p.logicalHeight() == 224);
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i)
                assert(at(p, 99 * 3 + i, 99 * 3 + j) == expectedRgba(colorFor(13)));
        auto hd2 = std::make_unique<Rig>(7, 0x00, false, 2);
        hd2->runFrame();
        const PPU& q = hd2->ppu;
        assert(q.width() == 512 && q.height() == 448);
        for (int j = 0; j < 2; ++j) {
            assert(at(q, 96 * 2 + 0, 99 * 2 + j) == expectedRgba(colorFor(12)));
            assert(at(q, 96 * 2 + 1, 99 * 2 + j) == expectedRgba(colorFor(13)));
        }
        auto off = std::make_unique<Rig>(7, 0x00, false, 1);
        off->runFrame();
        for (int row = 0; row < 224; row += 11)
            for (int x = 0; x < 256; x += 8)
                assert(at(q, x * 2 + 1, row * 2 + 1) == at(off->ppu, x, row) ||
                       at(q, x * 2, row * 2) == at(off->ppu, x, row));
    }
    {
        auto both = std::make_unique<Rig>(7, 0x00, true, 2);
        both->runFrame();
        const PPU& p = both->ppu;
        const int pad = PPU::WIDESCREEN_PAD;
        assert(p.width() == (256 + 2 * pad) * 2 && p.height() == 448);
        const int x = -5;
        const uint32_t expected = expectedRgba(colorFor(((x >> 3) & 127) + 1));
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
                assert(at(p, (pad + x) * 2 + i, 100 * 2 + j) == expected);
    }
    {
        auto hires = std::make_unique<Rig>(7, 0x08, true, 1);
        hires->runFrame();
        const PPU& p = hires->ppu;
        const int pad = PPU::WIDESCREEN_PAD;
        assert(p.width() == 512 + 4 * pad);
        const uint32_t expected = expectedRgba(colorFor(((-1 >> 3) & 127) + 1));
        assert(at(p, pad * 2 - 1, 60) == expected && at(p, pad * 2 - 2, 60) == expected);
    }
    std::cout << "ppu mode 7 presentation ok\n";
    return 0;
}
