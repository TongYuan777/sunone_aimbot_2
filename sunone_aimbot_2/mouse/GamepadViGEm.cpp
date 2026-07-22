#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include "GamepadViGEm.h"

// ============================================================================
// ViGEmClient 动态加载定义
// ============================================================================

using PVIGEM_CLIENT = void*;
using PVIGEM_TARGET = void*;
using VIGEM_ERROR = ULONG;

// ViGEm 错误码基值。成功返回 0x20000000，而非 0。
// 参考 ViGEmBus 官方头文件：VIGEM_ERROR_NONE = 0x20000000
constexpr VIGEM_ERROR VIGEM_ERROR_NONE = 0x20000000;

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
                           const std::string& zoomButton,
                           int pollIntervalMs)
    : playerIndex_(playerIndex)
    , stickScale_(stickScale)
    , deadzone_(deadzone)
    , aimButton_(aimButton)
    , shootButton_(shootButton)
    , zoomButton_(zoomButton)
    , pollIntervalMs_(pollIntervalMs > 0 ? pollIntervalMs : 10)
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
        const DWORD lastErr = GetLastError();
        std::cerr << "[Gamepad] Failed to load ViGEmClient.dll (0x" << std::hex << lastErr
                  << "). Please install ViGEmBus driver and place ViGEmClient.dll next to ai.exe."
                  << std::endl;
        return false;
    }
    std::cout << "[Gamepad] ViGEmClient.dll loaded." << std::endl;

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
    std::cout << "[Gamepad] vigem_alloc ok." << std::endl;

    VIGEM_ERROR err = vigem_->connect(vigem_->client);
    if (err != VIGEM_ERROR_NONE)
    {
        std::cerr << "[Gamepad] vigem_connect failed: 0x" << std::hex << err << std::endl;
        unloadViGEm();
        return false;
    }
    std::cout << "[Gamepad] vigem_connect ok." << std::endl;

    // 2) 创建虚拟 Xbox 360 控制器
    vigemTarget_ = vigem_->target_x360_alloc();
    if (!vigemTarget_)
    {
        std::cerr << "[Gamepad] vigem_target_x360_alloc failed." << std::endl;
        unloadViGEm();
        return false;
    }
    std::cout << "[Gamepad] vigem_target_x360_alloc ok." << std::endl;

    err = vigem_->target_add(vigem_->client, vigemTarget_);
    if (err != VIGEM_ERROR_NONE)
    {
        std::cerr << "[Gamepad] vigem_target_add failed: 0x" << std::hex << err << std::endl;
        unloadViGEm();
        return false;
    }
    std::cout << "[Gamepad] vigem_target_add ok." << std::endl;

    virtualConnected_.store(true);

    // 3) 启动 Raw Input 后台监听线程（绕过 Windows 前台窗口限制）
    stopFlag_.store(false);
    rawInputThread_ = std::thread(&GamepadViGEm::rawInputThreadFunc, this);

    // 4) 启动轮询线程（处理瞄准状态 + 虚拟手柄输出）
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

    // Raw Input 线程依赖 stopFlag_，join 前先唤醒消息泵
    if (rawInputHwnd_)
        PostMessageW(rawInputHwnd_, WM_NULL, 0, 0);

    if (rawInputThread_.joinable())
        rawInputThread_.join();

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
    {
        static auto lastLog = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now - lastLog > std::chrono::seconds(2))
        {
            std::cerr << "[Gamepad] move() skipped: device not open (opened="
                      << opened_.load() << ", virtualConnected=" << virtualConnected_.load() << ")" << std::endl;
            lastLog = now;
        }
        return false;
    }

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

    // 诊断日志：每 500ms 输出一次最近一次非零移动
    if (dx != 0 || dy != 0)
    {
        static auto lastMoveLog = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        if (now - lastMoveLog > std::chrono::milliseconds(500))
        {
            std::cout << "[Gamepad] move(" << dx << ", " << dy << ") scale=" << stickScale_
                      << " offset=(" << stickOffsetX_ << ", " << stickOffsetY_ << ")" << std::endl;
            lastMoveLog = now;
        }
    }

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
                                const std::string& zoomButton,
                                int pollIntervalMs)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    stickScale_ = stickScale;
    deadzone_ = deadzone;
    aimButton_ = aimButton;
    shootButton_ = shootButton;
    zoomButton_ = zoomButton;
    pollIntervalMs_.store(pollIntervalMs > 0 ? pollIntervalMs : 10);
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

