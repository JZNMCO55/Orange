#include "EditorWindowIcon.h"

#include "EditorWindowIconData.h"

#include <orange/engine/core/Log.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <vector>

namespace OrangeEditorBranding
{

    void ApplyEditorWindowIcons(GLFWwindow* window)
    {
        if (window == nullptr)
        {
            return;
        }

        // GLFWimage::pixels 类型是 `unsigned char*`（非 const），但 GLFW 文档
        // 明确说仅读不写。这里 const_cast 把 codegen 的 constexpr 数据指针递
        // 进去——只读用法安全。GLFW 内部会在 glfwSetWindowIcon 调用期间把
        // 像素拷贝到平台 native handle（Win32 上转 HICON），返回后不再持有
        // 指针，因此 constexpr 静态存储期与调用窗口足够。
        std::vector<GLFWimage> images;
        images.reserve(kEditorWindowIconCount);
        for (std::size_t i = 0; i < kEditorWindowIconCount; ++i)
        {
            const auto& src = kEditorWindowIcons[i];
            GLFWimage   img;
            img.width  = src.width;
            img.height = src.height;
            img.pixels = const_cast<unsigned char*>(src.pixels);
            images.push_back(img);
        }

        glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());
        ORANGE_LOG_INFO("[OrangeEditor] applied window icon ({} sizes)",
                        images.size());
    }

} // namespace OrangeEditorBranding
