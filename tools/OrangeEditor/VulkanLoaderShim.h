#ifndef ORANGE_EDITOR_VULKAN_LOADER_SHIM_H
#define ORANGE_EDITOR_VULKAN_LOADER_SHIM_H

// Vulkan loader 中转层 —— 编辑器侧需要 ImGui Vulkan backend、Windows COM
// 文件对话框、ImGui descriptor pool 的 Vulkan / Win32 直接调用，但都属于
// "胶水"性质代码，不该把 main.cpp 撑成多个责任。本头声明这一组工具，
// 具体实现走 VulkanLoaderShim.cpp 单独 TU。
//
// 设计要点：
//   * Vulkan 调用走 OrangeRender 内 volk 已加载的 vkGetInstanceProcAddr，
//     不依赖静态链 vulkan-1.lib（与 OrangeRender 内部 loader 状态保持一致）；
//   * ImGui Vulkan backend 在 NO_PROTOTYPES 下需要 ImGui_ImplVulkan_LoadFunctions
//     拿到 ~30 个 vk* 入口；ImguiVulkanLoader 是喂给它的 callback；
//   * ImguiVulkanLoader 内置一个 KHR-trampoline 陷阱补丁，详见实现内注释；
//   * Windows COM 文件对话框（IFileDialog）封装一份对外 UTF-8 接口；
//   * HWND 在公共面以 void* 表示，避免把 <windows.h> 拽进任何包含本头的 TU。

#include <vulkan/vulkan.h>

#include <string>

// ImGui_ImplVulkan_LoadFunctions 的 user-data 载体。pfn 取自
// `Interop::GetVulkanGetInstanceProcAddr()`，VkInstance / VkDevice 取自
// `Interop::GetVulkanDeviceHandles(...)`；pfnGetDeviceProcAddr 与 vkDevice
// 是 KHR-trampoline 补丁的二级回退入口，可为 nullptr / VK_NULL_HANDLE，
// 此时 loader 只走 vkGetInstanceProcAddr 路径。
struct ImguiVulkanLoaderCtx
{
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   pfnGetDeviceProcAddr;
    VkInstance                vkInstance;
    VkDevice                  vkDevice;
};

// ImGui_ImplVulkan_LoadFunctions 的 loader_func 签名 ——
// `PFN_vkVoidFunction (*)(const char* name, void* user_data)`。userData 强
// 转为 `const ImguiVulkanLoaderCtx*`。
PFN_vkVoidFunction ImguiVulkanLoader(const char* funcName, void* userData);

// 给 ImGui Vulkan backend 用的 descriptor pool（1000 个
// CombinedImageSampler，FREE_DESCRIPTOR_SET 模式）。失败返回
// VK_NULL_HANDLE 并 log；不抛异常。
VkDescriptorPool MakeImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                         VkInstance                instance,
                                         VkDevice                  device);

// MakeImguiDescriptorPool 的对应关停。pool == VK_NULL_HANDLE → no-op。
void DestroyImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                VkInstance                instance,
                                VkDevice                  device,
                                VkDescriptorPool          pool);

// Windows native file dialog（IFileOpenDialog / IFileSaveDialog）包装。
// 过滤器固定 `.scene.json`；路径以 UTF-8 写回 outPath；用户取消 / 失败
// → 返回 false（outPath 保持原状）。parentHwnd 由调用方从 GLFW 主窗口
// 取（glfwGetWin32Window），以 void* 透传，避免本头拉 <windows.h>。
bool ShowSceneFileDialog(bool isSave, void* parentHwnd, std::string& outPath);

// 同款 dialog 但过滤 `.scene.manifest.json` —— v0.6 c6 SaveSplit /
// LoadSplit 多文件落盘的 manifest 文件入口。manifest 路径决定 base
// directory，per-layer .scene.json 与 manifest 同目录。
bool ShowManifestFileDialog(bool isSave, void* parentHwnd, std::string& outPath);

// v1.1 T2：DCC 资产 import 入口对话框。过滤 .obj / .gltf / .glb / .png /
// .jpg / .jpeg / .tga / .hdr 一组，模式固定 open（非 save）。outPath 写
// 用户选中的源文件绝对路径，后续由 ImportDispatcher::Dispatch 路由。
bool ShowImportFileDialog(void* parentHwnd, std::string& outPath);

#endif // ORANGE_EDITOR_VULKAN_LOADER_SHIM_H