std::vector<int> GamepadViGEm::getConnectedGamepadIndices()
{
    std::vector<int> indices;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        XINPUT_STATE st{};
        ZeroMemory(&st, sizeof(st));
        if (XInputGetState(i, &st) == ERROR_SUCCESS)
            indices.push_back(static_cast<int>(i));
    }
    return indices;
}

void GamepadViGEm::collectPressedButtonsLocked(std::vector<std::string>& out) const
{
    out.clear();

    // 数字按键
    if (buttons_ & GamepadButton::A)            out.emplace_back(GamepadButtonName::A());
    if (buttons_ & GamepadButton::B)            out.emplace_back(GamepadButtonName::B());
    if (buttons_ & GamepadButton::X)            out.emplace_back(GamepadButtonName::X());
    if (buttons_ & GamepadButton::Y)            out.emplace_back(GamepadButtonName::Y());
    if (buttons_ & GamepadButton::LEFT_SHOULDER)  out.emplace_back(GamepadButtonName::LEFT_SHOULDER());
    if (buttons_ & GamepadButton::RIGHT_SHOULDER) out.emplace_back(GamepadButtonName::RIGHT_SHOULDER());
    if (buttons_ & GamepadButton::LEFT_THUMB)   out.emplace_back(GamepadButtonName::LEFT_THUMB());
    if (buttons_ & GamepadButton::RIGHT_THUMB)  out.emplace_back(GamepadButtonName::RIGHT_THUMB());
    if (buttons_ & GamepadButton::START)        out.emplace_back(GamepadButtonName::START());
    if (buttons_ & GamepadButton::BACK)         out.emplace_back(GamepadButtonName::BACK());
    if (buttons_ & GamepadButton::DPAD_UP)      out.emplace_back(GamepadButtonName::DPAD_UP());
    if (buttons_ & GamepadButton::DPAD_DOWN)    out.emplace_back(GamepadButtonName::DPAD_DOWN());
    if (buttons_ & GamepadButton::DPAD_LEFT)    out.emplace_back(GamepadButtonName::DPAD_LEFT());
    if (buttons_ & GamepadButton::DPAD_RIGHT)   out.emplace_back(GamepadButtonName::DPAD_RIGHT());

    // 扳机（超过阈值视为按下）
    const int triggerThreshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    if (leftTrigger_ > triggerThreshold)  out.emplace_back(GamepadButtonName::LEFT_TRIGGER());
    if (rightTrigger_ > triggerThreshold) out.emplace_back(GamepadButtonName::RIGHT_TRIGGER());
}

std::vector<std::string> GamepadViGEm::getCurrentlyPressedButtons() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    std::vector<std::string> result;
    collectPressedButtonsLocked(result);
    return result;
}

void GamepadViGEm::beginCapture()
{
    std::lock_guard<std::mutex> lock(captureMutex_);
    capturedButton_.clear();
    hasCaptured_ = false;
    capturing_.store(true);
}

void GamepadViGEm::cancelCapture()
{
    std::lock_guard<std::mutex> lock(captureMutex_);
    capturedButton_.clear();
    hasCaptured_ = false;
    capturing_.store(false);
}

bool GamepadViGEm::isCapturing() const
{
    return capturing_.load();
}

std::string GamepadViGEm::pollCapturedButton()
{
    std::lock_guard<std::mutex> lock(captureMutex_);
    if (!hasCaptured_)
        return std::string();

    std::string result;
    result.swap(capturedButton_);
    capturedButton_.clear();
    hasCaptured_ = false;
    capturing_.store(false);
    return result;
}

