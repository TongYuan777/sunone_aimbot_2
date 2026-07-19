#ifndef GAMEPAD_VIGEM_H
#define GAMEPAD_VIGEM_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ViGEmClient 动态加载的内部状态
struct ViGEmState;

// Xbox 360 手柄按钮位掩码（与 XINPUT_GAMEPAD_* 一致）
namespace GamepadButton
{
    constexpr std::uint16_t DPAD_UP = 0x0001;
    constexpr std::uint16_t DPAD_DOWN = 0x0002;
    constexpr std::uint16_t DPAD_LEFT = 0x0004;
    constexpr std::uint16_t DPAD_RIGHT = 0x0008;
    constexpr std::uint16_t START = 0x0010;
    constexpr std::uint16_t BACK = 0x0020;
    constexpr std::uint16_t LEFT_THUMB = 0x0040;
    constexpr std::uint16_t RIGHT_THUMB = 0x0080;
    constexpr std::uint16_t LEFT_SHOULDER = 0x0100;
    constexpr std::uint16_t RIGHT_SHOULDER = 0x0200;
    constexpr std::uint16_t A = 0x1000;
    constexpr std::uint16_t B = 0x2000;
    constexpr std::uint16_t X = 0x4000;
    constexpr std::uint16_t Y = 0x8000;
}

// 手柄按键名称（用于配置）
struct GamepadButtonName
{
    static const char* A();
    static const char* B();
    static const char* X();
    static const char* Y();
    static const char* LEFT_SHOULDER();
    static const char* RIGHT_SHOULDER();
    static const char* LEFT_TRIGGER();
    static const char* RIGHT_TRIGGER();
    static const char* LEFT_THUMB();
    static const char* RIGHT_THUMB();
    static const char* START();
    static const char* BACK();
    static const char* DPAD_UP();
    static const char* DPAD_DOWN();
    static const char* DPAD_LEFT();
    static const char* DPAD_RIGHT();

    // 获取所有可选的按键名称（用于 ImGui 下拉框）
    static std::vector<std::string> All();
};

// GamepadViGEm 类：读取真实手柄（XInput）+ 输出虚拟手柄（ViGEm）
// - XInput 轮询真实手柄按键状态
// - ViGEm 创建虚拟 Xbox 360 控制器，输出摇杆移动
class GamepadViGEm
{
public:
    GamepadViGEm(int playerIndex,
                 float stickScale,
                 int deadzone,
                 const std::string& aimButton,
                 const std::string& shootButton,
                 const std::string& zoomButton);
    ~GamepadViGEm();

    // 打开设备（连接 XInput + 创建虚拟手柄）
    bool open();
    void close();
    bool isOpen() const;

    // 移动（增量，鼠标 counts 语义）。内部转换为摇杆偏移
    bool move(int dx, int dy);

    // 虚拟手柄扳机/按键
    bool leftDown();
    bool leftUp();

    // 物理手柄按键状态查询
    bool aimingActive() const;
    bool shootingActive() const;
    bool zoomingActive() const;
    bool isButtonPressed(const std::string& name) const;

    // 连接状态
    bool isPhysicalConnected() const;
    bool isVirtualConnected() const;

    // 运行时更新配置
    void updateConfig(float stickScale, int deadzone,
                      const std::string& aimButton,
                      const std::string& shootButton,
                      const std::string& zoomButton);

    // -------- 手柄模式扩展 --------

    // 扫描 XInput 0-3 索引，返回当前已连接物理手柄的索引列表。
    // 该函数不依赖实例（可直接在 UI 选择阶段调用），内部仅做轻量探测。
    static std::vector<int> getConnectedGamepadIndices();

    // 返回当前物理手柄按下的按键名称列表（如 {"A","RT"}）。
    // 扳机按下超过阈值时也会被报告。物理手柄未连接时返回空。
    std::vector<std::string> getCurrentlyPressedButtons() const;

    // 按键捕获：启动后，下一次检测到任意按键按下会被记录。
    // 调用方通过 pollCapturedButton() 取走结果并自动结束捕获。
    void beginCapture();
    void cancelCapture();
    bool isCapturing() const;
    // 返回捕获到的按键名；若尚未捕获到则返回空字符串。
    // 取走结果后内部状态清空，捕获流程自动结束。
    std::string pollCapturedButton();

private:
    // XInput 轮询线程
    void pollingThreadFunc();

    // ViGEm 动态加载
    bool loadViGEm();
    void unloadViGEm();

    // 按键名称 -> XInput 按钮/扳机
    bool checkButton(const std::string& name) const;
    // 收集当前所有按下的按键名到 out（不加锁版本，调用者持锁）
    void collectPressedButtonsLocked(std::vector<std::string>& out) const;
    // 在轮询线程内处理捕获事件（调用者已持 stateMutex_）
    void updateCaptureLocked();

    // 配置
    int playerIndex_;
    float stickScale_;
    int deadzone_;
    std::string aimButton_;
    std::string shootButton_;
    std::string zoomButton_;

    // 状态
    std::atomic<bool> opened_{ false };
    std::atomic<bool> physicalConnected_{ false };
    std::atomic<bool> virtualConnected_{ false };

    // 物理手柄状态（由轮询线程更新）
    mutable std::mutex stateMutex_;
    std::uint16_t buttons_{ 0 };
    std::uint8_t leftTrigger_{ 0 };
    std::uint8_t rightTrigger_{ 0 };
    std::int16_t thumbLX_{ 0 };
    std::int16_t thumbLY_{ 0 };

    std::atomic<bool> aimingActive_{ false };
    std::atomic<bool> shootingActive_{ false };
    std::atomic<bool> zoomingActive_{ false };

    // 按键捕获状态
    std::atomic<bool> capturing_{ false };
    std::mutex captureMutex_;
    std::string capturedButton_;
    bool hasCaptured_{ false };

    // 轮询线程
    std::thread pollThread_;
    std::atomic<bool> stopFlag_{ false };

    // ViGEm 动态加载状态
    void* vigemLibrary_ = nullptr;       // HMODULE
    ViGEmState* vigem_ = nullptr;
    void* vigemTarget_ = nullptr;        // PVIGEM_TARGET

    // 摇杆累积偏移（用于把增量移动转换为持续摇杆偏移）
    mutable std::mutex stickMutex_;
    float stickOffsetX_ = 0.0f;
    float stickOffsetY_ = 0.0f;
    std::chrono::steady_clock::time_point lastStickUpdate_;

    // 禁止拷贝
    GamepadViGEm(const GamepadViGEm&) = delete;
    GamepadViGEm& operator=(const GamepadViGEm&) = delete;
};

#endif // GAMEPAD_VIGEM_H
