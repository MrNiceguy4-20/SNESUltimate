#include "cartridge.hpp"
#include "bus.hpp"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace {
int fails=0;
void expect(bool c,const char* m){if(!c){std::cerr<<"FAIL: "<<m<<'\n';++fails;}}
void w16(std::vector<uint8_t>& r,std::size_t o,uint16_t v){r[o]=uint8_t(v);r[o+1]=uint8_t(v>>8);}
std::vector<uint8_t> dspRom(){std::vector<uint8_t> r(2*1024*1024,0);auto h=std::size_t(0x7fc0);for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x20;r[h+0x16]=0x03;r[h+0x17]=0x0b;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}
std::vector<uint8_t> pilotwingsRom(){auto r=dspRom();auto h=std::size_t(0x7fc0);const char* t="PILOTWINGS";for(std::size_t i=0;t[i];++i)r[h+i]=uint8_t(t[i]);return r;}
std::vector<uint8_t> stRom(){std::vector<uint8_t> r(1024*1024,0);auto h=std::size_t(0x7fc0);for(int i=0;i<21;i++)r[h+i]=' ';r[h+0x15]=0x30;r[h+0x16]=0xf6;r[h+0x17]=8;r[h+0x18]=3;r[h+0x19]=1;w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;}
std::vector<uint8_t> dspVariantRom(uint8_t mapMode,uint8_t romType,uint8_t company=1,uint8_t sizeExp=0x0b){
 std::vector<uint8_t> r(2*1024*1024,0);auto h=std::size_t(0x7fc0);for(int i=0;i<21;i++)r[h+i]=' ';
 r[h+0x15]=mapMode;r[h+0x16]=romType;r[h+0x17]=sizeExp;r[h+0x19]=1;r[h+0x1a]=company;
 w16(r,h+0x1c,0xedcb);w16(r,h+0x1e,0x1234);w16(r,h+0x3c,0x8000);return r;
}
uint32_t loi(uint16_t value,uint8_t dest){return 0xc00000u|(uint32_t(value)<<6)|dest;}
uint32_t jump(uint16_t branch,uint16_t target,uint8_t bank=0){return 0x800000u|(uint32_t(branch&0x1ff)<<13)|(uint32_t(target&0x7ff)<<2)|(bank&3);}
constexpr uint32_t ret(){return 0x400000u;}
void op(std::vector<uint8_t>& p,std::size_t i,uint32_t v){p[i*3]=uint8_t(v);p[i*3+1]=uint8_t(v>>8);p[i*3+2]=uint8_t(v>>16);}
void save(const std::filesystem::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));}
}
int main(){
#if defined(_WIN32)
 _putenv_s("SNESULTIMATE_ALLOW_UNVERIFIED_FIRMWARE", "1");
#else
 setenv("SNESULTIMATE_ALLOW_UNVERIFIED_FIRMWARE", "1", 1);
#endif
 const auto dir=std::filesystem::temp_directory_path()/"snesultimate-phase7-necdsp";
 std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);
 const auto upperRoot=std::filesystem::temp_directory_path()/"snesultimate-phase1-uppercase";
 std::filesystem::remove_all(upperRoot);std::filesystem::create_directories(upperRoot/"Firmware");
 std::vector<uint8_t> upperProgram(0x1800,0),upperData(0x800,0);
 op(upperProgram,0,loi(0x0066,6));
 save(upperRoot/"Firmware"/"dsp1b.program.rom",upperProgram);
 save(upperRoot/"Firmware"/"dsp1b.data.rom",upperData);
 Cartridge upper;upper.setSRAMPersistence(false);
 expect(upper.loadData(dspRom(),(upperRoot/"game.sfc").string()),"load DSP1 with uppercase packaged Firmware directory");
 expect(upper.hardwareSupportLevel()==CartridgeSupportLevel::FirmwareLLE,"uppercase Firmware directory selects LLE on case-sensitive filesystems");
 Bus upperBus;upperBus.attachCartridge(&upper);upper.clockHardware(20);
 expect(upperBus.read(0x600000)==0x66,"uppercase Firmware directory executes discovered firmware");
 std::filesystem::remove_all(upperRoot);
 std::vector<uint8_t> program(0x1800,0),data(0x800,0);
 op(program,0,loi(0x0400,7));
 op(program,1,loi(0x0003,1));
 op(program,2,loi(0x0004,2));
 op(program,3,(1u<<20)|(5u<<16)|(2u<<4));
 op(program,4,(1u<<4)|6u);
 save(dir/"dsp1b.program.rom",program);save(dir/"dsp1b.data.rom",data);
 Cartridge c;c.setSRAMPersistence(false);expect(c.loadData(dspRom(),(dir/"game.sfc").string()),"load firmware-backed DSP1");
 expect(c.hardwareType()==CartridgeHardware::DSP1,"DSP1 detected");
 expect(c.hardwareSupportLevel()==CartridgeSupportLevel::FirmwareLLE,"firmware selects uPD low-level core");
 expect(c.hardwareNeedsClock(),"uPD core runs asynchronously");
 Bus b;b.attachCartridge(&c);
 expect((b.read(0x604000)&0x80)==0,"RQM low before firmware executes");
 c.clockHardware(20);
 expect((b.read(0x604000)&0x84)==0x84,"firmware controls RQM and DRC status bits");
 const auto state=c.saveHardwareState();
 expect(b.read(0x600000)==0x07,"DR exposes ALU-produced byte");
 expect((b.read(0x604000)&0x80)==0,"host DR access clears RQM");
 expect(c.loadHardwareState(c.hardwareStateTag(),state),"uPD state round trip");
 expect(b.read(0x600000)==0x07,"restored RQM/DR state is deterministic");
 std::vector<uint8_t> dsp1Program(0x1800,0), dsp1bProgram(0x1800,0);
 op(dsp1Program,0,loi(0x0011,6));
 op(dsp1bProgram,0,loi(0x0022,6));
 save(dir/"dsp1.program.rom",dsp1Program); save(dir/"dsp1.data.rom",data);
 save(dir/"dsp1b.program.rom",dsp1bProgram); save(dir/"dsp1b.data.rom",data);
 Cartridge pw; pw.setSRAMPersistence(false);
 expect(pw.loadData(pilotwingsRom(),(dir/"pilotwings.sfc").string()),"load Pilotwings with both DSP1 revisions available");
 expect(pw.hardwareSupportLevel()==CartridgeSupportLevel::FirmwareLLE,"Pilotwings selects firmware LLE when DSP1 firmware exists");
 Bus pwb; pwb.attachCartridge(&pw); pw.clockHardware(20);
 expect(pwb.read(0x600000)==0x11,"Pilotwings selects original DSP1 firmware instead of DSP1B");
 std::filesystem::remove(dir/"dsp1.program.rom");
 std::filesystem::remove(dir/"dsp1.data.rom");
 std::vector<uint8_t> mergedDsp1=dsp1Program; mergedDsp1.insert(mergedDsp1.end(),data.begin(),data.end());
 save(dir/"[BIOS] DSP1 (World).bin",mergedDsp1);
 Cartridge pwBin; pwBin.setSRAMPersistence(false);
 expect(pwBin.loadData(pilotwingsRom(),(dir/"pilotwings-bin.sfc").string()),"load Pilotwings with preservation-named merged DSP1 firmware");
 expect(pwBin.hardwareSupportLevel()==CartridgeSupportLevel::FirmwareLLE,"preservation-named merged DSP1 firmware selects LLE");
 Bus pwbb; pwbb.attachCartridge(&pwBin); pwBin.clockHardware(20);
 expect(pwbb.read(0x600000)==0x11,"merged descriptive DSP1 firmware is identified by content, not filename");
 std::filesystem::remove(dir/"[BIOS] DSP1 (World).bin");
 program.assign(0x1800,0); data.assign(0x800,0);
 op(program,0,loi(0x0400,7));
 op(program,1,loi(0x0003,1));
 op(program,2,loi(0x0004,2));
 op(program,3,(1u<<20)|(5u<<16)|(2u<<4));
 op(program,4,(1u<<4)|6u);
 save(dir/"dsp1b.program.rom",program); save(dir/"dsp1b.data.rom",data);
 auto legacyState=state;
 if(legacyState.size()>=36){
     const std::size_t countOff=legacyState.size()-36;
     legacyState[countOff+0]=4; legacyState[countOff+1]=legacyState[countOff+2]=legacyState[countOff+3]=0;
     legacyState.erase(legacyState.begin()+std::ptrdiff_t(countOff+4+4*2),legacyState.end());
 }
 expect(c.loadHardwareState(c.hardwareStateTag(),legacyState),"legacy four-entry uPD7725 stack state migrates to 16 entries");
 std::fill(program.begin(),program.end(),0);
 for(unsigned depth=0;depth<10;++depth){
     const std::size_t call=depth*2;
     op(program,call,jump(0x140,uint16_t(call+2)));
     op(program,call+1,ret());
 }
 op(program,20,ret());
 op(program,1,loi(0x005a,6));
 save(dir/"dsp1b.program.rom",program);
 Cartridge nested;nested.setSRAMPersistence(false);expect(nested.loadData(dspRom(),(dir/"nested.sfc").string()),"load nested-call DSP1 firmware");
 Bus nb;nb.attachCartridge(&nested);nested.clockHardware(2000);
 expect((nb.read(0x604000)&0x80)!=0,"ten-level uPD7725 call chain returns with RQM asserted");
 expect(nb.read(0x600000)==0x5a,"uPD7725 preserves return addresses beyond four nested calls");
 std::fill(program.begin(),program.end(),0);
 op(program,0,loi(0x01ff,4));
 op(program,1,(4u<<4)|6u);
 save(dir/"dsp1b.program.rom",program);
 Cartridge widths;widths.setSRAMPersistence(false);expect(widths.loadData(dspRom(),(dir/"widths.sfc").string()),"load register-width DSP1 firmware");
 Bus wb;wb.attachCartridge(&widths);widths.clockHardware(100);
 const uint8_t dpLo=wb.read(0x600000),dpHi=wb.read(0x600000);
 expect(dpLo==0xff&&dpHi==0x00,"uPD7725 DP is masked to its 8-bit architectural width");
 std::fill(program.begin(),program.end(),0);
 op(program,0,loi(0x1234,6));
 op(program,1,loi(0x0400,7));
 save(dir/"dsp1b.program.rom",program);
 Cartridge statusMask;statusMask.setSRAMPersistence(false);expect(statusMask.loadData(dspRom(),(dir/"statusmask.sfc").string()),"load status-mask DSP1 firmware");
 Bus smb;smb.attachCartridge(&statusMask);statusMask.clockHardware(3);
 expect(smb.read(0x600000)==0x34,"first 16-bit DR byte establishes DRS half-transfer latch");
 statusMask.clockHardware(4);
 const uint8_t maskedSr=smb.read(0x604000);
 expect((maskedSr&0x04)!=0&&((maskedSr&0x10)==0),"DRS is suppressed from status when DRC selects 8-bit mode");
 std::fill(program.begin(),program.end(),0); op(program,0,loi(0x005a,6));
 struct DspVariant { const char* fw; CartridgeHardware hw; std::vector<uint8_t> rom; uint32_t dr; uint32_t sr; const char* name; };
 const std::vector<DspVariant> variants={
   {"dsp2",CartridgeHardware::DSP2,dspVariantRom(0x20,0x05),0x208000,0x20c000,"DSP2"},
   {"dsp3",CartridgeHardware::DSP3,dspVariantRom(0x30,0x05,0xb2),0x208000,0x20c000,"DSP3"},
   {"dsp4",CartridgeHardware::DSP4,dspVariantRom(0x30,0x03),0x006000,0x007000,"DSP4"},
 };
 for(const auto& v:variants){
   save(dir/(std::string(v.fw)+".program.rom"),program);save(dir/(std::string(v.fw)+".data.rom"),data);
   Cartridge x;x.setSRAMPersistence(false);expect(x.loadData(v.rom,(dir/(std::string(v.name)+".sfc")).string()),"load firmware-backed DSP variant");
   expect(x.hardwareType()==v.hw,"DSP variant detected correctly");
   expect(x.hardwareSupportLevel()==CartridgeSupportLevel::FirmwareLLE,"DSP variant selects firmware LLE");
   Bus xb;xb.attachCartridge(&x);x.clockHardware(20);
   expect((xb.read(v.sr)&0x80)!=0,"DSP variant firmware asserts RQM on its mapped status register");
   expect(xb.read(v.dr)==0x5a,"DSP variant firmware exposes DR through its mapped data register");
 }
 std::vector<uint8_t> stProgram(0xc000,0),stData(0x1000,0);
 op(stProgram,0,loi(0x55aa,15));
 save(dir/"st010.program.rom",stProgram);save(dir/"st010.data.rom",stData);
 Cartridge st;st.setSRAMPersistence(false);expect(st.loadData(stRom(),(dir/"stgame.sfc").string()),"load firmware-backed ST010");
 expect(st.hardwareType()==CartridgeHardware::ST010,"ST010 detected");
 expect(st.hardwareSupportLevel()==CartridgeSupportLevel::FirmwareLLE,"ST010 firmware selects uPD96050 core");
 Bus sb;sb.attachCartridge(&st);st.clockHardware(4);
 expect(sb.read(0x680000)==0xaa&&sb.read(0x680001)==0x55,"uPD96050 firmware writes cartridge-visible shared RAM");
 save(dir/"st011.program.rom",stProgram);save(dir/"st011.data.rom",stData);
 auto st11rom=stRom(); st11rom[0x7fc0+0x17]=0x09;
 Cartridge st11;st11.setSRAMPersistence(false);expect(st11.loadData(st11rom,(dir/"st11game.sfc").string()),"load firmware-backed ST011");
 expect(st11.hardwareType()==CartridgeHardware::ST011,"ST011 detected");
 expect(st11.hardwareSupportLevel()==CartridgeSupportLevel::FirmwareLLE,"ST011 firmware selects uPD96050 core");
 Bus s11b;s11b.attachCartridge(&st11);st11.clockHardware(4);
 expect(s11b.read(0x680000)==0xaa&&s11b.read(0x680001)==0x55,"ST011 firmware shares uPD96050 RAM with the cartridge bus");
 std::fill(stProgram.begin(),stProgram.end(),0);
 op(stProgram,0,loi(0xffff,4));
 op(stProgram,1,(4u<<4)|6u);
 save(dir/"st010.program.rom",stProgram);
 Cartridge stWidth;stWidth.setSRAMPersistence(false);expect(stWidth.loadData(stRom(),(dir/"stwidth.sfc").string()),"load register-width ST010 firmware");
 Bus swb;swb.attachCartridge(&stWidth);stWidth.clockHardware(100);
 const uint8_t stDpLo=swb.read(0x600000),stDpHi=swb.read(0x600000);
 expect(stDpLo==0xff&&stDpHi==0x07,"uPD96050 DP is masked to its 11-bit architectural width");
 std::filesystem::remove_all(dir);
 if(fails){std::cerr<<fails<<" NEC DSP firmware regressions failed\n";return 1;}std::cout<<"NEC DSP firmware regression passed\n";
}
