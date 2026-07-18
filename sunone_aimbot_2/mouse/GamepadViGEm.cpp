#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "GamepadViGEm.h"

// ============================================================================
// ViGEmClient 动态加载定义
// ============================================================================

using PVIGEM_CLIENT = void*;
using PVIGEM_TARGET = void*;
using VIGEM_ERROR = ULONG;

constexpr VIGEM_ERROR VIGEM_ERROR_NONE = 0;

// XUSB_REPORT 结构（与 ViGEmClient.h 一致）
#pragma pack(push, 8)
struct XUSB_REPORT_SIM
{
    std::uint16_t wButtons;
    std::uint8_t bLeftTrigger;
    std::uint8_t bRightTrigger;
    std::int16_t sThumbLX;
    std::int16_t sThumbLY;
    std::int16_t sThumbRX;
    std::int16_t sThumbRY;
};
#pragma pack(pop)

struct ViGEmState
{
    // 函数指针
    PVIGEM_CLIENT (*alloc)();
    VIGEM_ERROR (*connect)(PVIGEM_CLIENT);
    void (*disconnect)(PVIGEM_CLIENT);
    void (*free)(PVIGEM_CLIENT);
    PVIGEM_TARGET (*target_x360_alloc)();
    VIGEM_ERROR (*target_add)(PVIGEM_CLIENT, PVIGEM_TARGET);
    VIGEM_ERROR (*target_remove)(PVIGEM_CLIENT, PVIGEM_TARGET);
    void (*target_free)(PVIGEM_TARGET);
    void (*target_x360_update)(PVIGEM_CLIENT, PVIGEM_TARGET, const XUSB_REPORT_SIM*);
    void (*target_set_vid)(PVIGEM_TARGET, std::uint16_t);
    void (*target_set_pid)(PVIGEM_TARGET, std::uint16_t);

    PVIGEM_CLIENT client = nullptr;
};

// ============================================================================
// GamepadButtonName 实现
// ============================================================================

const char* GamepadButtonName::A() { return "A"; }
const char* GamepadButtonName::B() { return "B"; }
const char* GamepadButtonName::X() { return "X"; }
const char* GamepadButtonName::Y() { return "Y"; }
const char* GamepadButtonName::LEFT_SHOULDER() { return "LB"; }
const char* GamepadButtonName::RIGHT_SHOULDER() { return "RB"; }
const char* GamepadButtonName::LEFT_TRIGGER() { return "LT"; }
const char* GamepadButtonName::RIGHT_TRIGGER() { return "RT"; }
const char* GamepadButtonName::LEFT_THUMB() { return "LS"; }
const char* GamepadButtonName::RIGHT_THUMB() { return "RS"; }
const char* GamepadButtonName::START() { return "Start"; }
const char* GamepadButtonName::BACK() { return "Back"; }
const char* GamepadButtonName::DPAD_UP() { return "DUp"; }
const char* GamepadButtonName::DPAD_DOWN() { return "DDown"; }
const char* GamepadButtonName::DPAD_LEFT() { return "DLeft"; }
const char* GamepadButtonName::DPAD_RIGHT() { return "DRight"; }

std::vector<std::string> GamepadButtonName::All()
{
    return {
        A(), B(), X(), Y(),
        LEFT_SHOULDER(), RIGHT_SHOULDER(),
        LEFT_TRIGGER(), RIGHT_TRIGGER(),
        LEFT_THUMB(), RIGHT_THUMB(),
        START(), BACK(),
        DPAD_UP(), DPAD_DOWN(), DPAD_LEFT(), DPAD_RIGHT()
    };
}

// ============================================================================
// GamepadViGEm 实现
// ============================================================================

GamepadViGEm::GamepadViGEm(int playerIndex,
                           float stickScale,
                           int deadzone,
                           const std::string& aimButton,
                           const std::string& shootButton,
                           const std::string& zoomButton)
    : playerIndex_(playerIndex)
    , stickScale_(stickScale)
    , deadzone_(deadzone)
    , aimButton_(aimButton)
    , shootButton_(shootButton)
    , zoomButton_(zoomButton)
    , lastStickUpdate_(std::chrono::steady_clock::now())
{
}

GamepadViGEm::~GamepadViGEm()
{
    close();
}

