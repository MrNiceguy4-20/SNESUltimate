#pragma once
#include <cstdint>
namespace cpu_alu {
struct Result {
    uint16_t value = 0;
    bool carry = false;
    bool overflow = false;
    bool negative = false;
    bool zero = false;
};
inline Result adc(uint16_t lhs, uint16_t rhs, bool carry_in, bool wide, bool decimal)
{
    const uint32_t mask = wide ? 0xFFFFu : 0x00FFu;
    const uint32_t sign = wide ? 0x8000u : 0x0080u;
    lhs = static_cast<uint16_t>(lhs & mask);
    rhs = static_cast<uint16_t>(rhs & mask);
    uint32_t value = 0;
    bool carry = carry_in;
    bool overflow = false;
    if (!decimal) {
        const uint32_t sum = static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs) + (carry ? 1u : 0u);
        value = sum & mask;
        carry = sum > mask;
        overflow = ((~(static_cast<uint32_t>(lhs) ^ static_cast<uint32_t>(rhs)) &
                     (static_cast<uint32_t>(lhs) ^ value) & sign) != 0);
    } else {
        const unsigned digits = wide ? 4u : 2u;
        bool preadjust_sign = false;
        for (unsigned digit = 0; digit < digits; ++digit) {
            const unsigned shift = digit * 4u;
            uint32_t part = ((lhs >> shift) & 0x0Fu) +
                            ((rhs >> shift) & 0x0Fu) +
                            (carry ? 1u : 0u);
            if (digit + 1u == digits) {
                preadjust_sign = (part & 0x08u) != 0;
            }
            if (part > 9u) {
                part += 6u;
                carry = true;
            } else {
                carry = false;
            }
            value |= (part & 0x0Fu) << shift;
        }
        value &= mask;
        const bool lhs_sign = (lhs & sign) != 0;
        const bool rhs_sign = (rhs & sign) != 0;
        overflow = (lhs_sign != preadjust_sign) && (rhs_sign != preadjust_sign);
    }
    Result result;
    result.value = static_cast<uint16_t>(value);
    result.carry = carry;
    result.overflow = overflow;
    result.negative = (value & sign) != 0;
    result.zero = (value & mask) == 0;
    return result;
}
inline Result sbc(uint16_t lhs, uint16_t rhs, bool carry_in, bool wide, bool decimal)
{
    const uint32_t mask = wide ? 0xFFFFu : 0x00FFu;
    const uint32_t sign = wide ? 0x8000u : 0x0080u;
    lhs = static_cast<uint16_t>(lhs & mask);
    rhs = static_cast<uint16_t>(rhs & mask);
    uint32_t value = 0;
    bool carry = false;
    bool overflow = false;
    if (!decimal) {
        const uint32_t borrow = carry_in ? 0u : 1u;
        const int32_t difference = static_cast<int32_t>(lhs) - static_cast<int32_t>(rhs) - static_cast<int32_t>(borrow);
        value = static_cast<uint32_t>(difference) & mask;
        carry = difference >= 0;
        overflow = (((static_cast<uint32_t>(lhs) ^ static_cast<uint32_t>(rhs)) &
                     (static_cast<uint32_t>(lhs) ^ value) & sign) != 0);
    } else {
        struct DecimalByte {
            uint8_t value;
            bool carry;
            bool preadjust_sign;
        };
        auto subByte = [](uint8_t a, uint8_t b, bool cin) -> DecimalByte {
            bool borrow = !cin;
            uint8_t lo = static_cast<uint8_t>((a & 0x0F) - (b & 0x0F) - (borrow ? 1 : 0));
            borrow = lo >= 0x80;
            if (borrow) lo = static_cast<uint8_t>(lo + 10);
            const bool lo_still_negative = lo >= 0x80;
            lo &= 0x0F;
            uint8_t hi = static_cast<uint8_t>((a >> 4) - (b >> 4) - (borrow ? 1 : 0));
            const bool preadjust = (hi & 0x08) != 0;
            borrow = hi >= 0x80;
            if (borrow) hi = static_cast<uint8_t>(hi + 10);
            hi = static_cast<uint8_t>(hi - (lo_still_negative ? 1 : 0));
            hi &= 0x0F;
            return {static_cast<uint8_t>((hi << 4) | lo), !borrow, preadjust};
        };
        const DecimalByte low = subByte(static_cast<uint8_t>(lhs), static_cast<uint8_t>(rhs), carry_in);
        bool preadjust_sign = low.preadjust_sign;
        if (wide) {
            const DecimalByte high = subByte(static_cast<uint8_t>(lhs >> 8), static_cast<uint8_t>(rhs >> 8), low.carry);
            value = static_cast<uint32_t>(low.value) | (static_cast<uint32_t>(high.value) << 8);
            carry = high.carry;
            preadjust_sign = high.preadjust_sign;
        } else {
            value = low.value;
            carry = low.carry;
        }
        value &= mask;
        const bool lhs_sign = (lhs & sign) != 0;
        const bool rhs_sign = (rhs & sign) != 0;
        overflow = (lhs_sign != preadjust_sign) && ((!rhs_sign) != preadjust_sign);
    }
    Result result;
    result.value = static_cast<uint16_t>(value);
    result.carry = carry;
    result.overflow = overflow;
    result.negative = (value & sign) != 0;
    result.zero = (value & mask) == 0;
    return result;
}
}
