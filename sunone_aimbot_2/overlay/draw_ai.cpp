#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "imgui/imgui.h"

#include "sunone_aimbot_2.h"
#include "include/other_tools.h"
#include "overlay.h"
#include "overlay/config_dirty.h"
#include "draw_settings.h"
#include "overlay/ui_sections.h"
#ifdef USE_CUDA
#include "overlay/export_progress_panel.h"
#include "trt_monitor.h"
#endif

std::string prev_backend = config.backend;
float prev_confidence_threshold = config.confidence_threshold;
float prev_nms_threshold = config.nms_threshold;
int prev_max_detections = config.max_detections;

static bool wasExporting = false;
static bool ai_state_initialized = false;

void draw_ai()
{
#ifdef USE_CUDA
    config.backend = "TRT";
#else
    config.backend = "DML";
#endif

    if (!ai_state_initialized)
    {
        prev_backend = config.backend;
        prev_confidence_threshold = config.confidence_threshold;
        prev_nms_threshold = config.nms_threshold;
        prev_max_detections = config.max_detections;
        ai_state_initialized = true;
    }

#ifdef USE_CUDA
    if (gIsTrtExporting)
    {
        OverlayExportUI::DrawTensorRtExportPanel(
            "ai_tensor_rt_export",
            "TensorRT 引擎导出",
            "正在编译优化后的 AI 推理引擎",
            config.ai_model.c_str(),
            "取消导出");
    }
#endif
    std::vector<std::string> availableModels = getAvailableModels();
    if (OverlayUI::BeginSection("模型", "ai_section_model"))
    {
        if (availableModels.empty())
        {
            ImGui::Text("'models' 文件夹中没有可用模型。");
        }
        else
        {
            int currentModelIndex = 0;
            auto it = std::find(availableModels.begin(), availableModels.end(), config.ai_model);

            if (it != availableModels.end())
            {
                currentModelIndex = static_cast<int>(std::distance(availableModels.begin(), it));
            }

            std::vector<const char*> modelsItems;
            modelsItems.reserve(availableModels.size());

            for (const auto& modelName : availableModels)
            {
                modelsItems.push_back(modelName.c_str());
            }

            {
                const auto row = OverlayUI::BeginSettingRow("模型");
                if (ImGui::Combo("##model", &currentModelIndex, modelsItems.data(), static_cast<int>(modelsItems.size())))
                {
                    if (config.ai_model != availableModels[currentModelIndex])
                    {
                        config.ai_model = availableModels[currentModelIndex];
                        OverlayConfig_MarkDirty();
                        detector_model_changed.store(true);
                    }
                }
                OverlayUI::EndSettingRow(row);
            }

            OverlayUI::TextRow(config.fixed_input_size ? "固定模型尺寸：已启用" : "固定模型尺寸：已禁用",
                IM_COL32(188, 188, 188, 255));
        }
        OverlayUI::EndSection();
    }

    if (OverlayUI::BeginSection("检测", "ai_section_detection"))
    {
        {
            const auto row = OverlayUI::BeginSettingRow("置信度阈值");
            ImGui::SliderFloat("##confidence_threshold", &config.confidence_threshold, 0.01f, 1.00f, "%.2f");
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("NMS 阈值");
            ImGui::SliderFloat("##nms_threshold", &config.nms_threshold, 0.00f, 1.00f, "%.2f");
            OverlayUI::EndSettingRow(row);
        }

        {
            const auto row = OverlayUI::BeginSettingRow("最大检测数");
            ImGui::SliderInt("##max_detections", &config.max_detections, 1, 100);
            OverlayUI::EndSettingRow(row);
        }
        OverlayUI::EndSection();
    }

    if (prev_confidence_threshold != config.confidence_threshold ||
        prev_nms_threshold != config.nms_threshold ||
        prev_max_detections != config.max_detections)
    {
        prev_nms_threshold = config.nms_threshold;
        prev_confidence_threshold = config.confidence_threshold;
        prev_max_detections = config.max_detections;
        OverlayConfig_MarkDirty();
    }

    if (prev_backend != config.backend)
    {
        prev_backend = config.backend;
        detector_model_changed.store(true);
        OverlayConfig_MarkDirty();
    }
}