bool GamepadViGEm::loadViGEm()
{
    HMODULE lib = LoadLibraryA("ViGEmClient.dll");
    if (!lib)
    {
        std::cerr << "[Gamepad] Failed to load ViGEmClient.dll. "
                  << "Please install ViGEmBus driver." << std::endl;
        return false;
    }

    vigem_ = new ViGEmState{};
    vigem_->alloc = reinterpret_cast<PVIGEM_CLIENT(*)()>(
        GetProcAddress(lib, "vigem_alloc"));
    vigem_->connect = reinterpret_cast<VIGEM_ERROR(*)(PVIGEM_CLIENT)>(
        GetProcAddress(lib, "vigem_connect"));
    vigem_->disconnect = reinterpret_cast<void(*)(PVIGEM_CLIENT)>(
        GetProcAddress(lib, "vigem_disconnect"));
    vigem_->free = reinterpret_cast<void(*)(PVIGEM_CLIENT)>(
        GetProcAddress(lib, "vigem_free"));
    vigem_->target_x360_alloc = reinterpret_cast<PVIGEM_TARGET(*)()>(
        GetProcAddress(lib, "vigem_target_x360_alloc"));
    vigem_->target_add = reinterpret_cast<VIGEM_ERROR(*)(PVIGEM_CLIENT, PVIGEM_TARGET)>(
        GetProcAddress(lib, "vigem_target_add"));
    vigem_->target_remove = reinterpret_cast<VIGEM_ERROR(*)(PVIGEM_CLIENT, PVIGEM_TARGET)>(
        GetProcAddress(lib, "vigem_target_remove"));
    vigem_->target_free = reinterpret_cast<void(*)(PVIGEM_TARGET)>(
        GetProcAddress(lib, "vigem_target_free"));
    vigem_->target_x360_update = reinterpret_cast<void(*)(PVIGEM_CLIENT, PVIGEM_TARGET, const XUSB_REPORT_SIM*)>(
        GetProcAddress(lib, "vigem_target_x360_update"));
    vigem_->target_set_vid = reinterpret_cast<void(*)(PVIGEM_TARGET, std::uint16_t)>(
        GetProcAddress(lib, "vigem_target_set_vid"));
    vigem_->target_set_pid = reinterpret_cast<void(*)(PVIGEM_TARGET, std::uint16_t)>(
        GetProcAddress(lib, "vigem_target_set_pid"));

    if (!vigem_->alloc || !vigem_->connect || !vigem_->disconnect || !vigem_->free ||
        !vigem_->target_x360_alloc || !vigem_->target_add || !vigem_->target_remove ||
        !vigem_->target_free || !vigem_->target_x360_update)
    {
        std::cerr << "[Gamepad] ViGEmClient.dll is missing required functions." << std::endl;
        delete vigem_;
        vigem_ = nullptr;
        FreeLibrary(lib);
        return false;
    }

    vigemLibrary_ = lib;
    return true;
}

void GamepadViGEm::unloadViGEm()
{
    if (vigemTarget_ && vigem_ && vigem_->client)
    {
        vigem_->target_remove(vigem_->client, vigemTarget_);
        vigem_->target_free(vigemTarget_);
        vigemTarget_ = nullptr;
    }
    if (vigem_ && vigem_->client)
    {
        vigem_->disconnect(vigem_->client);
        vigem_->free(vigem_->client);
        vigem_->client = nullptr;
    }
    if (vigem_)
    {
        delete vigem_;
        vigem_ = nullptr;
    }
    if (vigemLibrary_)
    {
        FreeLibrary(static_cast<HMODULE>(vigemLibrary_));
        vigemLibrary_ = nullptr;
    }
}

bool GamepadViGEm::open()
{
    if (opened_.load())
        return true;

    // 1) 加载并连接 ViGEmClient
    if (!loadViGEm())
    {
        return false;
    }

    vigem_->client = vigem_->alloc();
    if (!vigem_->client)
    {
        std::cerr << "[Gamepad] vigem_alloc failed." << std::endl;
        unloadViGEm();
        return false;
    }

    VIGEM_ERROR err = vigem_->connect(vigem_->client);
    if (err != VIGEM_ERROR_NONE)
    {
        std::cerr << "[Gamepad] vigem_connect failed: 0x" << std::hex << err << std::endl;
        unloadViGEm();
        return false;
    }

    // 2) 创建虚拟 Xbox 360 控制器
    vigemTarget_ = vigem_->target_x360_alloc();
    if (!vigemTarget_)
    {
        std::cerr << "[Gamepad] vigem_target_x360_alloc failed." << std::endl;
        unloadViGEm();
        return false;
    }

    err = vigem_->target_add(vigem_->client, vigemTarget_);
    if (err != VIGEM_ERROR_NONE)
    {
        std::cerr << "[Gamepad] vigem_target_add failed: 0x" << std::hex << err << std::endl;
        unloadViGEm();
        return false;
    }

    virtualConnected_.store(true);

    // 3) 检查物理手柄是否连接
    XINPUT_STATE xstate{};
    ZeroMemory(&xstate, sizeof(xstate));
    DWORD result = XInputGetState(static_cast<DWORD>(playerIndex_), &xstate);
    physicalConnected_.store(result == ERROR_SUCCESS);

    if (!physicalConnected_.load())
    {
        std::cout << "[Gamepad] Warning: physical controller not connected at index "
                  << playerIndex_ << ". Virtual controller is still active." << std::endl;
    }
    else
    {
        std::cout << "[Gamepad] Physical controller connected at index "
                  << playerIndex_ << std::endl;
    }

    // 4) 启动轮询线程
    stopFlag_.store(false);
    pollThread_ = std::thread(&GamepadViGEm::pollingThreadFunc, this);

    opened_.store(true);
    std::cout << "[Gamepad] Virtual Xbox 360 controller created." << std::endl;
    return true;
}

