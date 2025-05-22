/**
 * @file GraphicsConstants.h
 * @brief 定义渲染系统中使用的常量
 */

#ifndef ORANGE_GRAPHICS_CONSTANTS_H
#define ORANGE_GRAPHICS_CONSTANTS_H

#include <cstdint>
#include <limits>

namespace Orange
{
    namespace Graphics
    {

        // 常量限制
        constexpr uint32_t MAX_BOUND_VERTEX_BUFFERS = 16;
        constexpr uint32_t MAX_VERTEX_ATTRIBUTES = 16;
        constexpr uint32_t MAX_BOUND_COLOR_ATTACHMENTS = 8;
        constexpr uint32_t MAX_FRAMEBUFFER_LAYERS = 256;
        constexpr uint32_t MAX_VIEWPORTS = 16;
        constexpr uint32_t MAX_SCISSOR_RECTS = 16;
        constexpr uint32_t MAX_DESCRIPTOR_SETS = 4;
        constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 128;
        constexpr uint32_t MAX_TEXTURE_ARRAY_LAYERS = 256;
        constexpr uint32_t MAX_MIP_LEVELS = 16;
        constexpr uint32_t MAX_UNIFORM_BUFFER_RANGE = 65536;
        constexpr uint32_t MAX_STORAGE_BUFFER_RANGE = std::numeric_limits<uint32_t>::max();
        constexpr uint32_t MAX_DYNAMIC_UNIFORM_BUFFERS_PER_PIPELINE_LAYOUT = 8;
        constexpr uint32_t MAX_DYNAMIC_STORAGE_BUFFERS_PER_PIPELINE_LAYOUT = 4;
        constexpr uint32_t MAX_COMBINED_IMAGE_SAMPLERS_PER_SHADER_STAGE = 16;
        constexpr uint32_t MAX_SAMPLED_IMAGES_PER_SHADER_STAGE = 16;
        constexpr uint32_t MAX_STORAGE_IMAGES_PER_SHADER_STAGE = 8;
        constexpr uint32_t MAX_UNIFORM_BUFFERS_PER_SHADER_STAGE = 12;
        constexpr uint32_t MAX_STORAGE_BUFFERS_PER_SHADER_STAGE = 4;
        constexpr uint32_t MAX_INPUT_ATTACHMENTS_PER_SHADER_STAGE = 4;
        constexpr uint32_t MAX_FRAMEBUFFER_WIDTH = 16384;
        constexpr uint32_t MAX_FRAMEBUFFER_HEIGHT = 16384;

        // Vulkan特定常量
        namespace Vulkan
        {
            constexpr uint32_t MAX_MEMORY_TYPES = 32;
            constexpr uint32_t MAX_MEMORY_HEAPS = 16;
            constexpr uint32_t MAX_QUEUE_FAMILIES = 8;
            constexpr uint32_t MAX_DEVICE_GROUP_SIZE = 32;
            constexpr uint32_t MAX_PHYSICAL_DEVICE_NAME_SIZE = 256;
            constexpr uint32_t UUID_SIZE = 16;
            constexpr uint32_t MAX_EXTENSION_NAME_SIZE = 256;
            constexpr uint32_t MAX_DESCRIPTION_SIZE = 256;
            constexpr uint32_t MAX_MEMORY_ALLOCATION_COUNT = 4096;
            constexpr uint32_t MAX_SAMPLER_ALLOCATION_COUNT = 4000;
            constexpr uint32_t MAX_BUFFER_ALLOCATION_COUNT = 4096;
            constexpr uint32_t MAX_DESCRIPTOR_SET_UNIFORMS = 72;
            constexpr uint32_t MAX_PUSH_CONSTANTS_SIZE = 128;
        }

        // DirectX 12特定常量
        namespace D3D12
        {
            constexpr uint32_t MAX_ROOT_PARAMETERS = 64;
            constexpr uint32_t MAX_STATIC_SAMPLERS = 2048;
            constexpr uint32_t MAX_DESCRIPTOR_HEAP_SIZE_TIER_1_CBV_SRV_UAV = 1000000;
            constexpr uint32_t MAX_DESCRIPTOR_HEAP_SIZE_TIER_1_SAMPLER = 2048;
            constexpr uint32_t MAX_DESCRIPTOR_HEAP_SIZE_TIER_2_CBV_SRV_UAV = 1000000;
            constexpr uint32_t MAX_DESCRIPTOR_HEAP_SIZE_TIER_2_SAMPLER = 2048;
            constexpr uint32_t MAX_COMPUTE_ROOT_SIGNATURE_DWORDS = 64;
            constexpr uint32_t MAX_GRAPHICS_ROOT_SIGNATURE_DWORDS = 64;
        }

