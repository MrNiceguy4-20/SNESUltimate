#pragma once
#include <cstdint>
#include <string>
#include <vector>
enum class CartridgeSupportLevel : uint8_t {
    Unsupported = 0,
    PartialHLE = 1,
    Full = 2,
    FirmwareLLE = 3,
    TimingApproximate = 4,
};
class CartridgeDevice {
public:
    virtual ~CartridgeDevice() = default;
    virtual void reset() {}
    virtual bool read(uint32_t address, uint8_t& value) { (void)address; (void)value; return false; }
    virtual bool readHost(uint32_t address, uint8_t openBus, uint8_t& value) {
        (void)openBus;
        return read(address, value);
    }
    virtual bool write(uint32_t address, uint8_t value) { (void)address; (void)value; return false; }
    virtual void clock(uint64_t masterClocks) { (void)masterClocks; }
    virtual bool needsClock() const { return false; }
    virtual bool hostIrqPending() const { return false; }
    virtual bool hostIrqMayChangeWithoutHostAccess() const { return needsClock(); }
    virtual bool hostAccessRequiresClock(uint32_t address, bool write) const {
        (void)address; (void)write; return needsClock();
    }
    virtual void hostAccessBegin(uint32_t address, bool write, uint8_t masterClocks) {
        (void)address; (void)write; (void)masterClocks;
    }
    virtual bool clockAndHostIrq(uint64_t masterClocks) {
        clock(masterClocks);
        return hostIrqPending();
    }
    virtual void dmaRegisterWrite(int channel, int offset, uint8_t value) {
        (void)channel; (void)offset; (void)value;
    }
    virtual void dmaBegin(int channel, uint32_t aAddress, uint32_t length, uint8_t control) {
        (void)channel; (void)aAddress; (void)length; (void)control;
    }
    virtual bool dmaRead(int channel, uint32_t address, uint8_t& value) {
        (void)channel; (void)address; (void)value; return false;
    }
    virtual void dmaEnd(int channel) { (void)channel; }
    virtual uint32_t stateTag() const { return 0; }
    virtual std::vector<uint8_t> saveState() const { return {}; }
    virtual bool loadState(const std::vector<uint8_t>& state) { (void)state; return false; }
    virtual CartridgeSupportLevel supportLevel() const { return implemented() ? CartridgeSupportLevel::Full : CartridgeSupportLevel::Unsupported; }
    virtual void preparePersistentSave() {}
    virtual bool implemented() const = 0;
};
