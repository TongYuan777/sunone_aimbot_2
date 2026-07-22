#ifndef EMBEDDED_RESOURCE_H
#define EMBEDDED_RESOURCE_H

// 嵌入式资源释放接口
// 启动时将嵌入到 exe 中的小 DLL（rzctl.dll、ghub_mouse.dll）释放到 exe 所在目录，
// 这样用户分发时无需单独携带这些 DLL 文件。
// 已存在的同名文件不会被覆盖，便于用户使用更新版本。
namespace EmbeddedResource
{
    // 释放所有嵌入的 DLL 资源。在 main 早期阶段、创建输入设备之前调用。
    void ExtractAll();
}

#endif // EMBEDDED_RESOURCE_H