void GamepadViGEm::close()
{
    if (!opened_.load())
        return;

    stopFlag_.store(true);
    if (pollThread_.joinable())
        pollThread_.join();

    // 发送归零报告
    if (vigem_ && vigem_->client && vigemTarget_)
    {
        XUSB_REPORT_SIM report{};
        vigem_->target_x360_update(vigem_->client, vigemTarget_, &report);
    }

    unloadViGEm();

    physicalConnected_.store(false);
    virtualConnected_.store(false);
    opened_.store(false);

    std::cout << "[Gamepad] Closed." << std::endl;
}

bool GamepadViGEm::isOpen() const
{
    return opened_.load() && virtualConnected_.load();
}

bool GamepadViGEm::move(int dx, int dy)
{
    if (!isOpen())
        return false;

    // 把鼠标 counts 转换为摇杆偏移增量
    // stickScale_ 控制转换比例（counts -> 摇杆值）
    std::lock_guard<std::mutex> lock(stickMutex_);
    stickOffsetX_ += static_cast<float>(dx) * stickScale_;
    stickOffsetY_ += static_cast<float>(dy) * stickScale_;

    // 限制在摇杆范围内
    const float maxStick = 32767.0f;
    stickOffsetX_ = std::clamp(stickOffsetX_, -maxStick, maxStick);
    stickOffsetY_ = std::clamp(stickOffsetY_, -maxStick, maxStick);

    lastStickUpdate_ = std::chrono::steady_clock::now();
    return true;
}

bool GamepadViGEm::leftDown()
{
    // 射击：通过虚拟手柄 RT 扳机或 A 键
    // 这里用 A 键（可在配置中调整）
    return true; // 实际扳机状态由轮询线程同步
}

bool GamepadViGEm::leftUp()
{
    return true;
}

bool GamepadViGEm::isPhysicalConnected() const
{
    return physicalConnected_.load();
}

bool GamepadViGEm::isVirtualConnected() const
{
    return virtualConnected_.load();
}

void GamepadViGEm::updateConfig(float stickScale, int deadzone,
                                const std::string& aimButton,
                                const std::string& shootButton,
                                const std::string& zoomButton)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    stickScale_ = stickScale;
    deadzone_ = deadzone;
    aimButton_ = aimButton;
    shootButton_ = shootButton;
    zoomButton_ = zoomButton;
}

bool GamepadViGEm::checkButton(const std::string& name) const
{
    // 检查数字按钮
    if (name == GamepadButtonName::A()) return (buttons_ & GamepadButton::A) != 0;
    if (name == GamepadButtonName::B()) return (buttons_ & GamepadButton::B) != 0;
    if (name == GamepadButtonName::X()) return (buttons_ & GamepadButton::X) != 0;
    if (name == GamepadButtonName::Y()) return (buttons_ & GamepadButton::Y) != 0;
    if (name == GamepadButtonName::LEFT_SHOULDER()) return (buttons_ & GamepadButton::LEFT_SHOULDER) != 0;
    if (name == GamepadButtonName::RIGHT_SHOULDER()) return (buttons_ & GamepadButton::RIGHT_SHOULDER) != 0;
    if (name == GamepadButtonName::LEFT_THUMB()) return (buttons_ & GamepadButton::LEFT_THUMB) != 0;
    if (name == GamepadButtonName::RIGHT_THUMB()) return (buttons_ & GamepadButton::RIGHT_THUMB) != 0;
    if (name == GamepadButtonName::START()) return (buttons_ & GamepadButton::START) != 0;
    if (name == GamepadButtonName::BACK()) return (buttons_ & GamepadButton::BACK) != 0;
    if (name == GamepadButtonName::DPAD_UP()) return (buttons_ & GamepadButton::DPAD_UP) != 0;
    if (name == GamepadButtonName::DPAD_DOWN()) return (buttons_ & GamepadButton::DPAD_DOWN) != 0;
    if (name == GamepadButtonName::DPAD_LEFT()) return (buttons_ & GamepadButton::DPAD_LEFT) != 0;
    if (name == GamepadButtonName::DPAD_RIGHT()) return (buttons_ & GamepadButton::DPAD_RIGHT) != 0;

    // 检查扳机（阈值触发）
    const int triggerThreshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    if (name == GamepadButtonName::LEFT_TRIGGER()) return leftTrigger_ > triggerThreshold;
    if (name == GamepadButtonName::RIGHT_TRIGGER()) return rightTrigger_ > triggerThreshold;

    return false;
}

