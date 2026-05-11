// VulkanLoaderShim 实现 —— 见 VulkanLoaderShim.h 的注释。
//
// 本 TU 拢着所有 Win32 / 直接 Vulkan / COM 调用，让 main.cpp 与 panel /
// camera / asset 这些纯 UI / ECS 逻辑的 TU 不需要包 <windows.h> /
// <shobjidl.h> 等重量级 header。

#include "VulkanLoaderShim.h"

#include <cstdio>
#include <cstring>

// Windows IFileDialog —— 编辑器 File 菜单的 Open / Save As 走 native
// Common Item Dialog（COM）。NOMINMAX / WIN32_LEAN_AND_MEAN 避免污染
// std::min / max 等符号 + 减少 windows.h 拉的"无关海洋"。
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>

PFN_vkVoidFunction ImguiVulkanLoader(const char* funcName, void* userData)
{
    const auto* ctx = static_cast<const ImguiVulkanLoaderCtx*>(userData);

    // ImGui docking v1.91.5 的 imgui_impl_vulkan.cpp 硬编码用 KHR 后缀
    // 名解析 dynamic rendering 两个命令：
    //     ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR =
    //         loader_func("vkCmdBeginRenderingKHR", user_data);
    //     ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR =
    //         loader_func("vkCmdEndRenderingKHR", user_data);
    //
    // 这里我们**必须**拦截这两个名字并改用 vkGetDeviceProcAddr 解析到
    // core 名字（无 KHR 后缀）。
    //
    // [trampoline 陷阱] 1.3 SDK 的 Vulkan loader 在
    // `vkGetInstanceProcAddr(inst, "vkCmdBeginRenderingKHR")` 上**总是**
    // 返回一个非 null 的 loader trampoline —— 不管 device 有没有 enable
    // VK_KHR_dynamic_rendering 扩展。调用时 trampoline 才去查 device
    // dispatch 表里 KHR 槽位；OrangeRender 启用的是 Vulkan 1.3 core 的
    // `dynamicRendering` feature，不是 KHR 扩展，KHR 槽位在 dispatch 表
    // 里是 null —— trampoline 一旦被调用就跳 0x0000_0000_0000_0000 访问
    // 冲突。
    //
    // 实测现象：watch 窗口里 ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR
    // 显示 vulkan-1.dll!0x...3790（非 null，是 loader trampoline），但抛
    // 异常 0xC0000005 在 0x0000_0000_0000_0000 —— 进了 trampoline、查不
    // 到 dispatch、空跳。
    //
    // 主视口看不出问题：overlay callback 在 OrangeRender 外层 begin/end
    // rendering scope 里调 RenderDrawData，ImGui 不会自己 call KHR 入口；
    // OrangeRender 自己的 vkCmdBeginRendering 走的是 volk →
    // vkGetDeviceProcAddr 拿到的驱动直接函数指针，绕开 loader trampoline
    // 那层。
    //
    // 出路就是这里 —— 解析 KHR 名时改用 vkGetDeviceProcAddr 拿 core 名字。
    // vkGetDeviceProcAddr 直接落到驱动 ICD，没有 loader trampoline 这层，
    // dispatch 不依赖扩展启用状态、只看 feature；core `dynamicRendering`
    // feature 已 enable，驱动会返回有效函数指针。Vulkan 1.3 promote 这两
    // 个 KHR 命令时是纯名字 promotion、签名完全一致，强制 cast 安全。
    //
    // 注意：仅对这两个**被 promote 的**命令做替换。其它 KHR 命令（如
    // vkAcquireNextImageKHR、vkCreateSwapchainKHR）是真扩展、未被 promote，
    // 不能做同样替换。
    if (ctx->pfnGetDeviceProcAddr != nullptr && ctx->vkDevice != VK_NULL_HANDLE) {
        const char* coreName = nullptr;
        if (std::strcmp(funcName, "vkCmdBeginRenderingKHR") == 0) {
            coreName = "vkCmdBeginRendering";
        } else if (std::strcmp(funcName, "vkCmdEndRenderingKHR") == 0) {
            coreName = "vkCmdEndRendering";
        }
        if (coreName != nullptr) {
            PFN_vkVoidFunction core =
                ctx->pfnGetDeviceProcAddr(ctx->vkDevice, coreName);
            if (core != nullptr) { return core; }
            // 兜底：万一驱动只导出 KHR 名字（极不常见），最后再回 instance
            // proc addr 试一次。
        }
    }

    return ctx->pfnGetInstanceProcAddr(ctx->vkInstance, funcName);
}

