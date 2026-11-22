#pragma once
#include <array>
#include <cstdint>
class Controller {
public:
    enum class DeviceType : uint8_t {
        Standard = 0,
        Mouse = 1,
        Multitap = 2,
        SuperScope = 3,
    };
    enum Button {
        B = 0,
        Y,
        Select,
        Start,
        Up,
        Down,
        Left,
        Right,
        A,
        X,
        L,
        R,
        ButtonCount
    };
    enum class MouseButton : uint8_t { Left = 0, Right = 1 };
    enum class ScopeButton : uint8_t { Fire = 0, Cursor = 1, Turbo = 2, Pause = 3 };
    Controller();
    virtual ~Controller();
    void reset();
    void setDeviceType(DeviceType type);
    DeviceType deviceType() const { return device_type_; }
    void setButton(int button, bool pressed);
    void setMultitapButton(int subPort, int button, bool pressed);
    void setMultitapConnected(int subPort, bool connected);
    void addMouseMotion(int dx, int dy);
    void setMouseButton(MouseButton button, bool pressed);
    uint8_t mouseSensitivity() const { return mouse_sensitivity_; }
    void setSuperScopeAim(int x, int y, bool onScreen = true);
    void setSuperScopeButton(ScopeButton button, bool pressed);
    bool lightGunActive() const { return device_type_ == DeviceType::SuperScope && scope_on_screen_; }
    virtual void setStrobe(bool high);
    virtual void latch();
    virtual uint8_t readDataLines();
    virtual uint8_t readData();
    uint16_t snapshot() const;
    uint16_t autoJoyValue() const;
    void setIoPort(uint8_t value) { io_port_ = value; }
    bool lightGunLatchAt(int scanline, int dot) const;
    friend class SNES;
private:
    static uint16_t buttonWord(const bool* buttons);
    void captureButtons();
    void captureMultitap();
    void captureMouse();
    void captureSuperScope();
    uint8_t readStandardLines();
    uint8_t readMouseLines();
    uint8_t readMultitapLines();
    uint8_t readSuperScopeLines();
    DeviceType device_type_ = DeviceType::Standard;
    bool buttons_[ButtonCount] = {};
    uint16_t shift_reg_ = 0;
    uint8_t shift_index_ = 0;
    bool strobe_ = false;
    uint8_t io_port_ = 0xFF;
    std::array<std::array<bool, ButtonCount>, 4> multitap_buttons_{};
    std::array<bool, 4> multitap_connected_{{true, true, true, true}};
    std::array<uint16_t, 4> multitap_shift_{};
    std::array<uint8_t, 4> multitap_index_{};
    bool mouse_buttons_[2] = {};
    int32_t mouse_dx_accum_ = 0;
    int32_t mouse_dy_accum_ = 0;
    uint8_t mouse_sensitivity_ = 0;
    uint8_t mouse_x_sign_ = 0;
    uint8_t mouse_y_sign_ = 0;
    uint32_t mouse_shift_ = 0;
    uint8_t mouse_bits_read_ = 0;
    bool scope_fire_ = false;
    bool scope_cursor_ = false;
    bool scope_pause_ = false;
    bool scope_turbo_button_ = false;
    bool scope_turbo_ = false;
    bool scope_fire_edge_ = false;
    bool scope_pause_edge_ = false;
    int16_t scope_x_ = -1;
    int16_t scope_y_ = -1;
    bool scope_on_screen_ = false;
    uint16_t scope_shift_ = 0x00FF;
    uint8_t scope_bits_read_ = 0;
};