        // Metal特定常量
        namespace Metal
        {
            constexpr uint32_t MAX_BUFFERS_PER_SHADER_STAGE = 31;
            constexpr uint32_t MAX_TEXTURES_PER_SHADER_STAGE = 128;
            constexpr uint32_t MAX_SAMPLERS_PER_SHADER_STAGE = 16;
            constexpr uint32_t MAX_ARGUMENTS_BUFFER_PER_SHADER_STAGE = 8;
            constexpr uint32_t MAX_ARGUMENT_BUFFERS = 8;
            constexpr uint32_t MAX_RENDER_TARGET_ATTACHMENTS = 8;
        }

        // 公共扩展名称
        namespace Extensions
        {
            // Vulkan扩展
            constexpr const char *VK_KHR_SWAPCHAIN = "VK_KHR_swapchain";
            constexpr const char *VK_KHR_MAINTENANCE1 = "VK_KHR_maintenance1";
            constexpr const char *VK_KHR_DEDICATED_ALLOCATION = "VK_KHR_dedicated_allocation";
            constexpr const char *VK_KHR_GET_MEMORY_REQUIREMENTS2 = "VK_KHR_get_memory_requirements2";
            constexpr const char *VK_KHR_SHADER_DRAW_PARAMETERS = "VK_KHR_shader_draw_parameters";
            constexpr const char *VK_EXT_DEBUG_UTILS = "VK_EXT_debug_utils";
            constexpr const char *VK_EXT_DEBUG_MARKER = "VK_EXT_debug_marker";
            constexpr const char *VK_EXT_DEBUG_REPORT = "VK_EXT_debug_report";
            constexpr const char *VK_KHR_RAY_TRACING_PIPELINE = "VK_KHR_ray_tracing_pipeline";
            constexpr const char *VK_KHR_ACCELERATION_STRUCTURE = "VK_KHR_acceleration_structure";
            constexpr const char *VK_KHR_DEFERRED_HOST_OPERATIONS = "VK_KHR_deferred_host_operations";
            constexpr const char *VK_KHR_PIPELINE_LIBRARY = "VK_KHR_pipeline_library";
            constexpr const char *VK_KHR_BUFFER_DEVICE_ADDRESS = "VK_KHR_buffer_device_address";
            constexpr const char *VK_KHR_SPIRV_1_4 = "VK_KHR_spirv_1_4";
            constexpr const char *VK_EXT_MEMORY_BUDGET = "VK_EXT_memory_budget";
        }

        // 公共层名称
        namespace Layers
        {
            // Vulkan层
            constexpr const char *KHRONOS_VALIDATION = "VK_LAYER_KHRONOS_validation";
            constexpr const char *LUNARG_MONITOR = "VK_LAYER_LUNARG_monitor";
            constexpr const char *RENDERDOC = "VK_LAYER_RENDERDOC_Capture";
        }

        // 调试标记范围
        struct DebugMarkerScope
        {
            const char *name;
            uint32_t color;

            DebugMarkerScope(const char *name, uint32_t color = 0xFFFFFFFF)
                : name(name), color(color) {}
        };

        // 公共内存对齐常量
        constexpr uint32_t TEXTURE_DATA_PITCH_ALIGNMENT = 256;            // 纹理数据行对齐
        constexpr uint32_t CONSTANT_BUFFER_DATA_ALIGNMENT = 256;          // 常量缓冲区数据对齐
        constexpr uint32_t SHADER_VISIBLE_DESCRIPTOR_HEAP_ALIGNMENT = 64; // 着色器可见描述符堆对齐
        constexpr uint32_t RESOURCE_ALLOCATION_ALIGNMENT = 64 * 1024;     // 资源分配对齐
        constexpr uint32_t RAYTRACING_SHADER_TABLE_ALIGNMENT = 64;        // 光线追踪着色器表对齐
        constexpr uint32_t RAYTRACING_SHADER_RECORD_ALIGNMENT = 32;       // 光线追踪着色器记录对齐

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_GRAPHICS_CONSTANTS_H