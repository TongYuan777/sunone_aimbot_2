#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <d3d11.h>
#include <iostream>
#include <string>
#include <vector>

#include "imgui/imgui.h"
#include "scr/data_collector.h"
#include "sunone_aimbot_2.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "include/other_tools.h"
#include "capture.h"
#include "overlay/ui_sections.h"

#ifdef USE_CUDA
#include "depth/depth_mask.h"
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)       \
    do {                      \
        if ((p) != nullptr) { \
            (p)->Release();   \
            (p) = nullptr;    \
        }                     \
    } while (0)
#endif

int prev_screenshot_delay = config.screenshot_delay;
bool prev_verbose = config.verbose;

static ID3D11Texture2D* g_debugTex = nullptr;
static ID3D11ShaderResourceView* g_debugSRV = nullptr;
static int texW = 0, texH = 0;

static ID3D11Texture2D* g_maskTex = nullptr;
static ID3D11ShaderResourceView* g_maskSRV = nullptr;
static int maskTexW = 0, maskTexH = 0;

static float debug_scale = 0.5f;
static char g_collectOutputDirBuffer[512] = {};
static std::string g_collectOutputDirMirror;
static char g_collectClassFilterBuffer[256] = {};
static std::string g_collectClassFilterMirror;

static void syncDebugTextBuffer(char* buffer, size_t buffer_size, std::string& mirror, const std::string& value)
{
    if (mirror == value)
        return;

    std::snprintf(buffer, buffer_size, "%s", value.c_str());
    buffer[buffer_size - 1] = '\0';
    mirror = value;
}

static bool applyDebugTextBuffer(std::string& target, std::string& mirror, const char* buffer)
{
    const std::string value = buffer ? std::string(buffer) : std::string();
    if (target == value && mirror == value)
        return false;

    target = value;
    mirror = value;
    return true;
}

static int findDebugKeyIndexByName(const std::string& keyName)
{
    for (size_t k = 0; k < key_names.size(); ++k)
    {
        if (key_names[k] == keyName)
            return static_cast<int>(k);
    }
    return 0;
}

static bool drawScreenshotButtonRows()
{
    if (key_names_cstrs.empty())
    {
        ImGui::TextDisabled("无可用按键列表。");
        return false;
    }

    bool changed = false;
    if (config.screenshot_button.empty())
    {
        config.screenshot_button.push_back("None");
        changed = true;
    }

    for (size_t i = 0; i < config.screenshot_button.size();)
    {
        std::string& currentKeyName = config.screenshot_button[i];
        int currentIndex = findDebugKeyIndexByName(currentKeyName);
        const std::string rowLabel = (config.screenshot_button.size() > 1)
            ? "截图 " + std::to_string(i + 1)
            : "截图";

        ImGui::PushID(static_cast<int>(i));

        const auto row = OverlayUI::BeginSettingRow(rowLabel.c_str());
        const float actionBtnW = ImGui::GetFrameHeight();
        float comboWidth = row.controlWidth - (actionBtnW * 2.0f + 7.0f);
        if (comboWidth < 1.0f)
            comboWidth = 1.0f;
        ImGui::SetNextItemWidth(comboWidth);

        if (ImGui::Combo("##value", &currentIndex, key_names_cstrs.data(), static_cast<int>(key_names_cstrs.size())))
        {
            currentKeyName = key_names[currentIndex];
            changed = true;
        }

        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("+", ImVec2(actionBtnW, 0.0f)))
        {
            config.screenshot_button.insert(config.screenshot_button.begin() + static_cast<std::vector<std::string>::difference_type>(i + 1), "None");
            changed = true;
        }

        ImGui::SameLine(0.0f, 3.0f);
        bool removedCurrent = false;
        if (ImGui::Button("-", ImVec2(actionBtnW, 0.0f)))
        {
            if (config.screenshot_button.size() <= 1)
            {
                config.screenshot_button[0] = "None";
            }
            else
            {
                config.screenshot_button.erase(config.screenshot_button.begin() + static_cast<std::vector<std::string>::difference_type>(i));
                removedCurrent = true;
            }
            changed = true;
        }

        OverlayUI::EndSettingRow(row);
        ImGui::PopID();

        if (removedCurrent)
            continue;

        ++i;
    }

    return changed;
}

