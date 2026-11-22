#include "cpu_alu.hpp"
#include "cpu.hpp"
#include "bus.hpp"
#include "cartridge.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <random>
#include <vector>
namespace {
cpu_alu::Result refBinaryADC(uint16_t a, uint16_t b, bool c, bool wide) {
    const uint32_t mask=wide?0xFFFFu:0xFFu, sign=wide?0x8000u:0x80u;
    const uint32_t l=a&mask,r=b&mask,sum=l+r+(c?1u:0u),v=sum&mask;
    return {static_cast<uint16_t>(v),sum>mask,((~(l^r)&(l^v)&sign)!=0),(v&sign)!=0,v==0};
}
cpu_alu::Result refBinarySBC(uint16_t a, uint16_t b, bool c, bool wide) {
    const uint32_t mask=wide?0xFFFFu:0xFFu, sign=wide?0x8000u:0x80u;
    const int32_t d=static_cast<int32_t>(a&mask)-static_cast<int32_t>(b&mask)-(c?0:1);
    const uint32_t v=static_cast<uint32_t>(d)&mask,l=a&mask,r=b&mask;
    return {static_cast<uint16_t>(v),d>=0,(((l^r)&(l^v)&sign)!=0),(v&sign)!=0,v==0};
}
cpu_alu::Result refDecimalADC8(uint8_t a,uint8_t b,bool cin) {
    int lo=(a&15)+(b&15)+(cin?1:0); bool c=lo>9; if(c) lo=(lo-10)&15;
    int hi=(a>>4)+(b>>4)+(c?1:0); const bool pn=(hi&8)!=0; c=hi>9; if(c) hi=(hi-10)&15;
    const uint8_t v=static_cast<uint8_t>((hi<<4)|lo);
    const bool ov=((a>=0x80)^pn)&&((b>=0x80)^pn);
    return {v,c,ov,(v&0x80)!=0,v==0};
}
cpu_alu::Result refDecimalSBC8(uint8_t a,uint8_t b,bool cin) {
    bool borrow=!cin;
    uint8_t lo=static_cast<uint8_t>((a&15)-(b&15)-(borrow?1:0));
    borrow=lo>=0x80; if(borrow) lo=static_cast<uint8_t>(lo+10);
    const bool loStill=lo>=0x80; lo&=15;
    uint8_t hi=static_cast<uint8_t>((a>>4)-(b>>4)-(borrow?1:0));
    const bool pn=(hi&8)!=0; borrow=hi>=0x80; if(borrow) hi=static_cast<uint8_t>(hi+10);
    hi=static_cast<uint8_t>(hi-(loStill?1:0)); hi&=15;
    const uint8_t v=static_cast<uint8_t>((hi<<4)|lo);
    const bool ov=((a>=0x80)^pn)&&((b<0x80)^pn);
    return {v,!borrow,ov,(v&0x80)!=0,v==0};
}
bool same(const cpu_alu::Result& a,const cpu_alu::Result& b) {
    return a.value==b.value && a.carry==b.carry && a.overflow==b.overflow && a.negative==b.negative && a.zero==b.zero;
}
uint16_t makeValidBCD(std::mt19937& rng) {
    uint16_t v=0;
    for(unsigned i=0;i<4;++i) v|=static_cast<uint16_t>((rng()%10u)<<(i*4));
    return v;
}
uint32_t bcdToInt(uint16_t v) {
    return (v&15u)+10u*((v>>4)&15u)+100u*((v>>8)&15u)+1000u*((v>>12)&15u);
}
uint16_t intToBCD(uint32_t v) {
    v%=10000u; uint16_t out=0;
    for(unsigned i=0;i<4;++i){out|=static_cast<uint16_t>((v%10u)<<(i*4));v/=10u;}
    return out;
}
std::vector<uint8_t> makeRom(std::initializer_list<uint8_t> code) {
    std::vector<uint8_t> rom(0x10000,0xEA); size_t i=0; for(uint8_t b:code) rom[i++]=b;
    constexpr size_t h=0x7FC0; const char title[]="CPU ALU ACCURACY     ";
    for(size_t j=0;j<21;++j) rom[h+j]=static_cast<uint8_t>(title[j]);
    rom[h+0x15]=0x20; rom[h+0x17]=0x06; rom[h+0x19]=0x01; rom[0x7FFC]=0x00; rom[0x7FFD]=0x80;
    return rom;
}
struct Rig { Cartridge cart; Bus bus; CPU cpu; explicit Rig(std::vector<uint8_t> rom){
    assert(cart.loadData(std::move(rom),"alu.sfc","alu")); bus.attachCPU(&cpu); bus.attachCartridge(&cart); cpu.attachBus(&bus); bus.reset(); cpu.reset(); cpu.setCycleStepping(true);
}};
void runOne(CPU& cpu){cpu.step();while(cpu.hasPendingCycles())cpu.completeCycle();}
}
int main() {
    for(unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b) for(unsigned c=0;c<2;++c) {
        assert(same(cpu_alu::adc(static_cast<uint16_t>(a),static_cast<uint16_t>(b),c!=0,false,false),refBinaryADC(static_cast<uint16_t>(a),static_cast<uint16_t>(b),c!=0,false)));
        assert(same(cpu_alu::sbc(static_cast<uint16_t>(a),static_cast<uint16_t>(b),c!=0,false,false),refBinarySBC(static_cast<uint16_t>(a),static_cast<uint16_t>(b),c!=0,false)));
        assert(same(cpu_alu::adc(static_cast<uint16_t>(a),static_cast<uint16_t>(b),c!=0,false,true),refDecimalADC8(static_cast<uint8_t>(a),static_cast<uint8_t>(b),c!=0)));
        assert(same(cpu_alu::sbc(static_cast<uint16_t>(a),static_cast<uint16_t>(b),c!=0,false,true),refDecimalSBC8(static_cast<uint8_t>(a),static_cast<uint8_t>(b),c!=0)));
    }
    std::puts("8-bit ADC/SBC exhaustive binary+decimal PASS");
    std::mt19937 rng(0x65816u);
    for(unsigned i=0;i<250000;++i) {
        const uint16_t a=static_cast<uint16_t>(rng()), b=static_cast<uint16_t>(rng()); const bool c=(rng()&1u)!=0;
        assert(same(cpu_alu::adc(a,b,c,true,false),refBinaryADC(a,b,c,true)));
        assert(same(cpu_alu::sbc(a,b,c,true,false),refBinarySBC(a,b,c,true)));
    }
    for(unsigned i=0;i<100000;++i) {
        const uint16_t a=makeValidBCD(rng), b=makeValidBCD(rng); const bool c=(rng()&1u)!=0;
        const uint32_t ai=bcdToInt(a), bi=bcdToInt(b);
        const auto add=cpu_alu::adc(a,b,c,true,true);
        const uint32_t sum=ai+bi+(c?1u:0u);
        assert(add.value==intToBCD(sum)); assert(add.carry==(sum>=10000u)); assert(add.zero==(add.value==0)); assert(add.negative==((add.value&0x8000)!=0));
        const auto sub=cpu_alu::sbc(a,b,c,true,true);
        const int32_t diff=static_cast<int32_t>(ai)-static_cast<int32_t>(bi)-(c?0:1);
        const uint32_t wrapped=static_cast<uint32_t>((diff%10000+10000)%10000);
        assert(sub.value==intToBCD(wrapped)); assert(sub.carry==(diff>=0)); assert(sub.zero==(sub.value==0)); assert(sub.negative==((sub.value&0x8000)!=0));
    }
    std::puts("16-bit ADC/SBC binary+valid-BCD stress PASS");
    {
        const auto sub=cpu_alu::sbc(0xAB1D,0xF1E2,true,true,true);
        assert(sub.value==0x59DB);
        assert(!sub.carry && !sub.overflow && !sub.negative && !sub.zero);
    }
    {
        Rig r(makeRom({0xA9,0x40,0x85,0x10,0x24,0x10,0x89,0x80}));
        runOne(r.cpu); runOne(r.cpu); runOne(r.cpu);
        assert((r.cpu.p()&Flag_V)!=0 && (r.cpu.p()&Flag_N)==0 && (r.cpu.p()&Flag_Z)==0);
        const uint8_t before=static_cast<uint8_t>(r.cpu.p()&(Flag_N|Flag_V)); runOne(r.cpu);
        assert((r.cpu.p()&(Flag_N|Flag_V))==before); assert((r.cpu.p()&Flag_Z)!=0);
    }
    {
        Rig r(makeRom({0xA9,0x80,0xC9,0x80,0x0A,0x6A}));
        runOne(r.cpu); runOne(r.cpu);
        assert((r.cpu.p()&Flag_C)!=0 && (r.cpu.p()&Flag_Z)!=0 && (r.cpu.p()&Flag_N)==0);
        runOne(r.cpu);
        assert((r.cpu.a()&0xFF)==0 && (r.cpu.p()&Flag_C)!=0 && (r.cpu.p()&Flag_Z)!=0 && (r.cpu.p()&Flag_N)==0);
        runOne(r.cpu);
        assert((r.cpu.a()&0xFF)==0x80 && (r.cpu.p()&Flag_C)==0 && (r.cpu.p()&Flag_Z)==0 && (r.cpu.p()&Flag_N)!=0);
    }
    {
        const auto add=cpu_alu::adc(0x20,0x79,true,false,true);
        assert(add.value==0x00 && add.carry && add.overflow && add.zero);
        const auto sub=cpu_alu::sbc(0x00,0x80,true,false,true);
        assert(sub.value==0x20 && !sub.carry && sub.overflow && !sub.negative);
    }
    std::puts("cpu ALU/decimal accuracy regression ok");
    return 0;
}