bool GamepadViGEm::isButtonPressed(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return checkButton(name);
}

bool GamepadViGEm::aimingActive() const
{
    return aimingActive_.load();
}

bool GamepadViGEm::shootingActive() const
{
    return shootingActive_.load();
}

bool GamepadViGEm::zoomingActive() const
{
    return zoomingActive_.load();
}

void GamepadViGEm::pollingThreadFunc()
{
    XINPUT_STATE xstate{};

    while (!stopFlag_.load())
    {
        // 1) 轮询真实手柄
        ZeroMemory(&xstate, sizeof(xstate));
        DWORD result = XInputGetState(static_cast<DWORD>(playerIndex_), &xstate);
        bool connected = (result == ERROR_SUCCESS);
        physicalConnected_.store(connected);

        if (connected)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            buttons_ = xstate.Gamepad.wButtons;
            leftTrigger_ = xstate.Gamepad.bLeftTrigger;
            rightTrigger_ = xstate.Gamepad.bRightTrigger;
            thumbLX_ = xstate.Gamepad.sThumbLX;
            thumbLY_ = xstate.Gamepad.sThumbLY;

            // 更新自瞄/射击/缩放状态
            aimingActive_.store(checkButton(aimButton_));
            shootingActive_.store(checkButton(shootButton_));
            zoomingActive_.store(checkButton(zoomButton_));
        }
        else
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            buttons_ = 0;
            leftTrigger_ = 0;
            rightTrigger_ = 0;
            thumbLX_ = 0;
            thumbLY_ = 0;
            aimingActive_.store(false);
            shootingActive_.store(false);
            zoomingActive_.store(false);
        }

        // 2) 准备虚拟手柄报告
        XUSB_REPORT_SIM report{};

        if (connected)
        {
            // 透传真实手柄的按键状态到虚拟手柄（除了摇杆，摇杆由自瞄控制）
            report.wButtons = buttons_;
            report.bLeftTrigger = leftTrigger_;
            report.bRightTrigger = rightTrigger_;
            report.sThumbRX = xstate.Gamepad.sThumbRX;
            report.sThumbRY = xstate.Gamepad.sThumbRY;
        }

        // 3) 自瞄时叠加摇杆偏移
        {
            std::lock_guard<std::mutex> lock(stickMutex_);

            // 把摇杆偏移转换为 short 值
            std::int16_t stickX = static_cast<std::int16_t>(std::clamp(
                std::lround(stickOffsetX_), -32767L, 32767L));
            std::int16_t stickY = static_cast<std::int16_t>(std::clamp(
                std::lround(-stickOffsetY_), -32767L, 32767L)); // Y 轴反转

            // 应用死区
            if (std::abs(stickX) < deadzone_) stickX = 0;
            if (std::abs(stickY) < deadzone_) stickY = 0;

            report.sThumbLX = stickX;
            report.sThumbLY = stickY;

            // 摇杆偏移衰减（让摇杆自然回中）
            // 衰减率：每 10ms 衰减 15%
            constexpr float decayRate = 0.85f;
            stickOffsetX_ *= decayRate;
            stickOffsetY_ *= decayRate;

            // 小于阈值时归零
            if (std::abs(stickOffsetX_) < 1.0f) stickOffsetX_ = 0.0f;
            if (std::abs(stickOffsetY_) < 1.0f) stickOffsetY_ = 0.0f;
        }

        // 4) 发送虚拟手柄报告
        if (vigem_ && vigem_->client && vigemTarget_)
        {
            vigem_->target_x360_update(vigem_->client, vigemTarget_, &report);
        }

        // 10ms 轮询周期
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
