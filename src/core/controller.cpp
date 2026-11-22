#include "controller.hpp"
#include <algorithm>
#include <cstdlib>
Controller::Controller() { reset(); }
Controller::~Controller() = default;
uint16_t Controller::buttonWord(const bool* buttons) {
    uint16_t value = 0;
    for (int i = 0; i < ButtonCount; ++i) {
        if (buttons[i]) value |= static_cast<uint16_t>(1u << i);
    }
    return value;
}
void Controller::reset() {
    for (bool& button : buttons_) button = false;
    shift_reg_ = 0;
    shift_index_ = 0;
    strobe_ = false;
    io_port_ = 0xFF;
    for (auto& slot : multitap_buttons_) slot.fill(false);
    multitap_connected_.fill(true);
    multitap_shift_.fill(0);
    multitap_index_.fill(0);
    mouse_buttons_[0] = mouse_buttons_[1] = false;
    mouse_dx_accum_ = mouse_dy_accum_ = 0;
    mouse_sensitivity_ = 0;
    mouse_x_sign_ = mouse_y_sign_ = 0;
    mouse_shift_ = 0;
    mouse_bits_read_ = 0;
    scope_fire_ = scope_cursor_ = scope_pause_ = false;
    scope_turbo_button_ = false;
    scope_turbo_ = false;
    scope_fire_edge_ = scope_pause_edge_ = false;
    scope_x_ = scope_y_ = -1;
    scope_on_screen_ = false;
    scope_shift_ = 0x00FF;
    scope_bits_read_ = 0;
}
void Controller::setDeviceType(DeviceType type) {
    if (device_type_ == type) return;
    device_type_ = type;
    shift_reg_ = 0;
    shift_index_ = 0;
    mouse_shift_ = 0;
    mouse_bits_read_ = 0;
    scope_shift_ = 0x00FF;
    scope_bits_read_ = 0;
    multitap_shift_.fill(0);
    multitap_index_.fill(0);
    if (strobe_) latch();
}
void Controller::setButton(int button, bool pressed) {
    if (button < 0 || button >= ButtonCount) return;
    buttons_[button] = pressed;
    if (strobe_ && device_type_ == DeviceType::Standard) captureButtons();
}
void Controller::setMultitapButton(int subPort, int button, bool pressed) {
    if (subPort < 0 || subPort >= 4 || button < 0 || button >= ButtonCount) return;
    multitap_buttons_[static_cast<size_t>(subPort)][static_cast<size_t>(button)] = pressed;
    if (strobe_ && device_type_ == DeviceType::Multitap) captureMultitap();
}
void Controller::setMultitapConnected(int subPort, bool connected) {
    if (subPort < 0 || subPort >= 4) return;
    multitap_connected_[static_cast<size_t>(subPort)] = connected;
}
void Controller::addMouseMotion(int dx, int dy) {
    mouse_dx_accum_ += dx;
    mouse_dy_accum_ += dy;
}
void Controller::setMouseButton(MouseButton button, bool pressed) {
    mouse_buttons_[static_cast<unsigned>(button) & 1u] = pressed;
}
void Controller::setSuperScopeAim(int x, int y, bool onScreen) {
    scope_x_ = static_cast<int16_t>(x);
    scope_y_ = static_cast<int16_t>(y);
    scope_on_screen_ = onScreen;
}
void Controller::setSuperScopeButton(ScopeButton button, bool pressed) {
    switch (button) {
    case ScopeButton::Fire:
        if (pressed && !scope_fire_) scope_fire_edge_ = true;
        scope_fire_ = pressed;
        break;
    case ScopeButton::Cursor:
        scope_cursor_ = pressed;
        break;
    case ScopeButton::Turbo:
        if (pressed && !scope_turbo_button_) scope_turbo_ = !scope_turbo_;
        scope_turbo_button_ = pressed;
        break;
    case ScopeButton::Pause:
        if (pressed && !scope_pause_) scope_pause_edge_ = true;
        scope_pause_ = pressed;
        break;
    }
}
void Controller::captureButtons() {
    shift_reg_ = buttonWord(buttons_);
    shift_index_ = 0;
}
void Controller::captureMultitap() {
    for (size_t i = 0; i < 4; ++i) {
        multitap_shift_[i] = buttonWord(multitap_buttons_[i].data());
        multitap_index_[i] = 0;
    }
}
void Controller::captureMouse() {
    int dx = std::clamp(mouse_dx_accum_, -127, 127);
    int dy = std::clamp(mouse_dy_accum_, -127, 127);
    if (dx != 0) mouse_x_sign_ = dx < 0 ? 0x80 : 0;
    if (dy != 0) mouse_y_sign_ = dy < 0 ? 0x80 : 0;
    int mx = std::abs(dx);
    int my = std::abs(dy);
    static constexpr uint8_t sensitivityLut[2][8] = {
        {0, 1, 2, 3, 8, 10, 12, 21},
        {0, 1, 4, 9, 12, 20, 24, 28},
    };
    if (mouse_sensitivity_ > 0) {
        mx = sensitivityLut[mouse_sensitivity_ - 1][std::min(mx, 7)];
        my = sensitivityLut[mouse_sensitivity_ - 1][std::min(my, 7)];
    }
    const uint8_t b0 = 0x00;
    const uint8_t b1 = static_cast<uint8_t>(0x01 | ((mouse_sensitivity_ & 3u) << 4) |
        (mouse_buttons_[0] ? 0x40 : 0) | (mouse_buttons_[1] ? 0x80 : 0));
    const uint8_t b2 = static_cast<uint8_t>(my | mouse_y_sign_);
    const uint8_t b3 = static_cast<uint8_t>(mx | mouse_x_sign_);
    mouse_shift_ = (static_cast<uint32_t>(b0) << 24) | (static_cast<uint32_t>(b1) << 16) |
        (static_cast<uint32_t>(b2) << 8) | b3;
    mouse_bits_read_ = 0;
    mouse_dx_accum_ -= dx;
    mouse_dy_accum_ -= dy;
}
void Controller::captureSuperScope() {
    const bool fireReport = scope_turbo_ ? scope_fire_ : scope_fire_edge_;
    const bool pauseReport = scope_pause_edge_ && !scope_fire_ && !scope_cursor_;
    uint16_t packet = 0x00FF;
    if (fireReport) packet |= 0x8000;
    if (scope_cursor_) packet |= 0x4000;
    if (scope_turbo_) packet |= 0x2000;
    if (pauseReport) packet |= 0x1000;
    if (!scope_on_screen_) packet |= 0x0100;
    scope_shift_ = packet;
    scope_bits_read_ = 0;
    if (!scope_turbo_) scope_fire_edge_ = false;
    scope_pause_edge_ = false;
}
void Controller::setStrobe(bool high) {
    if (high) {
        switch (device_type_) {
        case DeviceType::Standard: captureButtons(); break;
        case DeviceType::Multitap: captureMultitap(); break;
        case DeviceType::Mouse:
        case DeviceType::SuperScope:
            break;
        }
    } else if (strobe_) {
        switch (device_type_) {
        case DeviceType::Standard: captureButtons(); break;
        case DeviceType::Mouse: captureMouse(); break;
        case DeviceType::Multitap: captureMultitap(); break;
        case DeviceType::SuperScope: captureSuperScope(); break;
        }
    }
    strobe_ = high;
}
void Controller::latch() {
    switch (device_type_) {
    case DeviceType::Standard: captureButtons(); break;
    case DeviceType::Mouse: captureMouse(); break;
    case DeviceType::Multitap: captureMultitap(); break;
    case DeviceType::SuperScope: captureSuperScope(); break;
    }
}
uint8_t Controller::readStandardLines() {
    if (strobe_) {
        captureButtons();
        return static_cast<uint8_t>(shift_reg_ & 1u);
    }
    if (shift_index_ >= 16) return 0x01;
    const uint8_t lines = static_cast<uint8_t>(shift_reg_ & 1u);
    shift_reg_ >>= 1;
    ++shift_index_;
    return lines;
}
uint8_t Controller::readMouseLines() {
    if (strobe_) {
        mouse_sensitivity_ = static_cast<uint8_t>((mouse_sensitivity_ + 1) % 3);
    }
    if (mouse_bits_read_ >= 32) return 0x01;
    const uint8_t bit = static_cast<uint8_t>((mouse_shift_ >> 31) & 1u);
    mouse_shift_ = (mouse_shift_ << 1) | 1u;
    ++mouse_bits_read_;
    return bit;
}
uint8_t Controller::readMultitapLines() {
    if (strobe_) {
        return 0x02;
    }
    const int base = (io_port_ & 0x80) ? 0 : 2;
    uint8_t lines = 0;
    for (int lane = 0; lane < 2; ++lane) {
        const int slot = base + lane;
        const uint8_t index = multitap_index_[static_cast<size_t>(slot)];
        uint8_t bit = 0;
        if (index < 16) {
            bit = static_cast<uint8_t>(multitap_shift_[static_cast<size_t>(slot)] & 1u);
            multitap_shift_[static_cast<size_t>(slot)] >>= 1;
        } else if (index == 16) {
            bit = multitap_connected_[static_cast<size_t>(slot)] ? 1u : 0u;
        }
        if (multitap_index_[static_cast<size_t>(slot)] < 0xFF)
            ++multitap_index_[static_cast<size_t>(slot)];
        lines |= static_cast<uint8_t>(bit << lane);
    }
    return lines;
}
uint8_t Controller::readSuperScopeLines() {
    if (strobe_) captureSuperScope();
    if (scope_bits_read_ >= 16) return 0x01;
    const uint8_t bit = static_cast<uint8_t>((scope_shift_ >> 15) & 1u);
    scope_shift_ = static_cast<uint16_t>((scope_shift_ << 1) | 1u);
    ++scope_bits_read_;
    return bit;
}
uint8_t Controller::readDataLines() {
    switch (device_type_) {
    case DeviceType::Standard: return readStandardLines();
    case DeviceType::Mouse: return readMouseLines();
    case DeviceType::Multitap: return readMultitapLines();
    case DeviceType::SuperScope: return readSuperScopeLines();
    }
    return 0;
}
uint8_t Controller::readData() {
    return static_cast<uint8_t>(readDataLines() & 0x01);
}
uint16_t Controller::snapshot() const {
    return buttonWord(buttons_);
}
uint16_t Controller::autoJoyValue() const {
    uint16_t value = 0;
    if (buttons_[B])      value |= 0x8000;
    if (buttons_[Y])      value |= 0x4000;
    if (buttons_[Select]) value |= 0x2000;
    if (buttons_[Start])  value |= 0x1000;
    if (buttons_[Up])     value |= 0x0800;
    if (buttons_[Down])   value |= 0x0400;
    if (buttons_[Left])   value |= 0x0200;
    if (buttons_[Right])  value |= 0x0100;
    if (buttons_[A])      value |= 0x0080;
    if (buttons_[X])      value |= 0x0040;
    if (buttons_[L])      value |= 0x0020;
    if (buttons_[R])      value |= 0x0010;
    return value;
}
bool Controller::lightGunLatchAt(int scanline, int dot) const {
    if (device_type_ != DeviceType::SuperScope || !scope_on_screen_) return false;
    const int targetX = static_cast<int>(scope_x_) + 10;
    const int targetY = static_cast<int>(scope_y_) - 3;
    if (targetX < 0 || targetX >= 256 || targetY < 0) return false;
    return scanline == targetY && dot == 22 + targetX;
}