static void uploadDebugFrame(const cv::Mat& bgr)
{
    if (bgr.empty()) return;

    if (!g_debugTex || bgr.cols != texW || bgr.rows != texH)
    {
        SAFE_RELEASE(g_debugTex);
        SAFE_RELEASE(g_debugSRV);

        texW = bgr.cols;  texH = bgr.rows;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = texW;
        td.Height = texH;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        g_pd3dDevice->CreateTexture2D(&td, nullptr, &g_debugTex);

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = td.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        g_pd3dDevice->CreateShaderResourceView(g_debugTex, &sd, &g_debugSRV);
    }

    static cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(g_pd3dDeviceContext->Map(g_debugTex, 0,
        D3D11_MAP_WRITE_DISCARD, 0, &ms)))
    {
        for (int y = 0; y < texH; ++y)
            memcpy((uint8_t*)ms.pData + ms.RowPitch * y,
                rgba.ptr(y), texW * 4);
        g_pd3dDeviceContext->Unmap(g_debugTex, 0);
    }
}

static void uploadMaskFrame(const cv::Mat& rgba)
{
    if (rgba.empty()) return;

    if (!g_maskTex || rgba.cols != maskTexW || rgba.rows != maskTexH)
    {
        SAFE_RELEASE(g_maskTex);
        SAFE_RELEASE(g_maskSRV);

        maskTexW = rgba.cols;
        maskTexH = rgba.rows;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = maskTexW;
        td.Height = maskTexH;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        g_pd3dDevice->CreateTexture2D(&td, nullptr, &g_maskTex);

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = td.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        g_pd3dDevice->CreateShaderResourceView(g_maskTex, &sd, &g_maskSRV);
    }

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(g_pd3dDeviceContext->Map(g_maskTex, 0,
        D3D11_MAP_WRITE_DISCARD, 0, &ms)))
    {
        for (int y = 0; y < maskTexH; ++y)
            memcpy((uint8_t*)ms.pData + ms.RowPitch * y,
                rgba.ptr(y), maskTexW * 4);
        g_pd3dDeviceContext->Unmap(g_maskTex, 0);
    }
}

