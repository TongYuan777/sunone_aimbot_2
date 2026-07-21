#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <shellapi.h>
#include <algorithm>

#include "imgui/imgui.h"
#include <imgui_internal.h>

#include "sunone_aimbot_2.h"
#include "overlay/config_dirty.h"
#include "overlay/ui_sections.h"
#include "include/other_tools.h"
#include "kmbox_net/picture.h"
#include "GamepadViGEm.h"

std::string ghub_version = get_ghub_version();

int prev_fovX = config.fovX;
int prev_fovY = config.fovY;
float prev_minSpeedMultiplier = config.minSpeedMultiplier;
float prev_maxSpeedMultiplier = config.maxSpeedMultiplier;
float prev_predictionInterval = config.predictionInterval;
bool  prev_kalman_enabled = config.kalman_enabled;
float prev_kalman_process_noise_position = config.kalman_process_noise_position;
float prev_kalman_process_noise_velocity = config.kalman_process_noise_velocity;
float prev_kalman_measurement_noise = config.kalman_measurement_noise;
float prev_kalman_velocity_damping = config.kalman_velocity_damping;
float prev_kalman_max_velocity = config.kalman_max_velocity;
int   prev_kalman_warmup_frames = config.kalman_warmup_frames;
bool  prev_kalman_compensate_detection_delay = config.kalman_compensate_detection_delay;
float prev_kalman_additional_prediction_ms = config.kalman_additional_prediction_ms;
float prev_kalman_reset_timeout_sec = config.kalman_reset_timeout_sec;
float prev_snapRadius = config.snapRadius;
float prev_nearRadius = config.nearRadius;
float prev_speedCurveExponent = config.speedCurveExponent;
float prev_snapBoostFactor = config.snapBoostFactor;

bool  prev_wind_mouse_enabled = config.wind_mouse_enabled;
float prev_wind_G = config.wind_G;
float prev_wind_W = config.wind_W;
float prev_wind_M = config.wind_M;
float prev_wind_D = config.wind_D;

bool prev_auto_shoot = config.auto_shoot;
float prev_bScope_multiplier = config.bScope_multiplier;

namespace
{
enum class MouseSettingsPage
{
    All,
    Movement,
    Prediction,
    Assist,
    Profiles,
    Input
};

bool shouldDrawMousePage(MouseSettingsPage current, MouseSettingsPage wanted)
{
    return current == MouseSettingsPage::All || current == wanted;
}
}

