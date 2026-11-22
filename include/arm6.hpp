#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
class ARM6Core {
public:
    using Read32 = std::function<uint32_t(uint32_t)>;
    using Read8 = std::function<uint8_t(uint32_t)>;
    using Write32 = std::function<void(uint32_t,uint32_t)>;
    using Write8 = std::function<void(uint32_t,uint8_t)>;
    void attach(Read32 r32, Read8 r8, Write32 w32, Write8 w8);
    void reset();
    unsigned step();
    uint32_t pc() const { return pc_; }
    uint32_t cpsr() const { return cpsr_; }
    uint32_t currentInstruction() const { return current_instruction_; }
    const std::array<uint32_t,16>& registers() const { return r_; }
    std::array<uint32_t,16>& registers() { return r_; }
    void setCpsr(uint32_t v);
    void setPc(uint32_t v) { pc_ = v & ~3u; }
    std::vector<uint8_t> saveState() const;
    bool loadState(const std::vector<uint8_t>&);
private:
    bool condition(uint32_t op) const;
    uint32_t reg(unsigned n) const;
    uint32_t operand2(uint32_t op, bool& carry) const;
    uint32_t shift(uint32_t value, unsigned type, unsigned amount, bool byRegister, bool& carry) const;
    void setNZ(uint32_t v);
    void setNZCV(uint32_t v, bool c, bool ov);
    void exception(uint32_t vector, uint32_t returnPc, uint8_t mode);
    void switchMode(uint8_t newMode);
    uint32_t& activeSpsr();
    const uint32_t& activeSpsr() const;
    bool modeHasSpsr(uint8_t mode) const;
    bool privileged() const;
    uint32_t loadWord(uint32_t address) const;
    void storeWord(uint32_t address, uint32_t value) const;
    uint32_t userReg(unsigned n) const;
    void setUserReg(unsigned n, uint32_t value);
    std::array<uint32_t,16> r_{};
    uint32_t pc_ = 0;
    uint32_t cpsr_ = 0x000000D3u;
    uint32_t current_instruction_ = 0;
    std::array<uint32_t,5> user_r8_12_{};
    uint32_t user_r13_ = 0, user_r14_ = 0;
    std::array<uint32_t,7> fiq_r8_14_{};
    uint32_t fiq_spsr_ = 0;
    uint32_t irq_r13_ = 0, irq_r14_ = 0, irq_spsr_ = 0;
    uint32_t svc_r13_ = 0, svc_r14_ = 0, svc_spsr_ = 0;
    uint32_t abt_r13_ = 0, abt_r14_ = 0, abt_spsr_ = 0;
    uint32_t und_r13_ = 0, und_r14_ = 0, und_spsr_ = 0;
    Read32 read32_;
    Read8 read8_;
    Write32 write32_;
    Write8 write8_;
};