static bool drawDataCollectionSection()
{
    syncDebugTextBuffer(g_collectOutputDirBuffer, sizeof(g_collectOutputDirBuffer), g_collectOutputDirMirror, config.collect_output_dir);
    syncDebugTextBuffer(g_collectClassFilterBuffer, sizeof(g_collectClassFilterBuffer), g_collectClassFilterMirror, config.auto_label_record_classes);

    bool changed = false;
    if (!OverlayUI::BeginSection("数据采集", "debug_section_data_collection"))
        return false;

    changed |= OverlayUI::CheckboxRow("运行时采集数据", &config.collect_data_while_playing, "##value", "在游戏运行时持续采集训练数据");
    changed |= OverlayUI::CheckboxRow("仅自瞄激活时", &config.collect_only_when_aimbot_running, "##value", "仅自瞄功能激活时才采集数据");
    changed |= OverlayUI::CheckboxRow("仅存在目标时", &config.collect_only_when_targets_present, "##value", "仅画面中存在检测目标时才采集");

    int saveEveryNFrames = config.collect_save_every_n_frames;
    if (OverlayUI::SliderIntRow("每 N 帧保存", &saveEveryNFrames, 1, 120, "%d", "##value", "每隔多少帧保存一次样本，值越小采集越频繁"))
    {
        config.collect_save_every_n_frames = saveEveryNFrames;
        changed = true;
    }

    int jpegQuality = config.collect_jpeg_quality;
    if (OverlayUI::SliderIntRow("JPEG 质量", &jpegQuality, 50, 100, "%d", "##value", "保存图片的 JPEG 压缩质量，越高越清晰"))
    {
        config.collect_jpeg_quality = jpegQuality;
        changed = true;
    }

    if (OverlayUI::InputTextRow("输出文件夹", g_collectOutputDirBuffer, sizeof(g_collectOutputDirBuffer), 0, "##value", "采集样本保存的输出目录路径"))
        changed |= applyDebugTextBuffer(config.collect_output_dir, g_collectOutputDirMirror, g_collectOutputDirBuffer);

    if (OverlayUI::BeginSubsection("自动标注"))
    {
        changed |= OverlayUI::CheckboxRow("写入 YOLO txt 标签", &config.auto_label_data, "##value", "自动生成 YOLO 格式的标签文件");

        ImGui::BeginDisabled(!config.auto_label_data);

        float minConf = config.auto_label_min_conf;
        if (OverlayUI::SliderFloatRow("最低置信度", &minConf, 0.01f, 0.99f, "%.2f", "##value", "仅记录置信度高于此阈值的目标"))
        {
            config.auto_label_min_conf = minConf;
            changed = true;
        }

        int maxBoxes = config.auto_label_max_boxes;
        if (OverlayUI::SliderIntRow("每文件最大框数", &maxBoxes, 1, 100, "%d", "##value", "单个标签文件最多包含的检测框数"))
        {
            config.auto_label_max_boxes = maxBoxes;
            changed = true;
        }

        if (OverlayUI::InputTextRow("类别筛选", g_collectClassFilterBuffer, sizeof(g_collectClassFilterBuffer), 0, "##value", "仅记录指定类别 ID，逗号分隔，留空记录全部"))
            changed |= applyDebugTextBuffer(config.auto_label_record_classes, g_collectClassFilterMirror, g_collectClassFilterBuffer);

        ImGui::TextDisabled("留空以记录所有类别。使用逗号分隔的 ID，如 0,1。");
        ImGui::EndDisabled();

        OverlayUI::EndSubsection();
    }

    const cvm::DataCollectionUiState ui = cvm::GetDataCollectionUiState("", config.ai_model.c_str(), config);
    ImGui::Separator();
    ImGui::Text("已观测帧：%llu", static_cast<unsigned long long>(ui.observed_frame_count));
    ImGui::Text("保存尝试：%llu", static_cast<unsigned long long>(ui.attempted_sample_count));
    ImGui::Text("已保存图片：%llu", static_cast<unsigned long long>(ui.saved_image_count));
    ImGui::Text("标签文件：%llu", static_cast<unsigned long long>(ui.saved_label_count));
    ImGui::TextWrapped("解析目录：%s", ui.resolved_output_dir.c_str());
    if (!ui.status.empty())
        ImGui::TextWrapped("状态：%s", ui.status.c_str());
    else
        ImGui::TextDisabled("状态：空闲");

    if (OverlayUI::ButtonRow("解析目录", "复制路径", "copy_resolved_path", "将解析后的输出目录路径复制到剪贴板"))
        ImGui::SetClipboardText(ui.resolved_output_dir.c_str());

    if (OverlayUI::ButtonRow("采集计数器", "重置计数", "reset_collect_counters", "重置已观测帧与保存计数的统计值"))
        cvm::ResetDataCollectionRuntime();

    OverlayUI::EndSection();
    return changed;
}