static void draw_mouse_page(MouseSettingsPage page)
{
    if (shouldDrawMousePage(page, MouseSettingsPage::Movement) &&
        OverlayUI::BeginSection("视野", "mouse_section_fov"))
    {
        OverlayUI::SliderIntRow("水平 FOV", &config.fovX, 10, 120, "%d", "##value", "鼠标水平方向自瞄的视野范围（度）");
        OverlayUI::SliderIntRow("垂直 FOV", &config.fovY, 10, 120, "%d", "##value", "鼠标垂直方向自瞄的视野范围（度）");
        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Movement) &&
        OverlayUI::BeginSection("速度倍率", "mouse_section_speed_multiplier"))
    {
        OverlayUI::SliderFloatRow("最小速度倍率", &config.minSpeedMultiplier, 0.1f, 5.0f, "%.1f", "##value", "鼠标移动的最低速度倍率，避免过慢");
        OverlayUI::SliderFloatRow("最大速度倍率", &config.maxSpeedMultiplier, 0.1f, 5.0f, "%.1f", "##value", "鼠标移动的最高速度倍率，限制最大速度");
        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Prediction) &&
        OverlayUI::BeginSection("预测", "mouse_section_prediction"))
    {
        OverlayUI::SliderFloatRow("预测间隔", &config.predictionInterval, 0.00f, 0.5f, "%.2f", "##value", "预测目标未来位置的时间间隔（秒），0 表示禁用预测");
        if (config.predictionInterval == 0.00f)
        {
            OverlayUI::TextRow("预测已禁用。", IM_COL32(255, 108, 108, 255));
        }

        const bool predictionEnabled = (config.predictionInterval > 0.0f);
        if (!predictionEnabled)
        {
            ImGui::BeginDisabled();
        }
        
        if (OverlayUI::SliderIntRow("未来位置数", &config.prediction_futurePositions, 1, 40, "%d", "##value", "预测计算的未来位置点数，越大越平滑但更耗 CPU"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::CheckboxRow("绘制未来位置", &config.draw_futurePositions, "##value", "在屏幕上可视化绘制预测的未来位置轨迹"))
        {
            OverlayConfig_MarkDirty();
        }
        
        if (!predictionEnabled)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("启用预测间隔（> 0）以编辑此区域。");
        }

        ImGui::Separator();
        if (OverlayUI::CheckboxRow("启用卡尔曼滤波", &config.kalman_enabled, "##value", "启用卡尔曼滤波平滑目标位置，减少抖动"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("卡尔曼过程噪声（位置）", &config.kalman_process_noise_position, 0.001f, 5000.0f, "%.3f", "##value", "位置过程噪声，越大滤波越平滑但响应越慢"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("卡尔曼过程噪声（速度）", &config.kalman_process_noise_velocity, 0.001f, 50000.0f, "%.3f", "##value", "速度过程噪声，越大滤波对速度变化越敏感"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("卡尔曼测量噪声", &config.kalman_measurement_noise, 0.001f, 5000.0f, "%.3f", "##value", "测量噪声，越大越信任预测而非检测值"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("卡尔曼速度阻尼", &config.kalman_velocity_damping, 0.0f, 3.0f, "%.3f", "##value", "速度阻尼系数，越大速度衰减越快"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("卡尔曼最大速度", &config.kalman_max_velocity, 100.0f, 60000.0f, "%.0f", "##value", "速度上限，避免异常大的速度导致跳变"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderIntRow("卡尔曼预热帧数", &config.kalman_warmup_frames, 0, 20, "%d", "##value", "滤波器预热所需帧数，期间估计不稳定"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::CheckboxRow("卡尔曼补偿推理延迟", &config.kalman_compensate_detection_delay, "##value", "补偿检测推理引入的延迟，使预测更准确"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("卡尔曼额外预测（毫秒）", &config.kalman_additional_prediction_ms, -80.0f, 120.0f, "%.1f", "##value", "额外向前预测的时间，正值提前，负值延后"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("卡尔曼重置超时（秒）", &config.kalman_reset_timeout_sec, 0.05f, 3.0f, "%.2f", "##value", "目标丢失多久后重置滤波器状态"))
        {
            OverlayConfig_MarkDirty();
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Movement) &&
        OverlayUI::BeginSection("目标修正", "mouse_section_target_correction"))
    {
        OverlayUI::SliderFloatRow("吸附半径", &config.snapRadius, 0.1f, 5.0f, "%.1f", "##value", "目标进入此半径时鼠标加速吸附");
        OverlayUI::SliderFloatRow("近处半径", &config.nearRadius, 1.0f, 40.0f, "%.1f", "##value", "目标进入此半径时降速以避免过冲");
        OverlayUI::SliderFloatRow("速度曲线指数", &config.speedCurveExponent, 0.1f, 10.0f, "%.1f", "##value", "速度曲线指数，影响加减速曲线形状");
        OverlayUI::SliderFloatRow("吸附加速系数", &config.snapBoostFactor, 0.01f, 4.00f, "%.2f", "##value", "吸附时的加速倍率，越大吸附越快");
        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Profiles) &&
        OverlayUI::BeginSection("游戏配置", "mouse_section_game_profile"))
    {
        std::vector<std::string> profile_names;
        for (const auto& kv : config.game_profiles)
            profile_names.push_back(kv.first);
        std::sort(profile_names.begin(), profile_names.end());

        static int selected_index = 0;
        for (size_t i = 0; i < profile_names.size(); ++i)
        {
            if (profile_names[i] == config.active_game)
            {
                selected_index = static_cast<int>(i);
                break;
            }
        }

        std::vector<const char*> profile_items;
        for (const auto& name : profile_names)
            profile_items.push_back(name.c_str());

        if (OverlayUI::ComboRow("当前游戏配置", &selected_index, profile_items.data(), static_cast<int>(profile_items.size()), "##value", "选择当前激活的游戏配置文件"))
        {
            config.active_game = profile_names[selected_index];
            OverlayConfig_MarkDirty();
            globalMouseThread->updateConfig(
                config.detection_resolution,
                config.fovX,
                config.fovY,
                config.minSpeedMultiplier,
                config.maxSpeedMultiplier,
                config.predictionInterval,
                config.auto_shoot,
                config.bScope_multiplier
            );
        }

        const auto& gp = config.currentProfile();

        ImGui::Text("当前配置：%s", gp.name.c_str());
        ImGui::Text("灵敏度：%.4f", gp.sens);
        ImGui::Text("偏航：%.4f", gp.yaw);
        ImGui::Text("俯仰：%.4f", gp.pitch);
        ImGui::Text("FOV 缩放：%s", gp.fovScaled ? "是" : "否");

        if (gp.name != "UNIFIED")
        {
            Config::GameProfile& modifiable = config.game_profiles[gp.name];
            bool changed = false;

            float sens_f = static_cast<float>(modifiable.sens);
            float yaw_f = static_cast<float>(modifiable.yaw);
            float pitch_f = static_cast<float>(modifiable.pitch);
            float baseFOV_f = static_cast<float>(modifiable.baseFOV);

            changed |= OverlayUI::SliderFloatRow("灵敏度", &sens_f, 0.001f, 10.0f, "%.4f", "##value", "游戏内鼠标灵敏度系数");
            changed |= OverlayUI::SliderFloatRow("偏航", &yaw_f, 0.001f, 0.1f, "%.4f", "##value", "鼠标水平移动的偏航系数");
            changed |= OverlayUI::SliderFloatRow("俯仰", &pitch_f, 0.001f, 0.1f, "%.4f", "##value", "鼠标垂直移动的俯仰系数");

            changed |= OverlayUI::CheckboxRow("FOV 缩放", &modifiable.fovScaled, "##value", "启用后按基础 FOV 缩放灵敏度");
            if (modifiable.fovScaled)
            {
                changed |= OverlayUI::SliderFloatRow("基础 FOV", &baseFOV_f, 10.0f, 180.0f, "%.1f", "##value", "作为灵敏度缩放参考的基础 FOV 值");
            }

            if (changed)
            {
                modifiable.sens = static_cast<double>(sens_f);
                modifiable.yaw = static_cast<double>(yaw_f);

                modifiable.pitch = static_cast<double>(pitch_f);

                modifiable.baseFOV = static_cast<double>(baseFOV_f);

                OverlayConfig_MarkDirty();
            }
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Profiles) &&
        OverlayUI::BeginSection("管理配置", "mouse_section_manage_profiles"))
    {
        static char new_profile_name[64] = "";
        bool addProfile = false;
        {
            const auto row = OverlayUI::BeginSettingRow("新配置名称");
            const float buttonW = 96.0f;
            const float inputW = std::max(1.0f, row.controlWidth - buttonW - ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(inputW);
            ImGui::InputText("##value", new_profile_name, sizeof(new_profile_name));
            ImGui::SameLine();
            addProfile = ImGui::Button("添加", ImVec2(buttonW, 0.0f));
            OverlayUI::EndSettingRow(row);
        }
        if (addProfile)
        {
            std::string name = std::string(new_profile_name);
            if (!name.empty() && config.game_profiles.count(name) == 0)
            {
                Config::GameProfile gp;
                gp.name = name;
                gp.sens = 1.0;
                gp.yaw = 0.022;
                gp.pitch = 0.022;
                gp.fovScaled = false;
                gp.baseFOV = 90.0;
                config.game_profiles[name] = gp;
                config.active_game = name;
                OverlayConfig_MarkDirty();
                new_profile_name[0] = '\0'; // clear
            }
        }

        const auto& gp = config.currentProfile();
        if (gp.name != "UNIFIED")
        {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 50, 50, 255));
            if (OverlayUI::ButtonRow("配置", "删除当前配置", "delete_current_profile", "删除当前选中的游戏配置（不可恢复）"))
            {
                config.game_profiles.erase(gp.name);
                if (config.game_profiles.count("UNIFIED") != 0)
                    config.active_game = "UNIFIED";
                else if (!config.game_profiles.empty())
                    config.active_game = config.game_profiles.begin()->first;
                else
                    config.active_game = "UNIFIED";

                OverlayConfig_MarkDirty();
            }
            ImGui::PopStyleColor();
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Assist) &&
        OverlayUI::BeginSection("简易无后座", "mouse_section_easy_no_recoil"))
    {
        if (OverlayUI::CheckboxRow("简易无后座", &config.easynorecoil, "##value", "启用简易无后座力补偿"))
        {
            OverlayConfig_MarkDirty();
        }

        if (!config.easynorecoil)
        {
            ImGui::BeginDisabled();
        }

        if (OverlayUI::SliderFloatRow("无后座强度", &config.easynorecoilstrength, 0.1f, 500.0f, "%.1f", "##value", "无后座力补偿强度，越高效果越明显但越易被检测"))
        {
            OverlayConfig_MarkDirty();
        }

        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "左右方向键：以 10 为步长调整后座强度");

        if (config.easynorecoilstrength >= 100.0f)
        {
            ImGui::TextColored(ImVec4(255, 255, 0, 255), "警告：过高的无后座强度可能被发现。");
        }

        if (!config.easynorecoil)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("启用简易无后座以编辑设置。");
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Assist) &&
        OverlayUI::BeginSection("自动射击", "mouse_section_auto_shoot"))
    {
        OverlayUI::CheckboxRow("自动射击", &config.auto_shoot, "##value", "启用后自动按下射击键");
        if (!config.auto_shoot)
        {
            ImGui::BeginDisabled();
        }

        OverlayUI::SliderFloatRow("bScope 倍率", &config.bScope_multiplier, 0.5f, 2.0f, "%.1f", "##value", "瞄准镜下的射击速度倍率调整");

        if (!config.auto_shoot)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("启用自动射击以编辑设置。");
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Movement) &&
        OverlayUI::BeginSection("风鼠标算法", "mouse_section_wind_mouse"))
    {
        if (OverlayUI::CheckboxRow("启用 WindMouse", &config.wind_mouse_enabled, "##value", "启用 WindMouse 算法模拟人手抖动轨迹"))
        {
            OverlayConfig_MarkDirty();
        }

        if (!config.wind_mouse_enabled)
        {
            ImGui::BeginDisabled();
        }

        if (OverlayUI::SliderFloatRow("重力", &config.wind_G, 4.00f, 40.00f, "%.2f", "##value", "WindMouse 算法的重力参数，影响轨迹向下偏移"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("风力扰动", &config.wind_W, 1.00f, 40.00f, "%.2f", "##value", "WindMouse 算法的风力扰动参数，影响随机抖动幅度"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("最大步长（速度裁剪）", &config.wind_M, 1.00f, 40.00f, "%.2f", "##value", "WindMouse 单步最大移动距离，限制速度上限"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::SliderFloatRow("行为变化距离", &config.wind_D, 1.00f, 40.00f, "%.2f", "##value", "WindMouse 行为变化间隔距离，越大轨迹变化越频繁"))
        {
            OverlayConfig_MarkDirty();
        }

        if (OverlayUI::ButtonRow("WindMouse", "恢复默认", "reset_wind_mouse_defaults", "将 WindMouse 算法参数恢复为默认值"))
        {
            config.wind_G = 18.0f;
            config.wind_W = 15.0f;
            config.wind_M = 10.0f;
            config.wind_D = 8.0f;
            OverlayConfig_MarkDirty();
        }

        if (!config.wind_mouse_enabled)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("启用 WindMouse 以编辑设置。");
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawMousePage(page, MouseSettingsPage::Input) &&
        OverlayUI::BeginSection("输入方式", "mouse_section_input_method"))
    {
        std::vector<std::string> input_methods = { "WIN32", "GHUB", "RAZER", "ARDUINO", "RP2350", "TEENSY41", "TEENSY41_HID", "KMBOX_NET", "KMBOX_A", "MAKCU", "GAMEPAD_VIGEM" };

        std::vector<const char*> method_items;
        method_items.reserve(input_methods.size());
        for (const auto& item : input_methods)
        {
            method_items.push_back(item.c_str());
        }

        int input_method_index = 0;
        for (size_t i = 0; i < input_methods.size(); ++i)
        {
            if (input_methods[i] == config.input_method)
            {
                input_method_index = static_cast<int>(i);
                break;
            }
        }

        if (OverlayUI::ComboRow("鼠标输入方式", &input_method_index, method_items.data(), static_cast<int>(method_items.size()), "##value", "选择鼠标输入方式，不同方式对应不同硬件或驱动"))
        {
            std::string new_input_method = input_methods[input_method_index];

            if (new_input_method != config.input_method)
            {
                config.input_method = new_input_method;
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }
        }

        if (config.input_method == "ARDUINO" || config.input_method == "TEENSY41")
        {
            if (arduinoSerial)
            {
                if (arduinoSerial->isOpen())
                {
                    ImGui::TextColored(ImVec4(0, 255, 0, 255), config.input_method == "TEENSY41" ? "Teensy 4.1 已连接" : "Arduino 已连接");
                }
                else
                {
                    ImGui::TextColored(ImVec4(255, 0, 0, 255), config.input_method == "TEENSY41" ? "Teensy 4.1 未连接" : "Arduino 未连接");
                }
            }

            std::vector<std::string> port_list;
            for (int i = 1; i <= 30; ++i)
            {
                port_list.push_back("COM" + std::to_string(i));
            }

            std::vector<const char*> port_items;
            port_items.reserve(port_list.size());
            for (const auto& port : port_list)
            {
                port_items.push_back(port.c_str());
            }

            int port_index = 0;
            for (size_t i = 0; i < port_list.size(); ++i)
            {
                if (port_list[i] == config.arduino_port)
                {
                    port_index = static_cast<int>(i);
                    break;
                }
            }

            if (OverlayUI::ComboRow(config.input_method == "TEENSY41" ? "Teensy 端口" : "Arduino 端口", &port_index, port_items.data(), static_cast<int>(port_items.size()), "##value", "选择 Arduino 或 Teensy 设备的串口端口"))
            {
                config.arduino_port = port_list[port_index];
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            std::vector<int> baud_rate_list = { 9600, 19200, 38400, 57600, 115200 };
            std::vector<std::string> baud_rate_str_list;
            for (const auto& rate : baud_rate_list)
            {
                baud_rate_str_list.push_back(std::to_string(rate));
            }

            std::vector<const char*> baud_rate_items;
            baud_rate_items.reserve(baud_rate_str_list.size());
            for (const auto& rate_str : baud_rate_str_list)
            {
                baud_rate_items.push_back(rate_str.c_str());
            }

            int baud_rate_index = 0;
            for (size_t i = 0; i < baud_rate_list.size(); ++i)
            {
                if (baud_rate_list[i] == config.arduino_baudrate)
                {
                    baud_rate_index = static_cast<int>(i);
                    break;
                }
            }

            if (OverlayUI::ComboRow(config.input_method == "TEENSY41" ? "Teensy 波特率" : "Arduino 波特率", &baud_rate_index, baud_rate_items.data(), static_cast<int>(baud_rate_items.size()), "##value", "选择 Arduino 或 Teensy 设备的串口波特率"))
            {
                config.arduino_baudrate = baud_rate_list[baud_rate_index];
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            if (config.input_method == "TEENSY41")
            {
                ImGui::TextDisabled("使用 Teensy 4.1 串口鼠标桥接协议。");
            }
            else
            {
                if (OverlayUI::CheckboxRow("Arduino 16 位鼠标", &config.arduino_16_bit_mouse, "##value", "启用后使用 16 位鼠标绝对坐标，否则使用相对坐标"))
                {
                    OverlayConfig_MarkDirty();
                    input_method_changed.store(true);
                }
                if (OverlayUI::CheckboxRow("Arduino 启用按键", &config.arduino_enable_keys, "##value", "启用 Arduino 模拟键盘按键输入"))
                {
                    OverlayConfig_MarkDirty();
                    input_method_changed.store(true);
                }
            }
        }
        else if (config.input_method == "RP2350")
        {
            if (rp2350Serial)
            {
                if (rp2350Serial->isOpen())
                {
                    ImGui::TextColored(ImVec4(0, 255, 0, 255), "RP2350 已连接");
                }
                else
                {
                    ImGui::TextColored(ImVec4(255, 0, 0, 255), "RP2350 未连接");
                }
            }

            std::vector<std::string> port_list;
            for (int i = 1; i <= 30; ++i)
            {
                port_list.push_back("COM" + std::to_string(i));
            }

            std::vector<const char*> port_items;
            port_items.reserve(port_list.size());
            for (const auto& port : port_list)
            {
                port_items.push_back(port.c_str());
            }

            int port_index = 0;
            for (size_t i = 0; i < port_list.size(); ++i)
            {
                if (port_list[i] == config.rp2350_port)
                {
                    port_index = static_cast<int>(i);
                    break;
                }
            }

            if (OverlayUI::ComboRow("RP2350 端口", &port_index, port_items.data(), static_cast<int>(port_items.size()), "##value", "选择 RP2350 设备的串口端口"))
            {
                config.rp2350_port = port_list[port_index];
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            std::vector<int> baud_rate_list = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
            std::vector<std::string> baud_rate_str_list;
            for (const auto& rate : baud_rate_list)
            {
                baud_rate_str_list.push_back(std::to_string(rate));
            }

            std::vector<const char*> baud_rate_items;
            baud_rate_items.reserve(baud_rate_str_list.size());
            for (const auto& rate_str : baud_rate_str_list)
            {
                baud_rate_items.push_back(rate_str.c_str());
            }

            int baud_rate_index = 0;
            for (size_t i = 0; i < baud_rate_list.size(); ++i)
            {
                if (baud_rate_list[i] == config.rp2350_baudrate)
                {
                    baud_rate_index = static_cast<int>(i);
                    break;
                }
            }

            if (OverlayUI::ComboRow("RP2350 波特率", &baud_rate_index, baud_rate_items.data(), static_cast<int>(baud_rate_items.size()), "##value", "选择 RP2350 设备的串口波特率"))
            {
                config.rp2350_baudrate = baud_rate_list[baud_rate_index];
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            if (OverlayUI::CheckboxRow("RP2350 16 位鼠标", &config.rp2350_16_bit_mouse, "##value", "启用后使用 16 位鼠标绝对坐标，否则使用相对坐标"))
            {
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }
            if (OverlayUI::CheckboxRow("RP2350 启用按键", &config.rp2350_enable_keys, "##value", "启用 RP2350 模拟键盘按键输入"))
            {
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }
        }
        else if (config.input_method == "GHUB")
        {
            if (ghub_version == "13.1.4")
            {
                std::string ghub_version_label = "已安装正确版本的 GHub：" + ghub_version;
                ImGui::Text(ghub_version_label.c_str());
            }
            else
            {
                ImGui::Text("已安装的 GHub 版本错误，或未设置默认 GHub 路径。\n默认系统路径：C:\\Program Files\\LGHUB");
                if (OverlayUI::ButtonRow("GHub", "打开 GHub 文档", "ghub_docs", "打开 GHub 输入方式的官方文档"))
                {
                    ShellExecute(0, 0, L"https://github.com/SunOner/sunone_aimbot_2/blob/main/docs/guides.md#g-hub-input-method", 0, 0, SW_SHOW);
                }
            }

            ImGui::TextColored(ImVec4(255, 0, 0, 255), "自行承担风险，部分游戏会检测此输入方式。");
        }
        else if (config.input_method == "TEENSY41_HID")
        {
            bool teensy41Connected = false;
            {
                std::lock_guard<std::mutex> lock(inputDevicesMutex);
                teensy41Connected = activeMouseInputOwner && activeMouseInputOwner->isOpen();
            }

            if (teensy41Connected)
            {
                ImGui::TextColored(ImVec4(0, 255, 0, 255), "Teensy 4.1 RawHID 已连接");
            }
            else
            {
                ImGui::TextColored(ImVec4(255, 0, 0, 255), "Teensy 4.1 RawHID 未连接");
            }

            static char serial[64] = "";
            static char vid[16] = "";
            static char pid[16] = "";
            static std::string last_serial;
            static std::string last_vid;
            static std::string last_pid;
            static int usage_page = 0;
            static int usage_id = 0;
            static int open_index = 0;
            static int timeout_ms = 0;
            static int reconnect_ms = 0;

            if (last_serial != config.teensy_hid_serial ||
                last_vid != config.teensy_hid_vid_filter ||
                last_pid != config.teensy_hid_pid_filter ||
                usage_page != config.teensy_hid_usage_page ||
                usage_id != config.teensy_hid_usage_id ||
                open_index != config.teensy_hid_open_index ||
                timeout_ms != config.teensy_hid_packet_timeout_ms ||
                reconnect_ms != config.teensy_hid_reconnect_interval_ms)
            {
                strncpy(serial, config.teensy_hid_serial.c_str(), sizeof(serial));
                strncpy(vid, config.teensy_hid_vid_filter.c_str(), sizeof(vid));
                strncpy(pid, config.teensy_hid_pid_filter.c_str(), sizeof(pid));
                serial[sizeof(serial) - 1] = '\0';
                vid[sizeof(vid) - 1] = '\0';
                pid[sizeof(pid) - 1] = '\0';
                last_serial = config.teensy_hid_serial;
                last_vid = config.teensy_hid_vid_filter;
                last_pid = config.teensy_hid_pid_filter;
                usage_page = config.teensy_hid_usage_page;
                usage_id = config.teensy_hid_usage_id;
                open_index = config.teensy_hid_open_index;
                timeout_ms = config.teensy_hid_packet_timeout_ms;
                reconnect_ms = config.teensy_hid_reconnect_interval_ms;
            }

            OverlayUI::InputTextRow("序列号", serial, sizeof(serial), 0, "##value", "Teensy RawHID 设备的序列号筛选（留空表示不筛选）");
            OverlayUI::InputTextRow("VID 筛选", vid, sizeof(vid), 0, "##value", "Teensy RawHID 设备的 Vendor ID（十六进制，留空表示不筛选）");
            OverlayUI::InputTextRow("PID 筛选", pid, sizeof(pid), 0, "##value", "Teensy RawHID 设备的 Product ID（十六进制，留空表示不筛选）");
            OverlayUI::InputIntRow("Usage Page", &usage_page, 1, 100, 0, "##value", "HID Usage Page，用于筛选 RawHID 设备（0 表示不筛选）");
            OverlayUI::InputIntRow("Usage ID", &usage_id, 1, 100, 0, "##value", "HID Usage ID，用于筛选 RawHID 设备（0 表示不筛选）");
            OverlayUI::InputIntRow("打开索引", &open_index, 1, 100, 0, "##value", "当存在多个匹配设备时，打开第几个（从 0 开始）");
            OverlayUI::InputIntRow("数据包超时（ms）", &timeout_ms, 1, 100, 0, "##value", "HID 数据包读取超时时间，单位毫秒");
            OverlayUI::InputIntRow("重连间隔（ms）", &reconnect_ms, 1, 100, 0, "##value", "设备断开后尝试重连的间隔时间，单位毫秒");

            if (OverlayUI::ButtonRow("Teensy HID", "保存并重连", "teensy_hid_save_reconnect", "保存 HID 参数并重新连接设备"))
            {
                config.teensy_hid_serial = serial;
                config.teensy_hid_vid_filter = vid;
                config.teensy_hid_pid_filter = pid;
                config.teensy_hid_usage_page = usage_page;
                config.teensy_hid_usage_id = usage_id;
                config.teensy_hid_open_index = open_index;
                config.teensy_hid_packet_timeout_ms = timeout_ms;
                config.teensy_hid_reconnect_interval_ms = reconnect_ms;
                last_serial = config.teensy_hid_serial;
                last_vid = config.teensy_hid_vid_filter;
                last_pid = config.teensy_hid_pid_filter;
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }
        }
        else if (config.input_method == "RAZER")
        {
            if (razerControl && razerControl->isOpen())
            {
                ImGui::TextColored(ImVec4(0, 255, 0, 255), "Razer rzctl 已连接");
            }
            else
            {
                ImGui::TextColored(ImVec4(255, 0, 0, 255), "Razer rzctl 未连接");
            }
            ImGui::Text("需要 rzctl.dll 放在 ai.exe 同目录下。");
            ImGui::TextColored(ImVec4(255, 0, 0, 255), "自行承担风险，部分游戏会检测此输入方式。");
        }
        else if (config.input_method == "WIN32")
        {
            ImGui::TextColored(ImVec4(255, 255, 255, 255), "这是标准的鼠标输入方式，大多数游戏中可能无效。请使用 GHUB、RAZER、ARDUINO、RP2350、TEENSY41 或 TEENSY41_HID。");
            ImGui::TextColored(ImVec4(255, 0, 0, 255), "自行承担风险，部分游戏会检测此输入方式。");
        }
        else if (config.input_method == "KMBOX_NET")
        {
            static char ip[32] = "";
            static char port[8] = "";
            static char uuid[16] = "";
            static std::string last_ip;
            static std::string last_port;
            static std::string last_uuid;

            if (last_ip != config.kmbox_net_ip || last_port != config.kmbox_net_port || last_uuid != config.kmbox_net_uuid)
            {
                strncpy(ip, config.kmbox_net_ip.c_str(), sizeof(ip));
                strncpy(port, config.kmbox_net_port.c_str(), sizeof(port));
                strncpy(uuid, config.kmbox_net_uuid.c_str(), sizeof(uuid));
                ip[sizeof(ip) - 1] = '\0';
                port[sizeof(port) - 1] = '\0';
                uuid[sizeof(uuid) - 1] = '\0';
                last_ip = config.kmbox_net_ip;
                last_port = config.kmbox_net_port;
                last_uuid = config.kmbox_net_uuid;
            }

            OverlayUI::InputTextRow("IP 地址", ip, sizeof(ip), 0, "##value", "kmbox 网络版的 IP 地址");
            OverlayUI::InputTextRow("端口", port, sizeof(port), 0, "##value", "kmbox 网络版的连接端口");
            OverlayUI::InputTextRow("UUID", uuid, sizeof(uuid), 0, "##value", "kmbox 网络版的设备 UUID 鉴权字符串");

            if (OverlayUI::ButtonRow("kmboxNet", "保存并重连", "kmbox_net_save_reconnect", "保存网络参数并重新连接 kmbox 设备"))
            {
                config.kmbox_net_ip = ip;
                config.kmbox_net_port = port;
                config.kmbox_net_uuid = uuid;
                last_ip = config.kmbox_net_ip;
                last_port = config.kmbox_net_port;
                last_uuid = config.kmbox_net_uuid;
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            bool kmboxNetConnected = false;
            {
                std::lock_guard<std::mutex> lock(inputDevicesMutex);
                KmboxNetConnection* device =
                    activeMouseInputOwner && std::string(activeMouseInputOwner->name()) == "KMBOX_NET"
                    ? activeMouseInputOwner->kmboxNet()
                    : nullptr;
                kmboxNetConnected = device && device->isOpen();
            }

            if (kmboxNetConnected)
            {
                ImGui::TextColored(ImVec4(0, 255, 0, 255), "kmboxNet 已连接");
            }
            else
            {
                ImGui::TextColored(ImVec4(255, 0, 0, 255), "kmboxNet 未连接");
            }

            if (!kmboxNetConnected)
                ImGui::BeginDisabled();

            if (OverlayUI::ButtonRow("kmboxNet 设备", "重启设备", "kmbox_net_reboot", "向 kmbox 设备发送重启命令"))
            {
                std::lock_guard<std::mutex> lock(inputDevicesMutex);
                KmboxNetConnection* device =
                    activeMouseInputOwner && std::string(activeMouseInputOwner->name()) == "KMBOX_NET"
                    ? activeMouseInputOwner->kmboxNet()
                    : nullptr;
                if (device && device->isOpen())
                    device->reboot();
            }

            if (OverlayUI::ButtonRow("kmboxNet 图像", "更换图像", "kmbox_net_image", "向 kmbox 设备 LCD 推送预设图像"))
            {
                std::lock_guard<std::mutex> lock(inputDevicesMutex);
                KmboxNetConnection* device =
                    activeMouseInputOwner && std::string(activeMouseInputOwner->name()) == "KMBOX_NET"
                    ? activeMouseInputOwner->kmboxNet()
                    : nullptr;
                if (device && device->isOpen())
                {
                    device->lcdColor(0);
                    device->lcdPicture(gImage_128x160);
                }
            }

            if (!kmboxNetConnected)
                ImGui::EndDisabled();
        }
        else if (config.input_method == "KMBOX_A")
        {
            static char pidvid[32] = "";
            static std::string last_pidvid;

            if (last_pidvid != config.kmbox_a_pidvid)
            {
                strncpy(pidvid, config.kmbox_a_pidvid.c_str(), sizeof(pidvid));
                pidvid[sizeof(pidvid) - 1] = '\0';
                last_pidvid = config.kmbox_a_pidvid;
            }

            OverlayUI::InputTextRow("PIDVID", pidvid, sizeof(pidvid), 0, "##value", "kmbox A 的 PID 和 VID（格式 PPPPVVVV，十六进制）");
            ImGui::TextDisabled("格式：PPPPVVVV（单个字段）");

            if (OverlayUI::ButtonRow("kmboxA", "保存并重连", "kmbox_a_save_reconnect", "保存 PIDVID 并重新连接 kmbox A 设备"))
            {
                config.kmbox_a_pidvid = pidvid;
                last_pidvid = config.kmbox_a_pidvid;
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            if (kmboxASerial && kmboxASerial->isOpen())
            {
                ImGui::TextColored(ImVec4(0, 255, 0, 255), "kmboxA 已连接");
            }
            else
            {
                ImGui::TextColored(ImVec4(255, 0, 0, 255), "kmboxA 未连接");
            }
        }
        else if (config.input_method == "MAKCU")
        {
            std::vector<std::string> port_list;
            for (int i = 1; i <= 30; ++i)
            {
                port_list.push_back("COM" + std::to_string(i));
            }

            std::vector<const char*> port_items;
            port_items.reserve(port_list.size());
            for (const auto& port : port_list)
            {
                port_items.push_back(port.c_str());
            }

            int port_index = 0;
            for (size_t i = 0; i < port_list.size(); ++i)
            {
                if (port_list[i] == config.makcu_port)
                {
                    port_index = static_cast<int>(i);
                    break;
                }
            }

            if (OverlayUI::ComboRow("Makcu 端口", &port_index, port_items.data(), static_cast<int>(port_items.size()), "##value", "选择 Makcu 设备的串口端口"))
            {
                config.makcu_port = port_list[port_index];
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            std::vector<int> baud_list = { 9600, 19200, 38400, 57600, 115200 };
            std::vector<std::string> baud_str_list;
            for (int b : baud_list) baud_str_list.push_back(std::to_string(b));

            std::vector<const char*> baud_items;
            baud_items.reserve(baud_list.size());
            for (const auto& baud : baud_str_list)
            {
                baud_items.push_back(baud.c_str());
            }

            int baud_index = 0;
            for (size_t i = 0; i < baud_list.size(); ++i)
            {
                if (baud_list[i] == config.makcu_baudrate)
                {
                    baud_index = static_cast<int>(i);
                    break;
                }
            }

            if (OverlayUI::ComboRow("Makcu 波特率", &baud_index, baud_items.data(), static_cast<int>(baud_items.size()), "##value", "选择 Makcu 设备的串口波特率"))
            {
                config.makcu_baudrate = baud_list[baud_index];
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            if (makcuSerial && makcuSerial->isOpen())
            {
                ImGui::TextColored(ImVec4(0, 255, 0, 255), "Makcu 已连接");
            }
            else
            {
                ImGui::TextColored(ImVec4(255, 0, 0, 255), "Makcu 未连接");
            }
        }
        else if (config.input_method == "GAMEPAD_VIGEM")
        {
            // 获取当前活动的手柄实例（可能为空，例如配置已切换但设备尚未重建）
            GamepadViGEm* gamepad = nullptr;
            {
                std::lock_guard<std::mutex> lock(inputDevicesMutex);
                gamepad = activeMouseInputOwner ? activeMouseInputOwner->gamepad() : nullptr;
            }

            // -------- 已连接手柄扫描（每 ~500ms 刷新一次） --------
            // 用静态缓存避免每帧调用 XInputGetState（4 次系统调用）
            static std::vector<int> s_connectedIndices;
            static float s_scanAccumulator = 0.0f;
            s_scanAccumulator += ImGui::GetIO().DeltaTime;
            if (s_connectedIndices.empty() || s_scanAccumulator >= 0.5f)
            {
                s_scanAccumulator = 0.0f;
                s_connectedIndices = GamepadViGEm::getConnectedGamepadIndices();
            }

            // 玩家索引下拉框：选项标注连接状态（如 "0 (已连接)" / "1 (未连接)"）
            std::vector<std::string> player_labels;
            player_labels.reserve(4);
            for (int i = 0; i < 4; ++i)
            {
                bool isConnected = false;
                for (int idx : s_connectedIndices)
                {
                    if (idx == i) { isConnected = true; break; }
                }
                player_labels.push_back(std::to_string(i) + (isConnected ? " (已连接)" : " (未连接)"));
            }
            std::vector<const char*> player_items;
            player_items.reserve(player_labels.size());
            for (const auto& lbl : player_labels) player_items.push_back(lbl.c_str());

            int player_idx = config.gamepad_player_index;
            if (player_idx < 0 || player_idx > 3) player_idx = 0;
            if (OverlayUI::ComboRow("玩家索引", &player_idx, player_items.data(), static_cast<int>(player_items.size()), "##value", "选择要使用的 XInput 手柄索引（0-3）"))
            {
                config.gamepad_player_index = player_idx;
                OverlayConfig_MarkDirty();
                input_method_changed.store(true);
            }

            // 当前已连接手柄数量提示
            if (s_connectedIndices.empty())
            {
                ImGui::TextColored(ImVec4(255, 108, 108, 255), "未检测到任何已连接的 XInput 手柄");
            }
            else
            {
                std::string idxStr = "已连接手柄索引：";
                for (size_t i = 0; i < s_connectedIndices.size(); ++i)
                {
                    if (i) idxStr += ", ";
                    idxStr += std::to_string(s_connectedIndices[i]);
                }
                ImGui::TextColored(ImVec4(108, 255, 108, 255), "%s", idxStr.c_str());
            }

            // 摇杆灵敏度
            float stick_scale = config.gamepad_stick_scale;
            if (OverlayUI::SliderFloatRow("摇杆灵敏度", &stick_scale, 1.0f, 1000.0f, "%.3f", "##value", "摇杆输入到鼠标移动的灵敏度倍率"))
            {
                config.gamepad_stick_scale = stick_scale;
                OverlayConfig_MarkDirty();
            }

            // 死区
            int deadzone = config.gamepad_deadzone;
            if (OverlayUI::SliderIntRow("摇杆死区", &deadzone, 0, 10000, "%d", "##value", "摇杆输入小于此值时视为零，避免漂移"))
            {
                config.gamepad_deadzone = deadzone;
                OverlayConfig_MarkDirty();
            }

            // -------- 实时按键显示 --------
            if (gamepad && gamepad->isPhysicalConnected())
            {
                auto pressed = gamepad->getCurrentlyPressedButtons();
                std::string pressedStr;
                for (size_t i = 0; i < pressed.size(); ++i)
                {
                    if (i) pressedStr += ", ";
                    pressedStr += pressed[i];
                }
                if (pressedStr.empty())
                    ImGui::TextDisabled("当前按下：（无）");
                else
                    ImGui::TextColored(ImVec4(108, 255, 108, 255), "当前按下：%s", pressedStr.c_str());
            }
            else
            {
                ImGui::TextDisabled("当前按下：（手柄未连接）");
            }

            // -------- 按键映射 + 捕获按钮 --------
            // 静态变量记录当前在捕获哪个字段（"aim"/"shoot"/"zoom" 或空）
            // 避免 pollCapturedButton() 被多个字段调用导致结果被偷走
            static std::string s_capturingField;

            // 清理残留捕获标记：gamepad 失效，或 gamepad 存在但已不在捕获状态
            // （例如切换输入方式后 gamepan 实例重建，isCapturing() 会归零）
            if (!s_capturingField.empty())
            {
                if (!gamepad || !gamepad->isCapturing())
                    s_capturingField.clear();
            }

            auto drawButtonComboWithCapture = [&](const char* label, std::string& configField, const char* fieldId)
            {
                const auto row = OverlayUI::BeginSettingRow(label);

                auto buttons = GamepadButtonName::All();
                std::vector<const char*> btn_items;
                btn_items.reserve(buttons.size());
                for (const auto& b : buttons) btn_items.push_back(b.c_str());
                int idx = 0;
                for (size_t i = 0; i < buttons.size(); ++i)
                {
                    if (buttons[i] == configField) { idx = static_cast<int>(i); break; }
                }

                const ImGuiStyle& style = ImGui::GetStyle();
                const float buttonW = 96.0f;
                const float comboW = std::max(40.0f, row.controlWidth - buttonW - style.ItemSpacing.x);

                ImGui::SetNextItemWidth(comboW);
                ImGui::Combo("##combo", &idx, btn_items.data(), static_cast<int>(btn_items.size()));
                ImGui::SameLine();

                const bool canCapture = gamepad && gamepad->isPhysicalConnected();
                const bool thisCapturing = canCapture && gamepad->isCapturing() && s_capturingField == fieldId;
                const bool otherCapturing = !s_capturingField.empty() && s_capturingField != fieldId;

                // 其他字段正在捕获时，禁用本字段的捕获按钮
                if (!canCapture || otherCapturing)
                    ImGui::BeginDisabled();

                const char* btnLabel = thisCapturing ? "停止##capture" : "捕获##capture";
                if (ImGui::Button(btnLabel, ImVec2(buttonW, 0.0f)))
                {
                    if (gamepad)
                    {
                        if (thisCapturing)
                        {
                            gamepad->cancelCapture();
                            s_capturingField.clear();
                        }
                        else
                        {
                            gamepad->beginCapture();
                            s_capturingField = fieldId;
                        }
                    }
                }

                if (!canCapture || otherCapturing)
                    ImGui::EndDisabled();

                OverlayUI::EndSettingRow(row);

                // 应用下拉框选择
                if (buttons[idx] != configField)
                {
                    configField = buttons[idx];
                    OverlayConfig_MarkDirty();
                    input_method_changed.store(true);
                }

                // 只有当前捕获的字段才取走结果
                if (gamepad && thisCapturing)
                {
                    std::string captured = gamepad->pollCapturedButton();
                    if (!captured.empty())
                    {
                        if (captured != configField)
                        {
                            configField = captured;
                            OverlayConfig_MarkDirty();
                            input_method_changed.store(true);
                        }
                        s_capturingField.clear();
                    }
                }
            };

            drawButtonComboWithCapture("自瞄按键", config.gamepad_aim_button, "aim");
            drawButtonComboWithCapture("射击按键", config.gamepad_shoot_button, "shoot");
            drawButtonComboWithCapture("缩放按键", config.gamepad_zoom_button, "zoom");

            // -------- 连接状态 --------
            if (gamepad)
            {
                if (gamepad->isVirtualConnected())
                    ImGui::TextColored(ImVec4(0, 255, 0, 255), "虚拟手柄：已连接");
                else
                    ImGui::TextColored(ImVec4(255, 0, 0, 255), "虚拟手柄：未连接");

                if (gamepad->isPhysicalConnected())
                    ImGui::TextColored(ImVec4(0, 255, 0, 255), "真实手柄：已连接");
                else
                    ImGui::TextColored(ImVec4(255, 0, 0, 255), "真实手柄：未连接");
            }
            else
            {
                ImGui::TextColored(ImVec4(255, 0, 0, 255), "手柄未初始化");
            }

            ImGui::TextWrapped("提示：需要安装 ViGEmBus 驱动，并将 ViGEmClient.dll 放入程序目录或 PATH。");
            ImGui::TextWrapped("点击 \"捕获\" 后按下任意手柄按键即可自动填入对应字段。");
        }

        OverlayUI::EndSection();
    }

    if (prev_fovX != config.fovX ||
        prev_fovY != config.fovY ||
        prev_minSpeedMultiplier != config.minSpeedMultiplier ||
        prev_maxSpeedMultiplier != config.maxSpeedMultiplier ||
        prev_predictionInterval != config.predictionInterval ||
        prev_kalman_enabled != config.kalman_enabled ||
        prev_kalman_process_noise_position != config.kalman_process_noise_position ||
        prev_kalman_process_noise_velocity != config.kalman_process_noise_velocity ||
        prev_kalman_measurement_noise != config.kalman_measurement_noise ||
        prev_kalman_velocity_damping != config.kalman_velocity_damping ||
        prev_kalman_max_velocity != config.kalman_max_velocity ||
        prev_kalman_warmup_frames != config.kalman_warmup_frames ||
        prev_kalman_compensate_detection_delay != config.kalman_compensate_detection_delay ||
        prev_kalman_additional_prediction_ms != config.kalman_additional_prediction_ms ||
        prev_kalman_reset_timeout_sec != config.kalman_reset_timeout_sec ||
        prev_snapRadius != config.snapRadius ||
        prev_nearRadius != config.nearRadius ||
        prev_speedCurveExponent != config.speedCurveExponent ||
        prev_snapBoostFactor != config.snapBoostFactor)
    {
        prev_fovX = config.fovX;
        prev_fovY = config.fovY;
        prev_minSpeedMultiplier = config.minSpeedMultiplier;
        prev_maxSpeedMultiplier = config.maxSpeedMultiplier;
        prev_predictionInterval = config.predictionInterval;
        prev_kalman_enabled = config.kalman_enabled;
        prev_kalman_process_noise_position = config.kalman_process_noise_position;
        prev_kalman_process_noise_velocity = config.kalman_process_noise_velocity;
        prev_kalman_measurement_noise = config.kalman_measurement_noise;
        prev_kalman_velocity_damping = config.kalman_velocity_damping;
        prev_kalman_max_velocity = config.kalman_max_velocity;
        prev_kalman_warmup_frames = config.kalman_warmup_frames;
        prev_kalman_compensate_detection_delay = config.kalman_compensate_detection_delay;
        prev_kalman_additional_prediction_ms = config.kalman_additional_prediction_ms;
        prev_kalman_reset_timeout_sec = config.kalman_reset_timeout_sec;
        prev_snapRadius = config.snapRadius;
        prev_nearRadius = config.nearRadius;
        prev_speedCurveExponent = config.speedCurveExponent;
        prev_snapBoostFactor = config.snapBoostFactor;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.bScope_multiplier);

        OverlayConfig_MarkDirty();
    }

    if (prev_wind_mouse_enabled != config.wind_mouse_enabled ||
        prev_wind_G != config.wind_G ||
        prev_wind_W != config.wind_W ||
        prev_wind_M != config.wind_M ||
        prev_wind_D != config.wind_D)
    {
        prev_wind_mouse_enabled = config.wind_mouse_enabled;
        prev_wind_G = config.wind_G;
        prev_wind_W = config.wind_W;
        prev_wind_M = config.wind_M;
        prev_wind_D = config.wind_D;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.bScope_multiplier);

        OverlayConfig_MarkDirty();
    }

    if (prev_auto_shoot != config.auto_shoot ||
        prev_bScope_multiplier != config.bScope_multiplier)
    {
        prev_auto_shoot = config.auto_shoot;
        prev_bScope_multiplier = config.bScope_multiplier;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.bScope_multiplier);

        OverlayConfig_MarkDirty();
    }
}

void draw_mouse()
{
    draw_mouse_page(MouseSettingsPage::All);
}

void draw_mouse_movement()
{
    draw_mouse_page(MouseSettingsPage::Movement);
}

void draw_mouse_prediction()
{
    draw_mouse_page(MouseSettingsPage::Prediction);
}

void draw_mouse_assist()
{
    draw_mouse_page(MouseSettingsPage::Assist);
}

void draw_mouse_profiles()
{
    draw_mouse_page(MouseSettingsPage::Profiles);
}

void draw_mouse_input()
{
    draw_mouse_page(MouseSettingsPage::Input);
}
