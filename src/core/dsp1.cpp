#include "dsp1.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace {
constexpr double kTau = 6.283185307179586476925286766559;
class DSP1Device final : public CartridgeDevice {
public:
    explicit DSP1Device(DSP1Map map, uint32_t masterHz) : map_(map), masterHz_(masterHz ? masterHz : 21477272u) { reset(); }
    void reset() override {
        command_ = 0;
        expected_ = 0;
        input_.clear();
        output_.clear();
        outputPos_ = 0;
        waiting_ = true;
        for (auto& m : matrix_) for (auto& row : m) row.fill(0);
        projection_valid_ = false;
        projection_params_.fill(0);
        raster_vs_ = 0;
        sin_aas_ = cos_aas_ = sin_azs_ = cos_azs_ = 0;
        sin_azs_clipped_ = cos_azs_clipped_ = cos_azs_clipped_base_ = 0;
        nx_ = ny_ = nz_ = 0;
        centre_x_ = centre_y_ = centre_z_ = 0;
        ground_x_ = ground_y_ = ground_z_ = 0;
        v_offset_ = 0;
        clipped_azs_ = 0;
        vplane_c_ = vplane_e_ = 0;
        c_les_ = e_les_ = g_les_ = 0;
        sec1_c_ = sec1_e_ = sec2_c_ = sec2_e_ = 0;
        dsp_frac_ = 0; busy_cycles_ = 0; raster_next_cycles_ = 0; input_word_index_ = 0; output_word_index_ = 0; pending_execute_ = false;
    }
    bool implemented() const override { return true; }
    CartridgeSupportLevel supportLevel() const override { return CartridgeSupportLevel::PartialHLE; }
    bool needsClock() const override { return true; }
    bool hostIrqMayChangeWithoutHostAccess() const override { return false; }
    void clock(uint64_t masterClocks) override {
        if (masterClocks == 0) return;
        constexpr uint64_t kDspHz = 7600000ull;
        const uint64_t whole = masterClocks / masterHz_;
        const uint64_t rem = masterClocks % masterHz_;
        uint64_t cycles = whole * kDspHz;
        const uint64_t scaled = rem * kDspHz + dsp_frac_;
        cycles += scaled / masterHz_;
        dsp_frac_ = scaled % masterHz_;
        if (busy_cycles_ != 0) {
            if (cycles >= busy_cycles_) {
                busy_cycles_ = 0;
                if (pending_execute_) { pending_execute_ = false; execute(); }
            } else busy_cycles_ -= static_cast<uint32_t>(cycles);
        }
        if (raster_next_cycles_ != 0) {
            if (cycles >= raster_next_cycles_) {
                raster_next_cycles_ = 0;
                if (command_ == 0x0a && waiting_ && output_.empty()) emitRaster(raster_vs_++);
            } else raster_next_cycles_ -= static_cast<uint32_t>(cycles);
        }
    }
    uint32_t stateTag() const override{return 0x31505344u;}
    std::vector<uint8_t> saveState() const override{stateio::Writer w;uint32_t v=7;w.pod(v);w.pod(map_);w.pod(command_);w.pod(expected_);w.vector(input_);w.vector(output_);w.pod(outputPos_);w.pod(waiting_);w.array(matrix_);w.pod(projection_valid_);w.array(projection_params_);w.pod(raster_vs_);w.pod(sin_aas_);w.pod(cos_aas_);w.pod(sin_azs_);w.pod(cos_azs_);w.pod(sin_azs_clipped_);w.pod(cos_azs_clipped_);w.pod(cos_azs_clipped_base_);w.pod(nx_);w.pod(ny_);w.pod(nz_);w.pod(centre_x_);w.pod(centre_y_);w.pod(centre_z_);w.pod(ground_x_);w.pod(ground_y_);w.pod(ground_z_);w.pod(v_offset_);w.pod(clipped_azs_);w.pod(vplane_c_);w.pod(vplane_e_);w.pod(c_les_);w.pod(e_les_);w.pod(g_les_);w.pod(sec1_c_);w.pod(sec1_e_);w.pod(sec2_c_);w.pod(sec2_e_);w.pod(masterHz_);w.pod(dsp_frac_);w.pod(busy_cycles_);w.pod(raster_next_cycles_);w.pod(input_word_index_);w.pod(output_word_index_);w.pod(pending_execute_);return w.take();}
    bool loadState(const std::vector<uint8_t>&b)override{stateio::Reader r(b);uint32_t v=0;if(!r.pod(v)||(v<1||v>7)||!r.pod(map_)||!r.pod(command_)||!r.pod(expected_)||!r.vector(input_)||!r.vector(output_)||!r.pod(outputPos_)||!r.pod(waiting_)||!r.array(matrix_))return false;if(v>=2){if(!r.pod(projection_valid_)||!r.array(projection_params_)||!r.pod(raster_vs_))return false;}else{projection_valid_=false;projection_params_.fill(0);raster_vs_=0;}if(v>=4){if(!r.pod(sin_aas_)||!r.pod(cos_aas_)||!r.pod(sin_azs_)||!r.pod(cos_azs_)||!r.pod(sin_azs_clipped_)||!r.pod(cos_azs_clipped_)||!r.pod(cos_azs_clipped_base_)||!r.pod(nx_)||!r.pod(ny_)||!r.pod(nz_)||!r.pod(centre_x_)||!r.pod(centre_y_)||!r.pod(centre_z_)||!r.pod(ground_x_)||!r.pod(ground_y_)||!r.pod(ground_z_)||!r.pod(v_offset_)||!r.pod(clipped_azs_))return false;}else if(projection_valid_){rebuildProjectionState();}if(v>=5){if(!r.pod(vplane_c_)||!r.pod(vplane_e_)||!r.pod(c_les_)||!r.pod(e_les_)||!r.pod(g_les_)||!r.pod(sec1_c_)||!r.pod(sec1_e_)||!r.pod(sec2_c_)||!r.pod(sec2_e_))return false;}else if(projection_valid_){rebuildProjectionState();}if(v>=6){uint32_t mh=0;if(!r.pod(mh)||!r.pod(dsp_frac_)||!r.pod(busy_cycles_))return false;if(v>=7){if(!r.pod(raster_next_cycles_))return false;}else raster_next_cycles_=0;if(!r.pod(input_word_index_)||!r.pod(output_word_index_)||!r.pod(pending_execute_)||mh==0)return false;masterHz_=mh;}else{dsp_frac_=0;busy_cycles_=0;raster_next_cycles_=0;input_word_index_=output_word_index_=0;pending_execute_=false;}return r.done()&&outputPos_<=output_.size();}
    static uint16_t commandCycles(uint8_t op) {
        switch (op) {
        case 0x00: case 0x20: case 0x10: case 0x04: case 0x08:
        case 0x18: case 0x38: case 0x28: case 0x0c: case 0x1c: return 6;
        default: return inputBytes(op) ? 6 : 0;
        }
    }
    static uint16_t inputCycles(uint8_t op, std::size_t wordIndex) {
        static constexpr uint16_t mul[] = {12,4};
        static constexpr uint16_t inv[] = {13,73};
        static constexpr uint16_t tri[] = {12,24};
        static constexpr uint16_t rad[] = {14,4,4};
        static constexpr uint16_t range[] = {12,4,4,8};
        static constexpr uint16_t dist[] = {15,4,127};
        static constexpr uint16_t rot[] = {12,3,37};
        static constexpr uint16_t polar[] = {13,3,2,2,2,107};
        static constexpr uint16_t raster[] = {211};
        static constexpr uint16_t parameter[] = {11,2,2,3,3,4,839};
        static constexpr uint16_t attitude[] = {13,4,4,137};
        static constexpr uint16_t objective[] = {14,4,7};
        static constexpr uint16_t subjective[] = {13,4,7};
        static constexpr uint16_t scalar[] = {15,4,7};
        static constexpr uint16_t target[] = {11,203};
        const uint16_t* p=nullptr; std::size_t n=0;
        switch(op){
        case 0x00: case 0x20: p=mul;n=2;break; case 0x10:p=inv;n=2;break;
        case 0x04:p=tri;n=2;break; case 0x08:p=rad;n=3;break;
        case 0x18: case 0x38:p=range;n=4;break; case 0x28:p=dist;n=3;break;
        case 0x0c:p=rot;n=3;break; case 0x1c:p=polar;n=6;break;
        case 0x0a:p=raster;n=1;break; case 0x02:p=parameter;n=7;break;
        case 0x01: case 0x11: case 0x21:p=attitude;n=4;break;
        case 0x0d: case 0x1d: case 0x2d:p=objective;n=3;break;
        case 0x03: case 0x13: case 0x23:p=subjective;n=3;break;
        case 0x0b: case 0x1b: case 0x2b:p=scalar;n=3;break;
        case 0x0e:p=target;n=2;break;
        default:return 0;
        }
        return wordIndex<n?p[wordIndex]:0;
    }
    static uint16_t outputCycles(uint8_t op, std::size_t wordIndex) {
        static constexpr uint16_t one4[] = {4};
        static constexpr uint16_t inv[] = {2,4};
        static constexpr uint16_t tri[] = {3,4};
        static constexpr uint16_t rad[] = {2,4};
        static constexpr uint16_t rot[] = {2,4};
        static constexpr uint16_t polar[] = {6,2,4};
        static constexpr uint16_t raster[] = {3,3,3,7};
        static constexpr uint16_t parameter[] = {2,10,5,5};
        static constexpr uint16_t coordinates[] = {5,5,4};
        static constexpr uint16_t scalar[] = {4};
        static constexpr uint16_t target[] = {4,4};
        const uint16_t* p=nullptr; std::size_t n=0;
        switch(op){
        case 0x00: case 0x20: case 0x18: case 0x38: case 0x28:p=one4;n=1;break;
        case 0x10:p=inv;n=2;break; case 0x04:p=tri;n=2;break;
        case 0x08:p=rad;n=2;break; case 0x0c:p=rot;n=2;break;
        case 0x1c:p=polar;n=3;break; case 0x0a:p=raster;n=4;break;
        case 0x02:p=parameter;n=4;break;
        case 0x0d: case 0x1d: case 0x2d:
        case 0x03: case 0x13: case 0x23:p=coordinates;n=3;break;
        case 0x0b: case 0x1b: case 0x2b:p=scalar;n=1;break;
        case 0x0e:p=target;n=2;break;
        default:return 0;
        }
        return wordIndex<n?p[wordIndex]:0;
    }
    bool ready() const { return busy_cycles_ == 0 && !pending_execute_; }
    void startBusy(uint16_t cycles) { busy_cycles_ = cycles; }
    bool read(uint32_t address, uint8_t& value) override {
        if (!owns(address)) return false;
        if (!isDataPort(address)) {
            value = ready() ? 0x80 : 0x00;
            return true;
        }
        if (!ready()) { value = 0x80; return true; }
        if (outputPos_ < output_.size()) {
            value = output_[outputPos_++];
            if ((outputPos_ & 1u) == 0u) startBusy(outputCycles(command_, output_word_index_++));
            if (outputPos_ == output_.size()) {
                const bool raster = command_ == 0x0a && projection_valid_;
                output_.clear();
                outputPos_ = 0;
                if (raster) {
                    raster_next_cycles_ = 200;
                }
            }
        } else {
            value = 0x80;
        }
        return true;
    }
    bool write(uint32_t address, uint8_t value) override {
        if (!owns(address)) return false;
        if (!isDataPort(address)) return true;
        if (!ready()) return true;
        if (command_ == 0x0a && outputPos_ < output_.size()) {
            ++outputPos_;
            if (outputPos_ == output_.size()) {
                output_.clear();
                outputPos_ = 0;
            }
            return true;
        }
        if (waiting_) {
            raster_next_cycles_ = 0;
            command_ = canonical(value);
            expected_ = inputBytes(command_);
            input_.clear();
            output_.clear();
            outputPos_ = 0; input_word_index_ = output_word_index_ = 0;
            startBusy(commandCycles(command_));
            if (expected_ == 0) {
                waiting_ = true;
            } else {
                waiting_ = false;
            }
            return true;
        }
        input_.push_back(value);
        if ((input_.size() & 1u) == 0u) startBusy(inputCycles(command_, input_word_index_++));
        if (input_.size() >= expected_) {
            waiting_ = true;
            if (busy_cycles_ == 0) execute(); else pending_execute_ = true;
        }
        return true;
    }
private:
    using Matrix = std::array<std::array<int16_t, 3>, 3>;
    bool isDataPort(uint32_t address) const {
        const uint16_t a = static_cast<uint16_t>(address);
        switch (map_) {
        case DSP1Map::LoROMSmall: return a < 0xc000;
        case DSP1Map::LoROMLarge: return a < 0x4000;
        case DSP1Map::HiROM:      return a < 0x7000;
        case DSP1Map::DSP2LoROM:  return false;
        }
        return false;
    }
    bool owns(uint32_t address) const {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t a = static_cast<uint16_t>(address);
        switch (map_) {
        case DSP1Map::LoROMSmall:
            return ((bank >= 0x30 && bank <= 0x3f) || (bank >= 0xb0 && bank <= 0xbf)) && a >= 0x8000;
        case DSP1Map::LoROMLarge:
            return ((bank >= 0x60 && bank <= 0x6f) || (bank >= 0xe0 && bank <= 0xef)) && a < 0x8000;
        case DSP1Map::HiROM:
            return ((bank <= 0x1f) || (bank >= 0x80 && bank <= 0x9f)) && a >= 0x6000 && a < 0x8000;
        case DSP1Map::DSP2LoROM:
            return false;
        }
        return false;
    }
    static uint8_t canonical(uint8_t op) {
        switch (op) {
        case 0x12: case 0x22: case 0x32: return 0x02;
        case 0x1a: case 0x2a: case 0x3a: return 0x0a;
        case 0x16: case 0x26: case 0x36: return 0x06;
        case 0x1e: case 0x2e: case 0x3e: return 0x0e;
        case 0x24: return 0x04;
        case 0x2c: return 0x0c;
        case 0x3c: return 0x1c;
        case 0x30: return 0x10;
        case 0x34: return 0x14;
        case 0x07: return 0x0f;
        case 0x27: return 0x2f;
        case 0x17: case 0x37: case 0x3f: return 0x1f;
        case 0x05: case 0x31: case 0x35: return 0x01;
        case 0x15: return 0x11;
        case 0x25: return 0x21;
        case 0x09: case 0x39: case 0x3d: return 0x0d;
        case 0x19: return 0x1d;
        case 0x29: return 0x2d;
        case 0x33: return 0x03;
        case 0x3b: return 0x0b;
        default: return op;
        }
    }
    static std::size_t inputBytes(uint8_t op) {
        switch (op) {
        case 0x00: case 0x20: case 0x10: case 0x04: return 4;
        case 0x02: return 14;
        case 0x0a: return 2;
        case 0x06: return 6;
        case 0x0e: return 4;
        case 0x08: case 0x28: case 0x0c: return 6;
        case 0x18: case 0x38: return 8;
        case 0x1c: case 0x14: return 12;
        case 0x01: case 0x11: case 0x21: return 8;
        case 0x0d: case 0x1d: case 0x2d:
        case 0x03: case 0x13: case 0x23:
        case 0x0b: case 0x1b: case 0x2b: return 6;
        case 0x0f: case 0x2f: case 0x1f: return 2;
        default: return 0;
        }
    }
    int16_t word(std::size_t i) const {
        return static_cast<int16_t>(static_cast<uint16_t>(input_[i]) |
            (static_cast<uint16_t>(input_[i + 1]) << 8));
    }
    void out16(int32_t value) {
        const uint16_t v = static_cast<uint16_t>(value);
        output_.push_back(static_cast<uint8_t>(v));
        output_.push_back(static_cast<uint8_t>(v >> 8));
    }
    static constexpr std::array<int16_t, 256> kSinTable = {
        0, 804, 1607, 2410, 3211, 4011, 4808, 5602,
        6392, 7179, 7961, 8739, 9512, 10278, 11039, 11793,
        12539, 13278, 14010, 14732, 15446, 16151, 16846, 17530,
        18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
        23170, 23732, 24279, 24812, 25330, 25832, 26319, 26790,
        27245, 27684, 28106, 28511, 28898, 29269, 29621, 29956,
        30273, 30572, 30852, 31114, 31357, 31581, 31785, 31971,
        32138, 32285, 32413, 32521, 32610, 32679, 32728, 32758,
        32767, 32758, 32728, 32679, 32610, 32521, 32413, 32285,
        32138, 31971, 31785, 31581, 31357, 31114, 30852, 30572,
        30273, 29956, 29621, 29269, 28898, 28511, 28106, 27684,
        27245, 26790, 26319, 25832, 25330, 24812, 24279, 23732,
        23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
        18204, 17530, 16846, 16151, 15446, 14732, 14010, 13278,
        12539, 11793, 11039, 10278, 9512, 8739, 7961, 7179,
        6392, 5602, 4808, 4011, 3211, 2410, 1607, 804,
        0, -804, -1607, -2410, -3211, -4011, -4808, -5602,
        -6392, -7179, -7961, -8739, -9512, -10278, -11039, -11793,
        -12539, -13278, -14010, -14732, -15446, -16151, -16846, -17530,
        -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
        -23170, -23732, -24279, -24812, -25330, -25832, -26319, -26790,
        -27245, -27684, -28106, -28511, -28898, -29269, -29621, -29956,
        -30273, -30572, -30852, -31114, -31357, -31581, -31785, -31971,
        -32138, -32285, -32413, -32521, -32610, -32679, -32728, -32758,
        -32768, -32758, -32728, -32679, -32610, -32521, -32413, -32285,
        -32138, -31971, -31785, -31581, -31357, -31114, -30852, -30572,
        -30273, -29956, -29621, -29269, -28898, -28511, -28106, -27684,
        -27245, -26790, -26319, -25832, -25330, -24812, -24279, -23732,
        -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
        -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13278,
        -12539, -11793, -11039, -10278, -9512, -8739, -7961, -7179,
        -6392, -5602, -4808, -4011, -3211, -2410, -1607, -804,
    };
    static constexpr std::array<int16_t, 256> kMulTable = {
        0, 3, 6, 9, 12, 15, 18, 21,
        25, 28, 31, 34, 37, 40, 43, 47,
        50, 53, 56, 59, 62, 65, 69, 72,
        75, 78, 81, 84, 87, 91, 94, 97,
        100, 103, 106, 109, 113, 116, 119, 122,
        125, 128, 131, 135, 138, 141, 144, 147,
        150, 153, 157, 160, 163, 166, 169, 172,
        175, 179, 182, 185, 188, 191, 194, 197,
        201, 204, 207, 210, 213, 216, 219, 223,
        226, 229, 232, 235, 238, 241, 245, 248,
        251, 254, 257, 260, 263, 267, 270, 273,
        276, 279, 282, 285, 289, 292, 295, 298,
        301, 304, 307, 311, 314, 317, 320, 323,
        326, 329, 333, 336, 339, 342, 345, 348,
        351, 354, 358, 361, 364, 367, 370, 373,
        376, 380, 383, 386, 389, 392, 395, 398,
        402, 405, 408, 411, 414, 417, 420, 424,
        427, 430, 433, 436, 439, 442, 446, 449,
        452, 455, 458, 461, 464, 468, 471, 474,
        477, 480, 483, 486, 490, 493, 496, 499,
        502, 505, 508, 512, 515, 518, 521, 524,
        527, 530, 534, 537, 540, 543, 546, 549,
        552, 556, 559, 562, 565, 568, 571, 574,
        578, 581, 584, 587, 590, 593, 596, 600,
        603, 606, 609, 612, 615, 618, 622, 625,
        628, 631, 634, 637, 640, 644, 647, 650,
        653, 656, 659, 662, 666, 669, 672, 675,
        678, 681, 684, 688, 691, 694, 697, 700,
        703, 706, 709, 713, 716, 719, 722, 725,
        728, 731, 735, 738, 741, 744, 747, 750,
        753, 757, 760, 763, 766, 769, 772, 775,
        779, 782, 785, 788, 791, 794, 797, 801,
    };
    static int16_t q15sin(int16_t angle) {
        if (angle < 0) {
            if (angle == INT16_MIN) return 0;
            return static_cast<int16_t>(-q15sin(static_cast<int16_t>(-angle)));
        }
        const uint16_t a = static_cast<uint16_t>(angle);
        int32_t v = kSinTable[a >> 8] +
            ((static_cast<int32_t>(kMulTable[a & 0xff]) * kSinTable[0x40 + (a >> 8)]) >> 15);
        if (v > 32767) v = 32767;
        return static_cast<int16_t>(v);
    }
    static int16_t q15cos(int16_t angle) {
        if (angle < 0) {
            if (angle == INT16_MIN) return INT16_MIN;
            angle = static_cast<int16_t>(-angle);
        }
        const uint16_t a = static_cast<uint16_t>(angle);
        int32_t v = kSinTable[0x40 + (a >> 8)] -
            ((static_cast<int32_t>(kMulTable[a & 0xff]) * kSinTable[a >> 8]) >> 15);
        if (v < -32768) v = -32767;
        return static_cast<int16_t>(v);
    }
    static int16_t mulq(int16_t a, int16_t b) {
        return static_cast<int16_t>((static_cast<int32_t>(a) * b) >> 15);
    }
    void setMatrix(unsigned index) {
        int16_t scale = static_cast<int16_t>(word(0) >> 1);
        const int16_t z = word(2), y = word(4), x = word(6);
        const int16_t sz = q15sin(z), cz = q15cos(z);
        const int16_t sy = q15sin(y), cy = q15cos(y);
        const int16_t sx = q15sin(x), cx = q15cos(x);
        Matrix& m = matrix_[index];
        auto mm = [](int32_t a, int32_t b) { return static_cast<int32_t>((a * b) >> 15); };
        m[0][0] = static_cast<int16_t>(mm(mm(scale, cz), cy));
        m[0][1] = static_cast<int16_t>(-mm(mm(scale, sz), cy));
        m[0][2] = static_cast<int16_t>(mm(scale, sy));
        m[1][0] = static_cast<int16_t>(mm(mm(scale, sz), cx) + mm(mm(mm(scale, cz), sx), sy));
        m[1][1] = static_cast<int16_t>(mm(mm(scale, cz), cx) - mm(mm(mm(scale, sz), sx), sy));
        m[1][2] = static_cast<int16_t>(-mm(mm(scale, sx), cy));
        m[2][0] = static_cast<int16_t>(mm(mm(scale, sz), sx) - mm(mm(mm(scale, cz), cx), sy));
        m[2][1] = static_cast<int16_t>(mm(mm(scale, cz), sx) + mm(mm(mm(scale, sz), cx), sy));
        m[2][2] = static_cast<int16_t>(mm(mm(scale, cx), cy));
    }
    void matrixForward(unsigned index) {
        const int16_t x = word(0), y = word(2), z = word(4);
        const Matrix& m = matrix_[index];
        for (unsigned row = 0; row < 3; ++row) {
            const int32_t v = (static_cast<int32_t>(x) * m[row][0] >> 15) +
                              (static_cast<int32_t>(y) * m[row][1] >> 15) +
                              (static_cast<int32_t>(z) * m[row][2] >> 15);
            out16(v);
        }
    }
    void matrixReverse(unsigned index) {
        const int16_t f = word(0), l = word(2), u = word(4);
        const Matrix& m = matrix_[index];
        for (unsigned col = 0; col < 3; ++col) {
            const int32_t v = (static_cast<int32_t>(f) * m[0][col] >> 15) +
                              (static_cast<int32_t>(l) * m[1][col] >> 15) +
                              (static_cast<int32_t>(u) * m[2][col] >> 15);
            out16(v);
        }
    }
    void scalar(unsigned index) {
        const int16_t x = word(0), y = word(2), z = word(4);
        const Matrix& m = matrix_[index];
        out16((static_cast<int32_t>(x) * m[0][0] >> 15) +
              (static_cast<int32_t>(y) * m[0][1] >> 15) +
              (static_cast<int32_t>(z) * m[0][2] >> 15));
    }
    static int16_t saturate16(int64_t value) {
        if (value > 32767) return 32767;
        if (value < -32767) return -32767;
        return static_cast<int16_t>(value);
    }
    static int16_t q15mul(int32_t a, int32_t b) {
        return static_cast<int16_t>((static_cast<int64_t>(a) * b) >> 15);
    }
    static int16_t wrap16(int32_t value) {
        return static_cast<int16_t>(static_cast<uint16_t>(value));
    }
    static void normalize(int16_t value, int16_t& coefficient, int16_t& exponent) {
        if (value == 0) {
            coefficient = 0;
            exponent = static_cast<int16_t>(exponent - 15);
            return;
        }
        if (value == -1) {
            coefficient = static_cast<int16_t>(0x8000);
            exponent = static_cast<int16_t>(exponent - 15);
            return;
        }
        int shift = 0;
        uint16_t bits = static_cast<uint16_t>(value);
        if (value >= 0) {
            while (shift < 15 && (bits & 0x4000u) == 0) { bits <<= 1; ++shift; }
        } else {
            while (shift < 15 && (bits & 0x4000u) != 0) { bits <<= 1; ++shift; }
        }
        coefficient = static_cast<int16_t>(bits);
        exponent = static_cast<int16_t>(exponent - shift);
    }
    static void normalizeDouble(int32_t value, int16_t& coefficient, int16_t& exponent) {
        if (value == 0) { coefficient = 0; exponent = 30; return; }
        int shift = 0;
        int64_t v = value;
        while (shift < 30) {
            const int64_t top = (v >> 29) & 3;
            if (top == 1 || top == 2) break;
            v <<= 1;
            ++shift;
        }
        coefficient = wrap16(static_cast<int32_t>(v >> 15));
        exponent = static_cast<int16_t>(shift);
    }
    static int16_t truncateFp(int16_t coefficient, int16_t exponent) {
        if (exponent > 0) {
            if (coefficient > 0) return 32767;
            if (coefficient < 0) return -32767;
            return 0;
        }
        if (exponent < 0) {
            if (exponent < -15) return 0;
            return static_cast<int16_t>(static_cast<int32_t>(coefficient) >> -exponent);
        }
        return coefficient;
    }
    static void inverseFp(int16_t coefficient, int16_t exponent,
                          int16_t& outCoefficient, int16_t& outExponent) {
        static constexpr std::array<int16_t, 128> kInverseSeed = {
            0x7fff,0x7f02,0x7e08,0x7d12,0x7c1f,0x7b30,0x7a45,0x795d,
            0x7878,0x7797,0x76ba,0x75df,0x7507,0x7433,0x7361,0x7293,
            0x71c7,0x70fe,0x7038,0x6f75,0x6eb4,0x6df6,0x6d3a,0x6c81,
            0x6bca,0x6b16,0x6a64,0x69b4,0x6907,0x685b,0x67b2,0x670b,
            0x6666,0x65c4,0x6523,0x6484,0x63e7,0x634c,0x62b3,0x621c,
            0x6186,0x60f2,0x6060,0x5fd0,0x5f41,0x5eb5,0x5e29,0x5d9f,
            0x5d17,0x5c91,0x5c0c,0x5b88,0x5b06,0x5a85,0x5a06,0x5988,
            0x590b,0x5890,0x5816,0x579d,0x5726,0x56b0,0x563b,0x55c8,
            0x5555,0x54e4,0x5474,0x5405,0x5398,0x532b,0x52bf,0x5255,
            0x51ec,0x5183,0x511c,0x50b6,0x5050,0x4fec,0x4f89,0x4f26,
            0x4ec5,0x4e64,0x4e05,0x4da6,0x4d48,0x4cec,0x4c90,0x4c34,
            0x4bda,0x4b81,0x4b28,0x4ad0,0x4a79,0x4a23,0x49cd,0x4979,
            0x4925,0x48d1,0x487f,0x482d,0x47dc,0x478c,0x473c,0x46ed,
            0x469f,0x4651,0x4604,0x45b8,0x456c,0x4521,0x44d7,0x448d,
            0x4444,0x43fc,0x43b4,0x436d,0x4326,0x42e0,0x429a,0x4255,
            0x4211,0x41cd,0x4189,0x4146,0x4104,0x40c2,0x4081,0x4040
        };
        if (coefficient == 0) { outCoefficient = 0x7fff; outExponent = 0x2f; return; }
        int sign = 1;
        int32_t c = coefficient;
        if (c < 0) {
            if (c < -32767) c = -32767;
            c = -c;
            sign = -1;
        }
        while (c < 0x4000) { c <<= 1; --exponent; }
        if (c == 0x4000) {
            if (sign > 0) outCoefficient = 0x7fff;
            else { outCoefficient = -0x4000; --exponent; }
        } else {
            int16_t i = kInverseSeed[static_cast<std::size_t>((c - 0x4000) >> 7)];
            for (int iteration = 0; iteration < 2; ++iteration) {
                const int32_t ci = (c * static_cast<int32_t>(i)) >> 15;
                const int32_t correction = (-static_cast<int32_t>(i) * ci) >> 15;
                i = wrap16((static_cast<int32_t>(i) + correction) << 1);
            }
            outCoefficient = wrap16(static_cast<int32_t>(i) * sign);
        }
        outExponent = static_cast<int16_t>(1 - exponent);
    }
    static int16_t normalizeExponent(int16_t value) {
        if (value == 0 || value == -1) return -15;
        int shift = 0;
        int16_t v = value;
        if (v >= 0) {
            while (shift < 15 && (static_cast<uint16_t>(v) & 0x4000u) == 0) {
                v = static_cast<int16_t>(static_cast<uint16_t>(v) << 1);
                ++shift;
            }
        } else {
            while (shift < 15 && (static_cast<uint16_t>(v) & 0x4000u) != 0) {
                v = static_cast<int16_t>(static_cast<uint16_t>(v) << 1);
                ++shift;
            }
        }
        return static_cast<int16_t>(-shift);
    }
    static int16_t divSigned(int64_t numerator, int64_t denominator) {
        if (denominator == 0) return numerator < 0 ? -32767 : 32767;
        return saturate16(numerator / denominator);
    }
    static int16_t maxZenithForExponent(int16_t exponent) {
        static constexpr std::array<int16_t, 16> kMaxAzs = {
            0x38b4, 0x38b7, 0x38ba, 0x38be, 0x38c0, 0x38c4, 0x38c7, 0x38ca,
            0x38ce, 0x38d0, 0x38d4, 0x38d7, 0x38da, 0x38dd, 0x38e0, 0x38e4
        };
        const int index = std::clamp<int>(-exponent, 0, 15);
        return kMaxAzs[static_cast<std::size_t>(index)];
    }
    void rebuildProjectionState() {
        if (!projection_valid_) return;
        const int16_t fx = projection_params_[0];
        const int16_t fy = projection_params_[1];
        const int16_t fz = projection_params_[2];
        const int16_t lfe = projection_params_[3];
        const int16_t les = projection_params_[4];
        const int16_t aas = projection_params_[5];
        const int16_t azs = projection_params_[6];
        sin_aas_ = q15sin(aas);
        cos_aas_ = q15cos(aas);
        sin_azs_ = q15sin(azs);
        cos_azs_ = q15cos(azs);
        nx_ = q15mul(sin_azs_, -sin_aas_);
        ny_ = q15mul(sin_azs_,  cos_aas_);
        nz_ = q15mul(cos_azs_, 0x7fff);
        centre_x_ = wrap16(static_cast<int32_t>(fx) + q15mul(lfe, nx_));
        centre_y_ = wrap16(static_cast<int32_t>(fy) + q15mul(lfe, ny_));
        centre_z_ = wrap16(static_cast<int32_t>(fz) + q15mul(lfe, nz_));
        ground_x_ = wrap16(static_cast<int32_t>(centre_x_) - q15mul(les, nx_));
        ground_y_ = wrap16(static_cast<int32_t>(centre_y_) - q15mul(les, ny_));
        ground_z_ = wrap16(static_cast<int32_t>(centre_z_) - q15mul(les, nz_));
        e_les_ = 0;
        normalize(les, c_les_, e_les_);
        g_les_ = les;
        vplane_e_ = 0;
        normalize(centre_z_, vplane_c_, vplane_e_);
        int16_t maxAzs = maxZenithForExponent(vplane_e_);
        int32_t clipped = azs;
        if (clipped < 0) {
            maxAzs = static_cast<int16_t>(-maxAzs);
            if (clipped < static_cast<int32_t>(maxAzs) + 1) clipped = static_cast<int32_t>(maxAzs) + 1;
        } else if (clipped > maxAzs) {
            clipped = maxAzs;
        }
        clipped_azs_ = static_cast<int16_t>(clipped);
        sin_azs_clipped_ = q15sin(clipped_azs_);
        cos_azs_clipped_ = q15cos(clipped_azs_);
        cos_azs_clipped_base_ = cos_azs_clipped_;
        int16_t c = vplane_c_;
        int16_t e = vplane_e_;
        inverseFp(cos_azs_clipped_, 0, sec1_c_, sec1_e_);
        c = q15mul(c, sec1_c_);
        normalize(c, c, e);
        e = static_cast<int16_t>(e + sec1_e_);
        c = q15mul(truncateFp(c, e), sin_azs_clipped_);
        centre_x_ = wrap16(static_cast<int32_t>(centre_x_) + q15mul(c, sin_aas_));
        centre_y_ = wrap16(static_cast<int32_t>(centre_y_) - q15mul(c, cos_aas_));
        int16_t correctedCos = cos_azs_clipped_;
        if (azs != clipped_azs_ || azs == maxAzs) {
            int16_t inputAzs = azs == -32768 ? -32767 : azs;
            int16_t delta = static_cast<int16_t>(inputAzs - maxAzs);
            if (delta >= 0) --delta;
            const int16_t aux = static_cast<int16_t>(~static_cast<uint16_t>(wrap16(static_cast<int32_t>(delta) << 2)));
            int16_t t = q15mul(aux, 0x14ac);
            t = wrap16(static_cast<int32_t>(q15mul(t, aux)) + 0x6488);
            const int16_t vof = wrap16(-q15mul(q15mul(t, aux), les));
            v_offset_ = vof;
            const int16_t q = q15mul(aux, aux);
            const int16_t corr = wrap16(static_cast<int32_t>(q15mul(q, 0x0a26)) + 0x277a);
            correctedCos = wrap16(static_cast<int32_t>(correctedCos) + q15mul(q15mul(q, corr), correctedCos));
        } else {
            v_offset_ = 0;
        }
        cos_azs_clipped_ = correctedCos;
        inverseFp(cos_azs_clipped_base_, 0, sec1_c_, sec1_e_);
        inverseFp(cos_azs_clipped_, 0, sec2_c_, sec2_e_);
    }
    void setupProjection() {
        for (unsigned i = 0; i < 7; ++i) projection_params_[i] = word(i * 2);
        projection_valid_ = true;
        rebuildProjectionState();
        const int16_t vof = v_offset_;
        v_offset_ = q15mul(projection_params_[4], cos_azs_clipped_);
        int16_t csec = 0, e = 0;
        inverseFp(sin_azs_clipped_, 0, csec, e);
        int16_t c = 0;
        int16_t ve = 0;
        normalize(v_offset_, c, ve);
        c = q15mul(c, csec);
        normalize(c, c, ve);
        ve = static_cast<int16_t>(ve + e);
        if (c == -32768) { c >>= 1; ++ve; }
        const int16_t vva = truncateFp(static_cast<int16_t>(-c), ve);
        out16(vof);
        out16(vva);
        out16(centre_x_);
        out16(centre_y_);
    }
    void emitRaster(int16_t vs) {
        if (!projection_valid_) { out16(0); out16(0); out16(0); out16(0); return; }
        const int16_t denom = wrap16(static_cast<int32_t>(q15mul(vs, sin_azs_)) + v_offset_);
        int16_t c = 0, e = 7;
        inverseFp(denom, e, c, e);
        e = static_cast<int16_t>(e + vplane_e_);
        const int16_t c1 = q15mul(c, vplane_c_);
        int16_t e1 = static_cast<int16_t>(e + sec2_e_);
        int16_t scale = 0;
        int16_t scaleE = e;
        normalize(c1, scale, scaleE);
        scale = truncateFp(scale, scaleE);
        int16_t secScale = q15mul(c1, sec2_c_);
        normalize(secScale, secScale, e1);
        secScale = truncateFp(secScale, e1);
        out16(q15mul(scale, cos_aas_));
        out16(q15mul(secScale, static_cast<int16_t>(-sin_aas_)));
        out16(q15mul(scale, sin_aas_));
        out16(q15mul(secScale, cos_aas_));
    }
    void projectPoint() {
        if (!projection_valid_) { out16(0); out16(0); out16(0); return; }
        const int32_t dx = static_cast<int32_t>(word(0)) - ground_x_;
        const int32_t dy = static_cast<int32_t>(word(2)) - ground_y_;
        const int32_t dz = static_cast<int32_t>(word(4)) - ground_z_;
        int16_t px=0, py=0, pz=0, ex=0, ey=0, ez=0;
        normalizeDouble(dx, px, ex); normalizeDouble(dy, py, ey); normalizeDouble(dz, pz, ez);
        px >>= 1; --ex; py >>= 1; --ey; pz >>= 1; --ez;
        int16_t refE = std::min({ex, ey, ez});
        auto shiftCoeff=[&](int16_t c,int delta)->int16_t{
            if(delta<=0) return c;
            if(delta>=15) return c<0?-1:0;
            return static_cast<int16_t>(static_cast<int32_t>(c)>>delta);
        };
        px=shiftCoeff(px,ex-refE); py=shiftCoeff(py,ey-refE); pz=shiftCoeff(pz,ez-refE);
        const int16_t normal = wrap16(-(q15mul(px,nx_)+q15mul(py,ny_)+q15mul(pz,nz_)));
        int64_t denorm = normal;
        int shift = 16-refE;
        if(shift>=0) denorm <<= std::min(shift,30); else denorm >>= std::min(-shift,30);
        if(denorm==-1) denorm=0;
        denorm >>= 1;
        const int32_t depth = static_cast<int32_t>(static_cast<uint16_t>(g_les_)) + static_cast<int32_t>(denorm);
        if(depth==0){out16(32767);out16(32767);out16(32767);return;}
        const int64_t scaleQ15 = (static_cast<int64_t>(static_cast<uint16_t>(g_les_)) << 15) / depth;
        const int16_t scale = saturate16(scaleQ15);
        const int16_t hc = wrap16(q15mul(px,cos_aas_)+q15mul(py,sin_aas_));
        const int16_t vc = wrap16(q15mul(px,q15mul(cos_azs_,static_cast<int16_t>(-sin_aas_)))+
                                  q15mul(py,q15mul(cos_azs_,cos_aas_))+
                                  q15mul(pz,static_cast<int16_t>(-sin_azs_)));
        out16(saturate16((static_cast<int64_t>(hc)*scale)>>15));
        out16(saturate16((static_cast<int64_t>(vc)*scale)>>15));
        out16(saturate16(static_cast<int64_t>(scale)>>7));
    }
    void targetPoint() {
        if (!projection_valid_) { out16(0); out16(0); return; }
        const int16_t h = word(0), v = word(2);
        const int16_t denom = wrap16(static_cast<int32_t>(q15mul(v, sin_azs_)) + v_offset_);
        int16_t c=0,e=8;
        inverseFp(denom,e,c,e);
        e=static_cast<int16_t>(e+vplane_e_);
        const int16_t c1=q15mul(c,vplane_c_);
        int16_t e1=static_cast<int16_t>(e+sec1_e_);
        int16_t hc=c1, he=e;
        normalize(hc,hc,he);
        hc=truncateFp(hc,he);
        const int16_t h8=wrap16(static_cast<int32_t>(h)<<8);
        hc=q15mul(hc,h8);
        int32_t x=static_cast<int32_t>(centre_x_)+q15mul(hc,cos_aas_);
        int32_t y=static_cast<int32_t>(centre_y_)-q15mul(hc,sin_aas_);
        int16_t vc=q15mul(c1,sec1_c_);
        normalize(vc,vc,e1);
        vc=truncateFp(vc,e1);
        const int16_t v8=wrap16(static_cast<int32_t>(v)<<8);
        vc=q15mul(vc,v8);
        x+=q15mul(vc,static_cast<int16_t>(-sin_aas_));
        y+=q15mul(vc,cos_aas_);
        out16(wrap16(x)); out16(wrap16(y));
    }
    void execute() {
        output_.clear(); outputPos_ = 0;
        switch (command_) {
        case 0x02: setupProjection(); break;
        case 0x0a: raster_vs_ = word(0); emitRaster(raster_vs_++); break;
        case 0x06: projectPoint(); break;
        case 0x0e: targetPoint(); break;
        case 0x00: case 0x20: {
            int32_t result = (static_cast<int32_t>(word(0)) * word(2)) >> 15;
            if (command_ == 0x20) ++result;
            out16(result);
            break;
        }
        case 0x10: {
            int16_t coefficient = 0, exponent = 0;
            inverseFp(word(0), word(2), coefficient, exponent);
            out16(coefficient);
            out16(exponent);
            break;
        }
        case 0x04: {
            const int16_t a = word(0), r = word(2);
            out16((static_cast<int32_t>(q15sin(a)) * static_cast<uint16_t>(r)) >> 15);
            out16((static_cast<int32_t>(q15cos(a)) * static_cast<uint16_t>(r)) >> 15);
            break;
        }
        case 0x08: {
            const int32_t x = word(0), y = word(2), z = word(4);
            const uint32_t radius2 = static_cast<uint32_t>((x*x + y*y + z*z) << 1);
            out16(radius2 & 0xffff); out16(radius2 >> 16); break;
        }
        case 0x18: case 0x38: {
            const int32_t x=word(0), y=word(2), z=word(4), r=word(6);
            int32_t d = (x*x + y*y + z*z - r*r) >> 15;
            if (command_ == 0x38) ++d;
            out16(static_cast<int32_t>(d)); break;
        }
        case 0x28: {
            const double x=word(0), y=word(2), z=word(4);
            long d = std::lround(std::sqrt(x*x+y*y+z*z));
            if (d > 32767) d = 32767;
            out16(static_cast<int32_t>(d)); break;
        }
        case 0x0c: {
            const int16_t a=word(0), x=word(2), y=word(4);
            const int16_t s=q15sin(a), c=q15cos(a);
            out16((static_cast<int32_t>(y)*s >> 15) + (static_cast<int32_t>(x)*c >> 15));
            out16((static_cast<int32_t>(y)*c >> 15) - (static_cast<int32_t>(x)*s >> 15));
            break;
        }
        case 0x1c: {
            int32_t vx=word(6), vy=word(8), vz=word(10);
            const int16_t az=word(0), ay=word(2), ax=word(4);
            int16_t s=q15sin(az), c=q15cos(az);
            int32_t nx=(vy*s>>15)+(vx*c>>15), ny=(vy*c>>15)-(vx*s>>15); vx=nx; vy=ny;
            s=q15sin(ay); c=q15cos(ay); int32_t nz=(vx*s>>15)+(vz*c>>15); nx=(vx*c>>15)-(vz*s>>15); vx=nx; vz=nz;
            s=q15sin(ax); c=q15cos(ax); ny=(vz*s>>15)+(vy*c>>15); nz=(vz*c>>15)-(vy*s>>15);
            out16(vx); out16(ny); out16(nz); break;
        }
        case 0x01: setMatrix(0); break;
        case 0x11: setMatrix(1); break;
        case 0x21: setMatrix(2); break;
        case 0x0d: matrixForward(0); break;
        case 0x1d: matrixForward(1); break;
        case 0x2d: matrixForward(2); break;
        case 0x03: matrixReverse(0); break;
        case 0x13: matrixReverse(1); break;
        case 0x23: matrixReverse(2); break;
        case 0x0b: scalar(0); break;
        case 0x1b: scalar(1); break;
        case 0x2b: scalar(2); break;
        case 0x14: {
            const int16_t zr=word(0), xr=word(2), yr=word(4), u=word(6), f=word(8), l=word(10);
            const int16_t sx=q15sin(xr), cx=q15cos(xr), sy=q15sin(yr), cy=q15cos(yr);
            int16_t secC=0,secE=0; inverseFp(cx,0,secC,secE);
            auto applyExp=[](int32_t c,int e)->int16_t{
                if(e>0){ if(e>=15) return c<0?-32767:32767; c<<=e; }
                else if(e<0){ c>>=std::min(-e,15); }
                return saturate16(c);
            };
            const int32_t znum=q15mul(u,cy)-q15mul(f,sy);
            const int16_t dz=applyExp(q15mul(znum,secC),secE);
            const int16_t dx=wrap16(static_cast<int32_t>(q15mul(u,sy))+q15mul(f,cy));
            const int32_t ybase=q15mul(u,cy)+q15mul(f,sy);
            const int16_t tanx=applyExp(q15mul(sx,secC),secE);
            const int16_t dy=wrap16(-q15mul(static_cast<int16_t>(ybase),tanx));
            out16(wrap16(static_cast<int32_t>(zr)+dz));
            out16(wrap16(static_cast<int32_t>(xr)+dx));
            out16(wrap16(static_cast<int32_t>(yr)+dy+l));
            break;
        }
        case 0x0f: out16(0); break;
        case 0x2f: out16(0x0100); break;
        default: break;
        }
    }
    DSP1Map map_;
    uint8_t command_ = 0;
    std::size_t expected_ = 0;
    std::vector<uint8_t> input_;
    std::vector<uint8_t> output_;
    std::size_t outputPos_ = 0;
    bool waiting_ = true;
    std::array<Matrix,3> matrix_{};
    bool projection_valid_ = false;
    std::array<int16_t, 7> projection_params_{};
    int16_t raster_vs_ = 0;
    int16_t sin_aas_ = 0, cos_aas_ = 0;
    int16_t sin_azs_ = 0, cos_azs_ = 0;
    int16_t sin_azs_clipped_ = 0, cos_azs_clipped_ = 0, cos_azs_clipped_base_ = 0;
    int16_t nx_ = 0, ny_ = 0, nz_ = 0;
    int16_t centre_x_ = 0, centre_y_ = 0, centre_z_ = 0;
    int16_t ground_x_ = 0, ground_y_ = 0, ground_z_ = 0;
    int16_t v_offset_ = 0;
    int16_t clipped_azs_ = 0;
    int16_t vplane_c_ = 0, vplane_e_ = 0;
    int16_t c_les_ = 0, e_les_ = 0, g_les_ = 0;
    int16_t sec1_c_ = 0, sec1_e_ = 0, sec2_c_ = 0, sec2_e_ = 0;
    uint32_t masterHz_ = 21477272u;
    uint64_t dsp_frac_ = 0;
    uint32_t busy_cycles_ = 0;
    uint32_t raster_next_cycles_ = 0;
    std::size_t input_word_index_ = 0, output_word_index_ = 0;
    bool pending_execute_ = false;
};
}
std::unique_ptr<CartridgeDevice> createDSP1Device(DSP1Map map, uint32_t masterClockHz) {
    return std::make_unique<DSP1Device>(map, masterClockHz);
}