void GamepadViGEm::updateCaptureLocked()
{
    // 调用者已持 stateMutex_
    if (!capturing_.load())
        return;

    // 先收集当前按下的按键（无锁访问 buttons_/triggers_，因为已持 stateMutex_）
    std::vector<std::string> pressed;
    collectPressedButtonsLocked(pressed);
    if (pressed.empty())
        return;

    std::lock_guard<std::mutex> capLock(captureMutex_);
    // 锁内复检：避免与 cancelCapture() 之间的 TOCTOU 竞态
    // （用户在收集按键后、获取锁前点击"停止"，此时不应再写入捕获结果）
    if (!capturing_.load() || hasCaptured_)
        return;
    capturedButton_ = pressed.front();
    hasCaptured_ = true;
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
    while (!stopFlag_.load())
    {
        // 1) 判断物理手柄是否活跃（由 Raw Input 线程异步更新）
        // 如果 1000ms 内没有收到 Raw Input 数据，认为手柄已断开
        const auto nowMs = steadyClockMs();
        const auto lastMs = lastRawInputMs_.load();
        bool connected = (lastMs != 0) && (nowMs - lastMs < 1000);

        // 诊断日志：物理手柄连接状态变化时输出
        static bool lastLoggedConnected = true;
        if (connected != lastLoggedConnected)
        {
            std::cout << "[Gamepad] physical connection changed: " << lastLoggedConnected
                      << " -> " << connected << " (rawInputAgeMs=" << (nowMs - lastMs) << ")" << std::endl;
            lastLoggedConnected = connected;
        }

        physicalConnected_.store(connected);

        // 准备虚拟手柄报告
        XUSB_REPORT_SIM report{};

        if (connected)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            // 更新自瞄/射击/缩放状态
            const bool wasAiming = aimingActive_.load();
            const bool nowAiming = checkButton(aimButton_);
            aimingActive_.store(nowAiming);
            if (nowAiming != wasAiming)
            {
                std::cout << "[Gamepad] aimingActive changed: " << wasAiming << " -> " << nowAiming
                          << " (aimButton=" << aimButton_ << ")" << std::endl;
            }
            shootingActive_.store(checkButton(shootButton_));
            zoomingActive_.store(checkButton(zoomButton_));

            // 透传真实手柄的按键状态到虚拟手柄（左摇杆保留真实输入，右摇杆由自瞄控制）
            report.wButtons = buttons_;
            report.bLeftTrigger = leftTrigger_;
            report.bRightTrigger = rightTrigger_;
            report.sThumbLX = thumbLX_;
            report.sThumbLY = thumbLY_;
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

        // 3) 自瞄时叠加摇杆偏移到右摇杆（RX/RY），FPS 游戏通常用右摇杆控制视角
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

            report.sThumbRX = stickX;
            report.sThumbRY = stickY;

            // 诊断日志：当输出非零摇杆时，每 500ms 输出一次
            if (stickX != 0 || stickY != 0)
            {
                static auto lastReportLog = std::chrono::steady_clock::time_point{};
                auto now = std::chrono::steady_clock::now();
                if (now - lastReportLog > std::chrono::milliseconds(500))
                {
                    std::cout << "[Gamepad] sending RX=" << stickX << " RY=" << stickY
                              << " rawOffset=(" << stickOffsetX_ << ", " << stickOffsetY_ << ")" << std::endl;
                    lastReportLog = now;
                }
            }

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

        // 轮询周期（可配置，决定虚拟手柄回报率 = 1000/interval Hz）
        const int intervalMs = pollIntervalMs_.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs > 0 ? intervalMs : 10));

        // 诊断日志：每 3 秒输出一次心跳，确认轮询线程在运行
        static auto lastHeartbeat = std::chrono::steady_clock::now();
        auto nowHeartbeat = std::chrono::steady_clock::now();
        if (nowHeartbeat - lastHeartbeat > std::chrono::seconds(3))
        {
            // 扫描所有 XInput 索引，帮助确认 ai 读的是哪个手柄
            std::cout << "[Gamepad] XInput scan: ";
            for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
            {
                XINPUT_STATE st{};
                DWORD r = XInputGetState(i, &st);
                std::cout << "[" << i << "]";
                if (r == ERROR_SUCCESS)
                {
                    std::cout << "ok:0x" << std::hex << st.Gamepad.wButtons
                              << " LT=" << static_cast<int>(st.Gamepad.bLeftTrigger)
                              << " RT=" << static_cast<int>(st.Gamepad.bRightTrigger)
                              << std::dec;
                }
                else
                {
                    std::cout << "disconnect(" << r << ")";
                }
                if (i + 1 < XUSER_MAX_COUNT) std::cout << ", ";
            }
            std::cout << " | activeIndex=" << playerIndex_
                      << ", virtual=" << virtualConnected_.load() << std::endl;
            lastHeartbeat = nowHeartbeat;
    }
}

// ============================================================================
// Raw Input 后台读取（绕过 Windows 前台窗口限制）
// ============================================================================

