#include "embedded_resource.h"

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
// 资源 ID（与 embedded_resources.rc 保持一致）
constexpr int IDR_RZCTL_DLL = 101;
constexpr int IDR_GHUB_MOUSE_DLL = 102;
// 自定义资源类型名（与 .rc 文件中 "DLL" 一致）
constexpr const wchar_t* kResourceType = L"DLL";

// 从 exe 资源中提取一个 RCDATA/DLL 资源到目标路径。
// 失败返回 false。目标已存在时不覆盖（返回 true，视为成功跳过）。
bool ExtractResource(int resourceId, const std::filesystem::path& targetPath)
{
    std::error_code ec;
    if (std::filesystem::exists(targetPath, ec))
    {
        // 已存在，不覆盖。用户可能放置了更新版本。
        return true;
    }

    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), kResourceType);
    if (!hRes)
        return false;

    HGLOBAL hMem = LoadResource(nullptr, hRes);
    if (!hMem)
        return false;

    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hMem);
    if (!data || size == 0)
        return false;

    // 确保父目录存在
    const auto parent = targetPath.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec))
    {
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    out.close();
    return out.good();
}

std::filesystem::path GetExeDir()
{
    wchar_t buffer[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, buffer, MAX_PATH) == 0)
        return std::filesystem::path();
    return std::filesystem::path(buffer).parent_path();
}
}

namespace EmbeddedResource
{
void ExtractAll()
{
    const std::filesystem::path exeDir = GetExeDir();
    if (exeDir.empty())
        return;

    struct ResourceEntry
    {
        int id;
        const wchar_t* fileName;
        const char* label;
    };

    const ResourceEntry entries[] = {
        {IDR_RZCTL_DLL, L"rzctl.dll", "rzctl.dll"},
        {IDR_GHUB_MOUSE_DLL, L"ghub_mouse.dll", "ghub_mouse.dll"},
    };

    for (const auto& entry : entries)
    {
        const auto target = exeDir / entry.fileName;
        if (ExtractResource(entry.id, target))
        {
            // 仅在首次释放（之前不存在）时打印，避免每次启动刷屏。
            // ExtractResource 内部已处理"已存在则跳过"，这里无法区分，
            // 所以保持静默；释放失败的诊断由各模块 LoadLibrary 时输出。
        }
        else
        {
            // 资源不存在或写入失败：仅 debug 提示，不阻断启动
            std::cerr << "[Embed] Failed to extract " << entry.label << std::endl;
        }
    }
}
}