void draw_debug_frame()
{
    cv::Mat frameCopy;
    {
        std::lock_guard<std::mutex> lk(frameMutex);
        if (!latestFrame.empty())
            latestFrame.copyTo(frameCopy);
    }

    uploadDebugFrame(frameCopy);

    if (!g_debugSRV) return;

    {
        const auto row = OverlayUI::BeginSettingRow("调试缩放");
        ImGui::SliderFloat("##value", &debug_scale, 0.1f, 2.0f, "%.1fx");
        OverlayUI::EndSettingRow(row);
    }

    ImVec2 image_size(texW * debug_scale, texH * debug_scale);
    ImGui::Image((ImTextureID)(intptr_t)g_debugSRV, image_size);

    ImVec2 image_pos = ImGui::GetItemRectMin();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

#ifdef USE_CUDA
    if (config.depth_mask_enabled)
    {
        auto& depthMask = depth_anything::GetDepthMaskGenerator();
        cv::Mat mask = depthMask.getMask();
        if (!mask.empty() && mask.size() == frameCopy.size())
        {
            cv::Mat alpha(mask.size(), CV_8U, cv::Scalar(0));
            alpha.setTo(config.depth_mask_alpha, mask);

            std::vector<cv::Mat> channels(4);
            channels[0] = cv::Mat(mask.size(), CV_8U, cv::Scalar(255));
            channels[1] = cv::Mat(mask.size(), CV_8U, cv::Scalar(0));
            channels[2] = cv::Mat(mask.size(), CV_8U, cv::Scalar(0));
            channels[3] = alpha;

            cv::Mat rgba;
            cv::merge(channels, rgba);
            uploadMaskFrame(rgba);

            if (g_maskSRV)
            {
                ImVec2 overlay_max(image_pos.x + image_size.x, image_pos.y + image_size.y);
                draw_list->AddImage((ImTextureID)(intptr_t)g_maskSRV, image_pos, overlay_max);
            }
        }
    }
#endif

    {
        std::lock_guard<std::mutex> lock(detectionBuffer.mutex);
        for (size_t i = 0; i < detectionBuffer.boxes.size(); ++i)
        {
            const cv::Rect& box = detectionBuffer.boxes[i];

            ImVec2 p1(image_pos.x + box.x * debug_scale,
                image_pos.y + box.y * debug_scale);
            ImVec2 p2(p1.x + box.width * debug_scale,
                p1.y + box.height * debug_scale);

            ImU32 color = IM_COL32(255, 0, 0, 255);

            draw_list->AddRect(p1, p2, color, 0.0f, 2.0f);

            if (i < detectionBuffer.classes.size())
            {
                std::string label = std::to_string(detectionBuffer.classes[i]);
                draw_list->AddText(ImVec2(p1.x, p1.y - 16), IM_COL32(255, 255, 0, 255), label.c_str());
            }
        }
    }

    if (config.draw_futurePositions && globalMouseThread)
    {
        auto futurePts = globalMouseThread->getFuturePositions();
        if (!futurePts.empty())
        {
            float scale_x = static_cast<float>(texW) / config.detection_resolution;
            float scale_y = static_cast<float>(texH) / config.detection_resolution;

            ImVec2 clip_min = image_pos;
            ImVec2 clip_max = ImVec2(image_pos.x + texW * debug_scale,
                image_pos.y + texH * debug_scale);
            draw_list->PushClipRect(clip_min, clip_max, true);

            int totalPts = static_cast<int>(futurePts.size());
            for (size_t i = 0; i < futurePts.size(); ++i)
            {
                int px = static_cast<int>(futurePts[i].first * scale_x);
                int py = static_cast<int>(futurePts[i].second * scale_y);
                ImVec2 pt(image_pos.x + px * debug_scale,
                    image_pos.y + py * debug_scale);

                int b = static_cast<int>(255 - (i * 255.0 / totalPts));
                int r = static_cast<int>(i * 255.0 / totalPts);
                int g = 50;

                ImU32 fillColor = IM_COL32(r, g, b, 255);
                ImU32 outlineColor = IM_COL32(255, 255, 255, 255);

                draw_list->AddCircleFilled(pt, 4.0f * debug_scale, fillColor);
                draw_list->AddCircle(pt, 4.0f * debug_scale, outlineColor, 0, 1.0f);
            }

            draw_list->PopClipRect();
        }
    }
}

void draw_capture_preview()
{
    if (OverlayUI::BeginSection("采集预览", "capture_section_preview"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("显示预览窗口");
            if (ImGui::Checkbox("##value", &config.show_window))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        if (config.show_window)
        {
            draw_debug_frame();
        }

        OverlayUI::EndSection();
    }
}

void draw_debug()
{
    bool changed = false;

    if (OverlayUI::BeginSection("截图按键", "debug_section_screenshot_buttons"))
    {
        if (drawScreenshotButtonRows())
            changed = true;

        if (OverlayUI::InputIntRow("截图延迟", &config.screenshot_delay, 50, 500, 0, "##value", "连续截图之间的间隔毫秒数"))
            changed = true;
        if (OverlayUI::CheckboxRow("详细控制台输出", &config.verbose, "##value", "在控制台输出更详细的运行日志信息"))
            changed = true;

        if (config.screenshot_delay < 0)
            config.screenshot_delay = 0;

        if (OverlayUI::ButtonRow("OpenCV", "打印构建信息", "button_cv2_build_info", "在控制台打印 OpenCV 编译配置信息"))
        {
            std::cout << cv::getBuildInformation() << std::endl;
        }

        OverlayUI::EndSection();
    }

    changed |= drawDataCollectionSection();

    if (prev_screenshot_delay != config.screenshot_delay ||
        prev_verbose != config.verbose)
    {
        prev_screenshot_delay = config.screenshot_delay;
        prev_verbose = config.verbose;
        changed = true;
    }

    if (changed)
    {
        OverlayConfig_MarkDirty();
    }
}
