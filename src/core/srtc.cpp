#include "srtc.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
namespace {
class SRTCDevice final : public CartridgeDevice {
public:
    SRTCDevice(std::vector<uint8_t>& storage, bool& dirty, uint32_t masterFrequency)
        : rtc_(storage), dirty_(dirty), master_frequency_(masterFrequency ? masterFrequency : 21477272u) {
        ensure();
        synchronizeFromPersistence();
        reset();
    }
    bool implemented() const override { return true; }
    bool needsClock() const override { return true; }
    uint32_t stateTag() const override { return 0x43545253u; }
    std::vector<uint8_t> saveState() const override {
        stateio::Writer w;
        uint32_t ver = 2;
        w.pod(ver); w.pod(mode_); w.pod(index_); w.pod(master_accum_); w.vector(rtc_);
        return w.take();
    }
    bool loadState(const std::vector<uint8_t>& b) override {
        stateio::Reader r(b); uint32_t ver = 0; std::vector<uint8_t> temp;
        if (!r.pod(ver) || ver != 2 || !r.pod(mode_) || !r.pod(index_) || !r.pod(master_accum_) ||
            !r.vector(temp, 1024) || !r.done() || temp.size() < 20) return false;
        if (index_ < -1 || index_ > 13 || master_accum_ >= master_frequency_) return false;
        rtc_ = std::move(temp); dirty_ = true;
        return true;
    }
    void reset() override {
        ensure(); mode_ = Read; index_ = -1;
    }
    void clock(uint64_t masterClocks) override {
        if (!masterClocks) return;
        master_accum_ += masterClocks;
        while (master_accum_ >= master_frequency_) {
            master_accum_ -= master_frequency_;
            tickSecond();
        }
    }
    void preparePersistentSave() override {
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        setTimestamp(now);
    }
    bool readHost(uint32_t address, uint8_t openBus, uint8_t& value) override {
        const uint8_t bank = uint8_t(address >> 16); const uint16_t a = uint16_t(address);
        if (!((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && a == 0x2800)) return false;
        if (mode_ != Read) { value = 0; return true; }
        if (index_ < 0) { ++index_; value = 0x0f; }
        else if (index_ > 12) { index_ = -1; value = 0x0f; }
        else value = rtc_[index_++];
        (void)openBus;
        return true;
    }
    bool write(uint32_t address, uint8_t data) override {
        const uint8_t bank = uint8_t(address >> 16); const uint16_t a = uint16_t(address);
        if (!((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && a == 0x2801)) return false;
        data &= 0x0f;
        if (data == 0x0d) { mode_ = Read; index_ = -1; return true; }
        if (data == 0x0e) { mode_ = Command; return true; }
        if (data == 0x0f) return true;
        if (mode_ == Write) {
            if (index_ >= 0 && index_ < 12) {
                set(static_cast<size_t>(index_++), data);
                if (index_ == 12) {
                    const unsigned day = rtc_[6] + rtc_[7] * 10;
                    const unsigned month = rtc_[8];
                    const unsigned year = 1000 + rtc_[9] + rtc_[10] * 10 + rtc_[11] * 100;
                    set(static_cast<size_t>(index_++), static_cast<uint8_t>(weekday(year, month, day)));
                }
            }
        } else if (mode_ == Command) {
            if (data == 0) { mode_ = Write; index_ = 0; }
            else if (data == 4) {
                mode_ = Ready; index_ = -1;
                for (int i = 0; i < 13; ++i) set(static_cast<size_t>(i), 0);
            } else mode_ = Ready;
        }
        return true;
    }
private:
    enum Mode : uint8_t { Ready, Command, Read, Write };
    void ensure() { if (rtc_.size() < 20) rtc_.resize(20, 0); }
    void set(size_t i, uint8_t v) { if (rtc_[i] != v) { rtc_[i] = v; dirty_ = true; } }
    uint64_t timestamp() const {
        uint64_t v = 0;
        for (unsigned i = 0; i < 4; ++i) v |= uint64_t(rtc_[16 + i]) << (i * 8);
        return v;
    }
    void setTimestamp(uint64_t t) {
        for (unsigned i = 0; i < 4; ++i) set(16 + i, static_cast<uint8_t>(t >> (i * 8)));
    }
    static bool leap(unsigned year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
    static unsigned daysInMonth(unsigned year, unsigned month) {
        static constexpr unsigned md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        month = std::max(1u, std::min(12u, month));
        return md[month - 1] + ((month == 2 && leap(year)) ? 1u : 0u);
    }
    static unsigned weekday(unsigned year, unsigned month, unsigned day) {
        year = std::max(1000u, year); month = std::max(1u, std::min(12u, month)); day = std::max(1u, std::min(31u, day));
        unsigned sum = 0;
        for (unsigned y = 1000; y < year; ++y) sum += leap(y) ? 366u : 365u;
        for (unsigned m = 1; m < month; ++m) sum += daysInMonth(year, m);
        sum += day - 1;
        return (sum + 3) % 7;
    }
    void tickSecond() {
        unsigned sec = rtc_[0] + rtc_[1] * 10;
        unsigned min = rtc_[2] + rtc_[3] * 10;
        unsigned hour = rtc_[4] + rtc_[5] * 10;
        unsigned day = rtc_[6] + rtc_[7] * 10;
        unsigned mon = rtc_[8];
        unsigned year = 1000 + rtc_[9] + rtc_[10] * 10 + rtc_[11] * 100;
        unsigned wday = rtc_[12] % 7;
        if (day == 0) day = 1;
        if (mon == 0 || mon > 12) mon = 1;
        if (++sec >= 60) {
            sec = 0;
            if (++min >= 60) {
                min = 0;
                if (++hour >= 24) {
                    hour = 0; ++day; wday = (wday + 1) % 7;
                    if (day > daysInMonth(year, mon)) {
                        day = 1;
                        if (++mon > 12) { mon = 1; year = 1000 + ((year - 999) % 1000); }
                    }
                }
            }
        }
        set(0, uint8_t(sec % 10)); set(1, uint8_t(sec / 10));
        set(2, uint8_t(min % 10)); set(3, uint8_t(min / 10));
        set(4, uint8_t(hour % 10)); set(5, uint8_t(hour / 10));
        set(6, uint8_t(day % 10)); set(7, uint8_t(day / 10)); set(8, uint8_t(mon));
        unsigned y = year - 1000;
        set(9, uint8_t(y % 10)); set(10, uint8_t((y / 10) % 10)); set(11, uint8_t((y / 100) % 10)); set(12, uint8_t(wday));
    }
    void advanceSeconds(uint64_t seconds) {
        while (seconds--) tickSecond();
    }
    void synchronizeFromPersistence() {
        ensure();
        const uint64_t then = timestamp();
        if (!then) return;
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        if (now > then) advanceSeconds(now - then);
    }
    std::vector<uint8_t>& rtc_;
    bool& dirty_;
    uint32_t master_frequency_ = 21477272u;
    uint64_t master_accum_ = 0;
    Mode mode_ = Read;
    int index_ = -1;
};
}
std::unique_ptr<CartridgeDevice> createSRTCDevice(std::vector<uint8_t>& rtcStorage, bool& dirty, uint32_t masterFrequency) {
    return std::make_unique<SRTCDevice>(rtcStorage, dirty, masterFrequency);
}
