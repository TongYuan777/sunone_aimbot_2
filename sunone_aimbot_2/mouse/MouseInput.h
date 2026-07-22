#ifndef MOUSE_INPUT_H
#define MOUSE_INPUT_H

#include <memory>
#include <optional>
#include <string>

class Arduino;
class Config;
class GamepadViGEm;
class GhubMouse;
class KmboxAConnection;
class KmboxNetConnection;
class MakcuConnection;
class RP2350;
class RzctlMouse;
class Teensy41RawHid;

enum class MouseInputMethod
{
    Win32,
    GHub,
    Razer,
    Arduino,
    RP2350,
    Teensy41,
    Teensy41Hid,
    KmboxNet,
    KmboxA,
    Makcu,
    GamepadViGEm
};

std::optional<MouseInputMethod> ParseMouseInputMethod(const std::string& method);
std::string MouseInputMethodName(MouseInputMethod method);

class IMouseInput
{
public:
    virtual ~IMouseInput() = default;

    virtual const char* name() const = 0;
    virtual bool isOpen() const = 0;
    virtual bool move(int dx, int dy) = 0;
    virtual bool leftDown() = 0;
    virtual bool leftUp() = 0;
    virtual bool hasPhysicalButtonState() const { return false; }
    virtual bool keyPressed(const std::string& keyName) { (void)keyName; return false; }
    virtual bool aimingActive() const { return false; }
    virtual bool shootingActive() const { return false; }
    virtual bool zoomingActive() const { return false; }

    // 手柄模式 B：基于目标相对屏幕中心的归一化误差 [-1, 1] 设置虚拟手柄右摇杆。
    // 仅 GamepadViGEm 实现，其它输入方式默认无操作。
    virtual void sendStickByError(float /*error_x*/, float /*error_y*/) {}

    virtual Arduino* arduino() { return nullptr; }
    virtual RP2350* rp2350() { return nullptr; }
    virtual GhubMouse* ghub() { return nullptr; }
    virtual RzctlMouse* razer() { return nullptr; }
    virtual KmboxNetConnection* kmboxNet() { return nullptr; }
    virtual KmboxAConnection* kmboxA() { return nullptr; }
    virtual MakcuConnection* makcu() { return nullptr; }
    virtual Teensy41RawHid* teensy41RawHid() { return nullptr; }
    virtual GamepadViGEm* gamepad() { return nullptr; }
};

std::unique_ptr<IMouseInput> CreateMouseInputDevice(const Config& config);

#endif // MOUSE_INPUT_H