namespace
{
std::uint64_t steadyClockMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

LRESULT CALLBACK GamepadViGEm::rawInputWndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* self = reinterpret_cast<GamepadViGEm*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self)
        return self->rawInputWndProc(hwnd, msg, wParam, lParam);

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT GamepadViGEm::rawInputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INPUT)
    {
        processRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return 0;
    }
    if (msg == WM_INPUT_DEVICE_CHANGE)
    {
        if (wParam == GIDC_ARRIVAL)
            std::cout << "[Gamepad] Raw Input device arrived." << std::endl;
        else if (wParam == GIDC_REMOVAL)
            std::cout << "[Gamepad] Raw Input device removed." << std::endl;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void GamepadViGEm::processRawInput(HRAWINPUT hRawInput)
{
    UINT size = 0;
    UINT headerSize = sizeof(RAWINPUTHEADER);
    if (GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, headerSize) != 0 || size == 0)
        return;

    std::vector<BYTE> buffer(size);
    if (GetRawInputData(hRawInput, RID_INPUT, buffer.data(), &size, headerSize) != size)
        return;

    auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
    if (raw->header.dwType != RIM_TYPEHID)
        return;

    const std::uint8_t* data = raw->hid.bRawData;
    const DWORD reportSize = raw->hid.dwSizeHid;

    if (parseXusbHidReport(data, reportSize))
    {
        lastRawInputMs_.store(steadyClockMs());
        physicalConnected_.store(true);

        // 按键捕获：在 Raw Input 线程中处理，避免与轮询线程竞争
        std::lock_guard<std::mutex> lock(stateMutex_);
        updateCaptureLocked();
    }
}

bool GamepadViGEm::parseXusbHidReport(const std::uint8_t* data, std::size_t size)
{
    if (!data || size < 13)
        return false;

    // Xbox 360 / ViGEm 标准 HID 输入报告（13 字节）：
    // [0] report id
    // [1] buttons low byte
    // [2] buttons high byte
    // [3] left trigger
    // [4] right trigger
    // [5..6]  left thumb X  (int16 little-endian)
    // [7..8]  left thumb Y  (int16 little-endian)
    // [9..10] right thumb X (int16 little-endian)
    // [11..12] right thumb Y (int16 little-endian)
    //
    // 按钮位与 XINPUT_GAMEPAD_* / GamepadButton 命名空间一致。

    std::uint16_t newButtons = data[1] | (static_cast<std::uint16_t>(data[2]) << 8);
    std::uint8_t newLT = data[3];
    std::uint8_t newRT = data[4];

    // 简单合理性检查：至少报告大小符合，且不全是 0xFF（异常填充）
    bool looksValid = (size == 13 || size == 14 || size == 18);
    if (!looksValid)
        return false;

    auto readInt16 = [&](std::size_t offset) -> std::int16_t {
        std::int16_t v = 0;
        std::memcpy(&v, data + offset, sizeof(v));
        return v;
    };

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        buttons_ = newButtons;
        leftTrigger_ = newLT;
        rightTrigger_ = newRT;
        thumbLX_ = readInt16(5);
        thumbLY_ = readInt16(7);
    }

    return true;
}

void GamepadViGEm::rawInputThreadFunc()
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = rawInputWndProcStatic;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SunAimbotRawInputWindow";
    if (!RegisterClassExW(&wc))
    {
        std::cerr << "[Gamepad] Failed to register Raw Input window class." << std::endl;
        return;
    }

    rawInputHwnd_ = CreateWindowExW(
        0, wc.lpszClassName, L"SunAimbotRawInput",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, this);

    if (!rawInputHwnd_)
    {
        std::cerr << "[Gamepad] Failed to create Raw Input message window." << std::endl;
        UnregisterClassW(wc.lpszClassName, hInst);
        return;
    }

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;          // Generic Desktop Controls
    rid.usUsage = 0x05;              // Game Pad
    rid.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid.hwndTarget = rawInputHwnd_;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
    {
        std::cerr << "[Gamepad] Failed to register Raw Input device. Error: " << GetLastError() << std::endl;
        DestroyWindow(rawInputHwnd_);
        rawInputHwnd_ = nullptr;
        UnregisterClassW(wc.lpszClassName, hInst);
        return;
    }

    std::cout << "[Gamepad] Raw Input registered. Listening for gamepad input in background." << std::endl;

    MSG msg{};
    while (!stopFlag_.load())
    {
        // 等待 Raw Input 消息，最多 100ms 超时，便于及时响应 stopFlag
        DWORD wait = MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_RAWINPUT);
        if (wait == WAIT_OBJECT_0)
        {
            while (PeekMessageW(&msg, rawInputHwnd_, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    // 清理
    RegisterRawInputDevices(&rid, 0, sizeof(rid)); // 注销
    DestroyWindow(rawInputHwnd_);
    rawInputHwnd_ = nullptr;
    UnregisterClassW(wc.lpszClassName, hInst);
    std::cout << "[Gamepad] Raw Input thread stopped." << std::endl;
}

