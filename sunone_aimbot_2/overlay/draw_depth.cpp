#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <commdlg.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <algorithm>
#include <exception>
#include <thread>
#include <atomic>
#include <mutex>

#include "imgui/imgui.h"
#include "sunone_aimbot_2.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "capture.h"
#include "draw_settings.h"
#include "include/other_tools.h"
#include "overlay/ui_sections.h"

#ifdef USE_CUDA
#include "overlay/export_progress_panel.h"
#include "depth/depth_anything_trt.h"
#include "depth/depth_mask.h"
#include "tensorrt/nvinf.h"
#include "tensorrt/trt_monitor.h"
#endif

#ifdef USE_CUDA
static const char* kDepthColormapNames[] = {
    "Autumn",
    "Bone",
    "Jet",
    "Winter",
    "Rainbow",
    "Ocean",
    "Summer",
    "Spring",
    "Cool",
    "HSV",
    "Pink",
    "Hot",
    "Parula",
    "Magma",
    "Inferno",
    "Plasma",
    "Viridis",
    "Cividis",
    "Twilight",
    "Twilight Shifted",
    "Turbo",
    "Deepgreen"
};

#endif

void draw_depth()
{
#ifndef USE_CUDA
    if (OverlayUI::BeginSection("深度", "depth_section_unavailable"))
    {
        ImGui::TextUnformatted("深度功能需要 CUDA 版本。");
        OverlayUI::EndSection();
    }
    return;
#else
    static std::string depthStatus = "深度运行时由系统自动管理。";
    static std::atomic<bool> depthExportRunning{ false };
    static std::thread depthExportThread;
    static std::mutex depthExportMutex;
    static std::string depthExportResult;
    static std::string depthExportedModel;

    if (depthExportThread.joinable() && !depthExportRunning.load())
    {
        depthExportThread.join();
    }
    std::string completedEngineModel;
    {
        std::lock_guard<std::mutex> lock(depthExportMutex);
        if (!depthExportResult.empty())
        {
            depthStatus = depthExportResult;
            completedEngineModel = depthExportedModel;
            depthExportResult.clear();
            depthExportedModel.clear();
        }
    }
    if (!completedEngineModel.empty() && config.depth_model_path != completedEngineModel)
    {
        config.depth_model_path = completedEngineModel;
        OverlayConfig_MarkDirty();
    }

    std::vector<std::string> availableDepthModels = getAvailableDepthModels();
    std::string selectedModel;
    bool hasModels = !availableDepthModels.empty();

    if (OverlayUI::BeginSection("深度推理", "depth_section_inference"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("启用深度推理");
            if (ImGui::Checkbox("##enable_depth_inference", &config.depth_inference_enabled))
            {
                OverlayConfig_MarkDirty();
                if (!config.depth_inference_enabled)
                    depthStatus = "深度推理已禁用。";
            }
            OverlayUI::EndSettingRow(row);
        }

        if (!hasModels)
        {
            OverlayUI::TextRow("'depth_models' 中没有可用深度模型。", IM_COL32(255, 108, 108, 255));
        }
        else
        {
            int currentModelIndex = 0;
            auto it = std::find(availableDepthModels.begin(), availableDepthModels.end(), config.depth_model_path);
            if (it == availableDepthModels.end())
            {
                std::string configFile = std::filesystem::path(config.depth_model_path).filename().string();
                it = std::find(availableDepthModels.begin(), availableDepthModels.end(), configFile);
            }
            if (it != availableDepthModels.end())
            {
                currentModelIndex = static_cast<int>(std::distance(availableDepthModels.begin(), it));
            }

            std::vector<const char*> modelItems;
            modelItems.reserve(availableDepthModels.size());
            for (const auto& modelName : availableDepthModels)
            {
                modelItems.push_back(modelName.c_str());
            }

            const auto row = OverlayUI::BeginSettingRow("深度模型");
            if (ImGui::Combo("##depth_model", &currentModelIndex, modelItems.data(), static_cast<int>(modelItems.size())))
            {
                if (config.depth_model_path != availableDepthModels[currentModelIndex])
                {
                    config.depth_model_path = availableDepthModels[currentModelIndex];
                    OverlayConfig_MarkDirty();
                }
            }
            OverlayUI::EndSettingRow(row);

            selectedModel = availableDepthModels[currentModelIndex];
        }

        const bool selectedIsOnnx = hasModels && OtherTools::HasExtensionCaseInsensitive(selectedModel, ".onnx");
        const bool exportBusy = depthExportRunning.load();
        if (!hasModels || selectedIsOnnx || exportBusy)
        {
            ImGui::BeginDisabled();
        }
        {
            const auto row = OverlayUI::BeginSettingRow("加载深度模型");
            if (ImGui::Button("加载", ImVec2(row.controlWidth, 0.0f)))
            {
                if (config.depth_model_path != selectedModel)
                {
                    config.depth_model_path = selectedModel;
                    OverlayConfig_MarkDirty();
                    depthStatus = "深度模型路径已应用。运行时将自动更新加载。";
                }
                else
                {
                    depthStatus = "深度模型路径已选中。";
                }
            }
            OverlayUI::EndSettingRow(row);
        }
        if (!hasModels || selectedIsOnnx || exportBusy)
        {
            ImGui::EndDisabled();
        }

        if (!hasModels || !selectedIsOnnx || exportBusy)
        {
            ImGui::BeginDisabled();
        }
        {
            const auto row = OverlayUI::BeginSettingRow("导出深度引擎");
            if (ImGui::Button("导出", ImVec2(row.controlWidth, 0.0f)))
            {
                if (!depthExportRunning.load())
                {
                    if (config.depth_model_path != selectedModel)
                    {
                        config.depth_model_path = selectedModel;
                        OverlayConfig_MarkDirty();
                    }

                    std::string exportPath = selectedModel;
                    if (exportPath.empty())
                    {
                        depthStatus = "请设置要导出的深度 ONNX 路径。";
                    }
                    else if (!OtherTools::HasExtensionCaseInsensitive(exportPath, ".onnx"))
                    {
                        depthStatus = "导出需要 .onnx 深度模型路径。";
                    }
                    else
                    {
                        depthExportRunning.store(true);
                        depthStatus = "深度引擎导出已开始...";
                        depthExportThread = std::thread([exportPath] {
                            depth_anything::DepthAnythingTrt exporter;
                            std::string result;
                            std::string exportedModel;
                            try
                            {
                                std::string enginePath;
                                if (exporter.exportEngine(exportPath, gLogger, &enginePath))
                                {
                                    exportedModel = std::filesystem::path(enginePath).filename().string();
                                    result = "深度引擎已导出：" + exportedModel;
                                }
                                else
                                {
                                    if (gTrtExportCancelRequested.load())
                                    {
                                        result = "深度导出已取消。";
                                    }
                                    else
                                    {
                                        result = exporter.lastError();
                                    }
                                }
                            }
                            catch (const std::exception& e)
                            {
                                result = std::string("深度导出失败：") + e.what();
                                exportedModel.clear();
                            }
                            catch (...)
                            {
                                result = "深度导出失败，发生未知错误。";
                                exportedModel.clear();
                            }
                            {
                                std::lock_guard<std::mutex> lock(depthExportMutex);
                                depthExportResult = result;
                                depthExportedModel = exportedModel;
                            }
                            depthExportRunning.store(false);
                        });
                    }
                }
            }
            OverlayUI::EndSettingRow(row);
        }
        if (!hasModels || !selectedIsOnnx || exportBusy)
        {
            ImGui::EndDisabled();
        }

        if (exportBusy)
        {
            OverlayExportUI::DrawTensorRtExportPanel(
                "depth_tensor_rt_export",
                "深度引擎导出",
                "正在编译优化后的深度推理引擎",
                selectedModel.c_str(),
                "取消深度导出");
        }

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("深度运行时", "depth_section_runtime"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("深度 FPS");
            if (ImGui::SliderInt("##depth_fps", &config.depth_fps, 0, 120))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("深度遮罩 FPS");
            if (ImGui::SliderInt("##depth_mask_fps", &config.depth_mask_fps, 1, 30))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }
        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("深度遮罩", "depth_section_mask"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("启用深度遮罩");
            if (ImGui::Checkbox("##enable_depth_mask", &config.depth_mask_enabled))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("深度遮罩近处 %");
            if (ImGui::SliderInt("##depth_mask_near_percent", &config.depth_mask_near_percent, 1, 100))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("深度遮罩扩展（像素）");
            if (ImGui::SliderInt("##depth_mask_expand", &config.depth_mask_expand, 0, 128))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("深度遮罩保持帧数");
            if (ImGui::SliderInt("##depth_mask_hold_frames", &config.depth_mask_hold_frames, 0, 120))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("深度遮罩不透明度");
            if (ImGui::SliderInt("##depth_mask_alpha", &config.depth_mask_alpha, 0, 255))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }
        if (config.depth_mask_enabled && config.depth_mask_alpha == 0)
        {
            OverlayUI::TextRow("深度遮罩不可见：不透明度为 0。", IM_COL32(255, 108, 108, 255));
        }

        {
            const auto row = OverlayUI::BeginSettingRow("深度遮罩反转");
            if (ImGui::Checkbox("##depth_mask_invert", &config.depth_mask_invert))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("深度调试叠加（游戏）");
            if (ImGui::Checkbox("##depth_debug_overlay_game", &config.depth_debug_overlay_enabled))
            {
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        int colormapIndex = config.depth_colormap;
        {
            const auto row = OverlayUI::BeginSettingRow("深度色图");
            if (ImGui::Combo("##depth_colormap", &colormapIndex, kDepthColormapNames, IM_ARRAYSIZE(kDepthColormapNames)))
            {
                config.depth_colormap = colormapIndex;
                OverlayConfig_MarkDirty();
            }
            OverlayUI::EndSettingRow(row);
        }

        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("深度状态", "depth_section_status"))
    {
        ImGui::Text("状态：%s", depthStatus.c_str());

        if (config.depth_inference_enabled && config.depth_mask_enabled)
        {
            auto& depthMask = depth_anything::GetDepthMaskGenerator();
            const auto state = depthMask.debugState();
            const auto lastErr = depthMask.lastError();
            const auto frameSize = depthMask.lastFrameSize();

            ImGui::Separator();
            ImGui::Text("遮罩运行时：%s", state.model_ready ? "ready" : "not ready");
            ImGui::Text("遮罩模型路径：%s",
                state.last_model_path.empty() ? "（无）" : state.last_model_path.c_str());
            if (frameSize.first > 0 && frameSize.second > 0)
                ImGui::Text("上一帧遮罩：%dx%d", frameSize.first, frameSize.second);

            if (!lastErr.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "遮罩错误：%s", lastErr.c_str());
        }
        else if (config.depth_inference_enabled)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("深度遮罩已禁用。");
        }
        else
        {
            ImGui::Separator();
            ImGui::TextUnformatted("深度推理已禁用。");
        }

        ImGui::TextUnformatted("启用调试叠加后，深度预览将显示在游戏画面中。");
        OverlayUI::EndSection();
    }
#endif
}