// 通过 OrangeRender 暴露的 loader fn 级联解 `vkGetDeviceProcAddr` →
// `vkCreateDescriptorPool`，与 ImGui 共用同一条 loader 解析路径；不再调
// 静态 vulkan-1.lib stub 的 `vkGetInstanceProcAddr`。
VkDescriptorPool MakeImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                          VkInstance               instance,
                                          VkDevice                 device)
{
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (vkGetDeviceProcAddrFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] resolve vkGetDeviceProcAddr failed\n");
        return VK_NULL_HANDLE;
    }
    auto vkCreateDescriptorPoolFn =
        reinterpret_cast<PFN_vkCreateDescriptorPool>(
            vkGetDeviceProcAddrFn(device, "vkCreateDescriptorPool"));
    if (vkCreateDescriptorPoolFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] resolve vkCreateDescriptorPool failed\n");
        return VK_NULL_HANDLE;
    }

    constexpr VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    };
    VkDescriptorPoolCreateInfo desc{};
    desc.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    desc.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    desc.maxSets       = 1000;
    desc.poolSizeCount = sizeof(sizes) / sizeof(sizes[0]);
    desc.pPoolSizes    = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPoolFn(device, &desc, nullptr, &pool) != VK_SUCCESS) {
        std::fprintf(stderr, "[OrangeEditor] vkCreateDescriptorPool failed\n");
    }
    return pool;
}

void DestroyImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                 VkInstance               instance,
                                 VkDevice                 device,
                                 VkDescriptorPool         pool)
{
    if (pool == VK_NULL_HANDLE) { return; }
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (vkGetDeviceProcAddrFn == nullptr) { return; }
    auto vkDestroyDescriptorPoolFn =
        reinterpret_cast<PFN_vkDestroyDescriptorPool>(
            vkGetDeviceProcAddrFn(device, "vkDestroyDescriptorPool"));
    if (vkDestroyDescriptorPoolFn != nullptr) {
        vkDestroyDescriptorPoolFn(device, pool, nullptr);
    }
}

// Windows native file dialog（IFileOpenDialog / IFileSaveDialog）包装。
//
// 设计：
// * isSave 决定调 FileSave 还是 FileOpen dialog；
// * 过滤器固定 ".scene.json"（编辑器场景文件后缀；与引擎 Scene::Save/Load
//   约定一致）；
// * 路径以 UTF-8 写回 outPath —— 引擎 Scene::Save / Load 接受 string_view，
//   传 UTF-8 即可；
// * CoInitializeEx STA 模式 —— ComDlg 要求；CoUninitialize 仅在本帧
//   真正初始化（hr == S_OK）时才调，避免误关掉调用方更高层的 COM 上下文；
// * 失败 / 用户取消 → 返回 false，outPath 保持原状；
// * parentHwnd 用主窗口的 HWND（GLFW 的 HWND 通过 glfwGetWin32Window 取），
//   让 dialog 作为 modal child 居中 / 抢焦点。本函数签名 void* 透传，
//   内部 cast 回 HWND 调 Win32 API。
bool ShowSceneFileDialog(bool isSave, void* parentHwnd, std::string& outPath)
{
    const HRESULT hrCo = CoInitializeEx(nullptr,
                                        COINIT_APARTMENTTHREADED
                                        | COINIT_DISABLE_OLE1DDE);
    if (hrCo != S_OK && hrCo != S_FALSE) { return false; }

    bool ok = false;
    IFileDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(
        isSave ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
        nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&pDialog));
    if (SUCCEEDED(hr)) {
        const COMDLG_FILTERSPEC filterSpec[] = {
            { L"Scene Files (*.scene.json)", L"*.scene.json" },
            { L"All Files (*.*)",            L"*.*" },
        };
        pDialog->SetFileTypes(2, filterSpec);
        pDialog->SetFileTypeIndex(1);
        pDialog->SetDefaultExtension(L"scene.json");
        pDialog->SetTitle(isSave ? L"Save Scene As" : L"Open Scene");

        hr = pDialog->Show(static_cast<HWND>(parentHwnd));
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pDialog->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszPath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                if (SUCCEEDED(hr)) {
                    const int u8len = WideCharToMultiByte(
                        CP_UTF8, 0, pszPath, -1,
                        nullptr, 0, nullptr, nullptr);
                    if (u8len > 1) {
                        outPath.resize(static_cast<std::size_t>(u8len - 1));
                        WideCharToMultiByte(
                            CP_UTF8, 0, pszPath, -1,
                            outPath.data(), u8len, nullptr, nullptr);
                        ok = true;
                    }
                    CoTaskMemFree(pszPath);
                }
                pItem->Release();
            }
        }
        pDialog->Release();
    }
    if (hrCo == S_OK) { CoUninitialize(); }
    return ok;
}
