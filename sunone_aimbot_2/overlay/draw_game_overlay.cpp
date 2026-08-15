#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <commdlg.h>
#include <algorithm>
#include <string>
#include <cstring>

#include "imgui/imgui.h"
#include "config.h"
#include "sunone_aimbot_2.h"
#include "overlay/config_dirty.h"
#include "overlay/ui_sections.h"

extern std::string g_iconLastError;

namespace
{
enum class GameOverlaySettingsPage
{
    All,
    General,
    Visuals,
    Icon
};

bool shouldDrawGameOverlayPage(GameOverlaySettingsPage current, GameOverlaySettingsPage wanted)
{
    return current == GameOverlaySettingsPage::All || current == wanted;
}
}

static void draw_game_overlay_page(GameOverlaySettingsPage page)
{
    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::General) &&
        OverlayUI::BeginSection("常规", "game_overlay_section_general"))
    {
        if (OverlayUI::CheckboxRow("启用", &config.game_overlay_enabled, "##value", "启用或关闭整个游戏叠加层"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderIntRow("叠加层最大 FPS（0 = 无上限）", &config.game_overlay_max_fps, 0, 256, "%d", "##value", "限制叠加层渲染帧率以降低开销，0 表示不限制"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("绘制检测框", &config.game_overlay_draw_boxes, "##value", "在检测到的目标周围绘制方框"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("补偿叠加层延迟", &config.game_overlay_compensate_latency, "##value", "根据采集到渲染的延迟向前推算目标位置"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("绘制未来位置", &config.game_overlay_draw_future, "##value", "绘制预测的未来目标位置点序列"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("绘制 Wind 调试尾迹", &config.game_overlay_draw_wind_tail, "##value", "绘制 Wind 模型的调试尾迹用于分析"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("显示目标修正", &config.game_overlay_show_target_correction, "##value", "显示目标位置修正前后的差值"))
            OverlayConfig_MarkDirty();

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Visuals) &&
        OverlayUI::BeginSection("方框颜色", "game_overlay_section_box_color"))
    {
        bool colorChanged = false;

        colorChanged |= OverlayUI::SliderIntRow("不透明度", &config.game_overlay_box_a, 0, 255, "%d", "##value", "检测框的 alpha 通道，0 完全透明，255 完全不透明");
        colorChanged |= OverlayUI::SliderIntRow("红", &config.game_overlay_box_r, 0, 255, "%d", "##value", "检测框颜色的红色通道（RGB）");
        colorChanged |= OverlayUI::SliderIntRow("绿", &config.game_overlay_box_g, 0, 255, "%d", "##value", "检测框颜色的绿色通道（RGB）");
        colorChanged |= OverlayUI::SliderIntRow("蓝", &config.game_overlay_box_b, 0, 255, "%d", "##value", "检测框颜色的蓝色通道（RGB）");

        if (OverlayUI::SliderFloatRow("方框粗细", &config.game_overlay_box_thickness, 0.5f, 10.0f, "%.1f", "##value", "检测框边线的像素粗细"))
            OverlayConfig_MarkDirty();

        if (colorChanged)
        {
            config.clampGameOverlayColor();
            OverlayConfig_MarkDirty();
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Visuals) &&
        OverlayUI::BeginSection("采集框", "game_overlay_section_capture_frame"))
    {
        if (OverlayUI::CheckboxRow("绘制采集框", &config.game_overlay_draw_frame, "##value", "在画面上绘制采集区域边框"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::CheckboxRow("绘制圆形参考", &config.game_overlay_draw_circle_fov, "##value", "绘制 FOV 圆形参考线用于辅助瞄准"))
            OverlayConfig_MarkDirty();

        bool frameColorChanged = false;

        frameColorChanged |= OverlayUI::SliderIntRow("不透明度", &config.game_overlay_frame_a, 0, 255, "%d", "##value", "采集框的 alpha 通道，0 完全透明，255 完全不透明");
        frameColorChanged |= OverlayUI::SliderIntRow("红", &config.game_overlay_frame_r, 0, 255, "%d", "##value", "采集框颜色的红色通道（RGB）");
        frameColorChanged |= OverlayUI::SliderIntRow("绿", &config.game_overlay_frame_g, 0, 255, "%d", "##value", "采集框颜色的绿色通道（RGB）");
        frameColorChanged |= OverlayUI::SliderIntRow("蓝", &config.game_overlay_frame_b, 0, 255, "%d", "##value", "采集框颜色的蓝色通道（RGB）");

        if (OverlayUI::SliderFloatRow("框线粗细", &config.game_overlay_frame_thickness, 0.5f, 10.0f, "%.1f", "##value", "采集框边线的像素粗细"))
            OverlayConfig_MarkDirty();

        if (frameColorChanged)
        {
            config.clampGameOverlayColor();
            OverlayConfig_MarkDirty();
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Visuals) &&
        OverlayUI::BeginSection("未来点样式", "game_overlay_section_future_style"))
    {
        if (OverlayUI::SliderFloatRow("点半径", &config.game_overlay_future_point_radius, 1.0f, 20.0f, "%.1f", "##value", "未来位置预测点的像素半径"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderFloatRow("点步进透明度衰减", &config.game_overlay_future_alpha_falloff, 0.1f, 5.0f, "%.2f", "##value", "未来点序列沿步进方向透明度衰减速率，越大衰减越快"))
            OverlayConfig_MarkDirty();

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Icon) &&
        OverlayUI::BeginSection("图标叠加", "game_overlay_section_icon"))
    {
        if (OverlayUI::CheckboxRow("启用图标叠加", &config.game_overlay_icon_enabled, "##value", "在目标上叠加自定义图标"))
            OverlayConfig_MarkDirty();

        if (!config.game_overlay_icon_enabled)
        {
            ImGui::BeginDisabled();
        }

        static bool pathInit = false;
        static char iconPathBuf[512];

        if (!pathInit)
        {
            pathInit = true;
            memset(iconPathBuf, 0, sizeof(iconPathBuf));
            std::string p = config.game_overlay_icon_path;
            if (p.size() >= sizeof(iconPathBuf)) p = p.substr(0, sizeof(iconPathBuf) - 1);
            memcpy(iconPathBuf, p.c_str(), p.size());
        }

        {
            const auto row = OverlayUI::BeginSettingRow("图标路径");
            const float browseW = 76.0f;
            const float inputW = std::max(1.0f, row.controlWidth - browseW - ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(inputW);
            if (ImGui::InputText("##value", iconPathBuf, IM_ARRAYSIZE(iconPathBuf)))
            {
                config.game_overlay_icon_path = iconPathBuf;
                OverlayConfig_MarkDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("浏览", ImVec2(browseW, 0.0f)))
            {
                char filePath[MAX_PATH] = {};
                OPENFILENAMEA ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = nullptr;
                ofn.lpstrFile = filePath;
                ofn.nMaxFile = sizeof(filePath);
                ofn.lpstrFilter = "图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.ico\0所有文件\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

                if (GetOpenFileNameA(&ofn))
                {
                    strncpy_s(iconPathBuf, filePath, sizeof(iconPathBuf) - 1);
                    config.game_overlay_icon_path = iconPathBuf;
                    OverlayConfig_MarkDirty();
                }
            }
            OverlayUI::EndSettingRow(row);
        }

        if (OverlayUI::SliderIntRow("图标宽度", &config.game_overlay_icon_width, 4, 512, "%d", "##value", "图标渲染的像素宽度"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderIntRow("图标高度", &config.game_overlay_icon_height, 4, 512, "%d", "##value", "图标渲染的像素高度"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderFloatRow("图标 X 偏移", &config.game_overlay_icon_offset_x, -500.0f, 500.0f, "%.1f", "##value", "图标相对于锚点的水平偏移量"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::SliderFloatRow("图标 Y 偏移", &config.game_overlay_icon_offset_y, -500.0f, 500.0f, "%.1f", "##value", "图标相对于锚点的垂直偏移量"))
            OverlayConfig_MarkDirty();

        if (OverlayUI::InputIntRow("图标类别（-1 = 全部）", &config.game_overlay_icon_class, 1, 100, 0, "##value", "仅对指定类别的目标显示图标，-1 表示所有类别"))
        {
            if (config.game_overlay_icon_class < -1) config.game_overlay_icon_class = -1;
            OverlayConfig_MarkDirty();
        }

        const char* anchors[] = { "center", "top", "bottom", "head" };
        int currentAnchor = 0;
        for (int i = 0; i < (int)(sizeof(anchors) / sizeof(anchors[0])); ++i)
        {
            if (config.game_overlay_icon_anchor == anchors[i])
            {
                currentAnchor = i;
                break;
            }
        }

        if (OverlayUI::ComboRow("图标锚点", &currentAnchor, anchors, IM_ARRAYSIZE(anchors), "##value", "图标在目标上的对齐位置：中心、顶部、底部或头部"))
        {
            config.game_overlay_icon_anchor = anchors[currentAnchor];
            OverlayConfig_MarkDirty();
        }

        if (!config.game_overlay_icon_enabled)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("启用图标叠加以编辑设置。");
        }

        OverlayUI::EndSection();
    }

    if (shouldDrawGameOverlayPage(page, GameOverlaySettingsPage::Icon) && !g_iconLastError.empty())
    {
        if (OverlayUI::BeginSection("错误", "game_overlay_section_errors"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
            ImGui::TextWrapped("%s", g_iconLastError.c_str());
            ImGui::PopStyleColor();
            OverlayUI::EndSection();
        }
    }

}

void draw_game_overlay_settings()
{
    draw_game_overlay_page(GameOverlaySettingsPage::All);
}

void draw_game_overlay_general()
{
    draw_game_overlay_page(GameOverlaySettingsPage::General);
}

void draw_game_overlay_visuals()
{
    draw_game_overlay_page(GameOverlaySettingsPage::Visuals);
}

void draw_game_overlay_icon()
{
    draw_game_overlay_page(GameOverlaySettingsPage::Icon);
}
