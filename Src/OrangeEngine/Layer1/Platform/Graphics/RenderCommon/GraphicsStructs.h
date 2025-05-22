/**
 * @file GraphicsStructs.h
 * @brief 定义渲染系统中使用的复杂结构体
 */

#ifndef ORANGE_GRAPHICS_STRUCTS_H
#define ORANGE_GRAPHICS_STRUCTS_H

#include "GraphicsTypes.h"
#include "GraphicsEnums.h"
#include <vector>
#include <string>

namespace Orange
{
    namespace Graphics
    {

        /**
         * @brief 设备特性描述
         */
        struct DeviceFeatures
        {
            bool geometryShader = false;          // 几何着色器
            bool tessellationShader = false;      // 曲面细分着色器
            bool multiViewport = false;           // 多视口
            bool samplerAnisotropy = false;       // 各向异性采样
            bool textureCompressionBC = false;    // BC纹理压缩
            bool textureCompressionETC2 = false;  // ETC2纹理压缩
            bool textureCompressionASTC = false;  // ASTC纹理压缩
            bool textureCubeArray = false;        // 立方体纹理数组
            bool raytracing = false;              // 光线追踪
            bool meshShading = false;             // 网格着色
            bool drawIndirectCount = false;       // 间接绘制计数
            bool depthClipEnable = false;         // 深度裁剪启用
            bool depthBoundsTest = false;         // 深度边界测试
            bool wideLines = false;               // 宽线
            bool fillModeNonSolid = false;        // 非实体填充模式
            bool variableMultisampleRate = false; // 可变多重采样率
            bool inheritedQueries = false;        // 继承查询
            bool logicOp = false;                 // 逻辑操作
        };

        /**
         * @brief 设备属性
         */
        struct DeviceProperties
        {
            std::string deviceName;                             // 设备名称
            std::string driverVersion;                          // 驱动版本
            std::string apiVersion;                             // API版本
            uint32_t vendorId = 0;                              // 供应商ID
            uint32_t deviceId = 0;                              // 设备ID
            uint32_t maxImageDimension1D = 0;                   // 1D纹理最大尺寸
            uint32_t maxImageDimension2D = 0;                   // 2D纹理最大尺寸
            uint32_t maxImageDimension3D = 0;                   // 3D纹理最大尺寸
            uint32_t maxImageDimensionCube = 0;                 // 立方体纹理最大尺寸
            uint32_t maxImageArrayLayers = 0;                   // 纹理数组最大层数
            uint32_t maxTexelBufferElements = 0;                // 纹素缓冲区最大元素
            uint32_t maxUniformBufferRange = 0;                 // 统一缓冲区最大范围
            uint32_t maxStorageBufferRange = 0;                 // 存储缓冲区最大范围
            uint32_t maxPushConstantsSize = 0;                  // 推送常量最大大小
            uint32_t maxMemoryAllocationCount = 0;              // 最大内存分配数
            uint32_t maxSamplerAllocationCount = 0;             // 最大采样器分配数
            uint64_t bufferImageGranularity = 0;                // 缓冲区图像粒度
            uint64_t sparseAddressSpaceSize = 0;                // 稀疏地址空间大小
            uint32_t maxBoundDescriptorSets = 0;                // 最大绑定描述符集
            uint32_t maxPerStageDescriptorSamplers = 0;         // 每阶段最大采样器描述符
            uint32_t maxPerStageDescriptorUniformBuffers = 0;   // 每阶段最大统一缓冲区描述符
            uint32_t maxPerStageDescriptorStorageBuffers = 0;   // 每阶段最大存储缓冲区描述符
            uint32_t maxPerStageDescriptorSampledImages = 0;    // 每阶段最大采样图像描述符
            uint32_t maxPerStageDescriptorStorageImages = 0;    // 每阶段最大存储图像描述符
            uint32_t maxPerStageDescriptorInputAttachments = 0; // 每阶段最大输入附件描述符
            uint32_t maxPerStageResources = 0;                  // 每阶段最大资源
            uint32_t maxFragmentCombinedOutputResources = 0;    // 片段着色器最大组合输出资源
            float maxSamplerLodBias = 0.0f;                     // 采样器最大LOD偏移
            float maxSamplerAnisotropy = 0.0f;                  // 采样器最大各向异性
        };

        /**
         * @brief 设备创建信息
         */
        struct DeviceCreateInfo
        {
            RenderAPI api = RenderAPI::Vulkan;   // 渲染API
            CreateFlag flags = CreateFlag::None; // 创建标志
            DeviceFeatures features;             // 请求的特性
            void *windowHandle = nullptr;        // 窗口句柄
            std::vector<std::string> extensions; // 请求的扩展
            std::vector<std::string> layers;     // 请求的层
        };

        /**
         * @brief 缓冲区创建信息
         */
        struct BufferCreateInfo
        {
            uint64_t size = 0;                    // 缓冲区大小
            BufferType type = BufferType::Vertex; // 缓冲区类型
            bool hostVisible = false;             // 主机可见
            bool hostCoherent = false;            // 主机一致
            bool hostCached = false;              // 主机缓存
            bool deviceLocal = true;              // 设备本地
            bool mapped = false;                  // 映射
            std::string name;                     // 调试名称
        };

        /**
         * @brief 纹理创建信息
         */
        struct TextureCreateInfo
        {
            TextureType type = TextureType::Texture2D;     // 纹理类型
            PixelFormat format = PixelFormat::RGBA8_UNORM; // 像素格式
            Extent3D extent = {1, 1, 1};                   // 纹理尺寸
            uint32_t mipLevels = 1;                        // Mip级别
            uint32_t arrayLayers = 1;                      // 数组层数
            bool cubemap = false;                          // 是否是立方体纹理
            bool generateMipmaps = false;                  // 生成mipmap
            bool renderTarget = false;                     // 是否作为渲染目标
            bool depthStencil = false;                     // 是否是深度模板
            bool sampled = true;                           // 是否可采样
            bool storage = false;                          // 是否可存储
            std::string name;                              // 调试名称
        };

        /**
         * @brief 纹理视图创建信息
         */
        struct TextureViewCreateInfo
        {
            TextureViewType viewType = TextureViewType::View2D; // 视图类型
            PixelFormat format = PixelFormat::UNKNOWN;          // 像素格式，UNKNOWN表示使用纹理格式
            uint32_t baseMipLevel = 0;                          // 基础mip级别
            uint32_t levelCount = 1;                            // mip级别数量
            uint32_t baseArrayLayer = 0;                        // 基础数组层
            uint32_t layerCount = 1;                            // 数组层数量
            std::string name;                                   // 调试名称
        };

        /**
         * @brief 采样器创建信息
         */
        struct SamplerCreateInfo
        {
            TextureFilterMode magFilter = TextureFilterMode::Linear;      // 放大过滤
            TextureFilterMode minFilter = TextureFilterMode::Linear;      // 缩小过滤
            MipmapFilterMode mipmapMode = MipmapFilterMode::Linear;       // Mipmap过滤
            TextureAddressMode addressModeU = TextureAddressMode::Repeat; // U寻址模式
            TextureAddressMode addressModeV = TextureAddressMode::Repeat; // V寻址模式
            TextureAddressMode addressModeW = TextureAddressMode::Repeat; // W寻址模式
            float mipLodBias = 0.0f;                                      // MIP LOD偏移
            bool anisotropyEnable = false;                                // 各向异性启用
            float maxAnisotropy = 1.0f;                                   // 最大各向异性
            bool compareEnable = false;                                   // 比较启用
            CompareOp compareOp = CompareOp::Never;                       // 比较操作
            float minLod = 0.0f;                                          // 最小LOD
            float maxLod = 1000.0f;                                       // 最大LOD
            Color4f borderColor = {0.0f, 0.0f, 0.0f, 0.0f};               // 边框颜色
            std::string name;                                             // 调试名称
        };

        /**
         * @brief 着色器创建信息
         */
        struct ShaderCreateInfo
        {
            ShaderType type = ShaderType::Vertex;            // 着色器类型
            ShaderLanguage language = ShaderLanguage::SPIRV; // 着色器语言
            std::vector<uint8_t> code;                       // 着色器代码
            std::string entryPoint = "main";                 // 入口点
            std::string filePath;                            // 文件路径（用于调试）
            std::string name;                                // 调试名称
        };

        /**
         * @brief 着色器模块信息
         */
        struct ShaderModuleInfo
        {
            ShaderType type;        // 着色器类型
            void *shaderModule;     // 着色器模块句柄
            std::string entryPoint; // 入口点
        };

        /**
         * @brief 顶点输入描述
         */
        struct VertexInputDescription
        {
            std::vector<VertexAttribute> attributes; // 顶点属性
            uint32_t binding = 0;                    // 绑定点
            uint32_t stride = 0;                     // 顶点步长
            bool perInstance = false;                // 是否按实例
        };

        /**
         * @brief 输入装配状态
         */
        struct InputAssemblyState
        {
            PrimitiveTopology topology = PrimitiveTopology::TriangleList; // 图元拓扑
            bool primitiveRestart = false;                                // 图元重启
        };

        /**
         * @brief 光栅化状态
         */
        struct RasterizationState
        {
            bool depthClampEnable = false;                     // 深度钳制启用
            bool rasterizerDiscardEnable = false;              // 光栅化丢弃启用
            PolygonMode polygonMode = PolygonMode::Fill;       // 多边形模式
            CullMode cullMode = CullMode::None;                // 面剔除模式
            FrontFace frontFace = FrontFace::CounterClockwise; // 正面定义
            bool depthBiasEnable = false;                      // 深度偏移启用
            float depthBiasConstantFactor = 0.0f;              // 深度偏移常量因子
            float depthBiasClamp = 0.0f;                       // 深度偏移钳制
            float depthBiasSlopeFactor = 0.0f;                 // 深度偏移斜率因子
            float lineWidth = 1.0f;                            // 线宽
        };

        /**
         * @brief 多重采样状态
         */
        struct MultisampleState
        {
            uint32_t rasterizationSamples = 1;  // 光栅化样本
            bool sampleShadingEnable = false;   // 样本着色启用
            float minSampleShading = 0.0f;      // 最小样本着色
            std::vector<uint32_t> sampleMask;   // 样本掩码
            bool alphaToCoverageEnable = false; // Alpha到覆盖启用
            bool alphaToOneEnable = false;      // Alpha到1启用
        };

        /**
         * @brief 深度模板状态
         */
        struct DepthStencilState
        {
            bool depthTestEnable = true;                // 深度测试启用
            bool depthWriteEnable = true;               // 深度写入启用
            CompareOp depthCompareOp = CompareOp::Less; // 深度比较操作
            bool depthBoundsTestEnable = false;         // 深度边界测试启用
            float minDepthBounds = 0.0f;                // 最小深度边界
            float maxDepthBounds = 1.0f;                // 最大深度边界
            bool stencilTestEnable = false;             // 模板测试启用

            // 正面模板
            struct StencilOpState
            {
                CompareOp compareOp = CompareOp::Always; // 比较操作
                uint32_t reference = 0;                  // 参考值
                uint32_t compareMask = 0xFFFFFFFF;       // 比较掩码
                uint32_t writeMask = 0xFFFFFFFF;         // 写入掩码
            };

            StencilOpState frontStencil; // 正面模板状态
            StencilOpState backStencil;  // 背面模板状态
        };

        /**
         * @brief 颜色混合附件状态
         */
        struct ColorBlendAttachmentState
        {
            bool blendEnable = false;                                               // 混合启用
            BlendFactor srcColorBlendFactor = BlendFactor::One;                     // 源颜色混合因子
            BlendFactor dstColorBlendFactor = BlendFactor::Zero;                    // 目标颜色混合因子
            BlendOp colorBlendOp = BlendOp::Add;                                    // 颜色混合操作
            BlendFactor srcAlphaBlendFactor = BlendFactor::One;                     // 源Alpha混合因子
            BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;                    // 目标Alpha混合因子
            BlendOp alphaBlendOp = BlendOp::Add;                                    // Alpha混合操作
            uint8_t colorWriteMask = static_cast<uint8_t>(ColorComponentFlag::All); // 颜色写入掩码
        };

        /**
         * @brief 颜色混合状态
         */
        struct ColorBlendState
        {
            bool logicOpEnable = false;                         // 逻辑操作启用
            LogicOp logicOp = LogicOp::Copy;                    // 逻辑操作
            std::vector<ColorBlendAttachmentState> attachments; // 附件状态
            float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // 混合常量
        };

        /**
         * @brief 动态状态
         */
        struct DynamicState
        {
            std::vector<uint32_t> dynamicStates; // 动态状态
        };

        /**
         * @brief 描述符绑定
         */
        struct DescriptorBinding
        {
            uint32_t binding = 0;                                // 绑定点
            DescriptorType type = DescriptorType::UniformBuffer; // 描述符类型
            uint32_t count = 1;                                  // 描述符数量
            ShaderStageFlag stageFlags = ShaderStageFlag::All;   // 着色器阶段
        };

        /**
         * @brief 描述符集布局创建信息
         */
        struct DescriptorSetLayoutCreateInfo
        {
            std::vector<DescriptorBinding> bindings; // 绑定
            std::string name;                        // 调试名称
        };

        /**
         * @brief 推送常量范围
         */
        struct PushConstantRange
        {
            ShaderStageFlag stageFlags = ShaderStageFlag::All; // 着色器阶段
            uint32_t offset = 0;                               // 偏移
            uint32_t size = 0;                                 // 大小
        };

        /**
         * @brief 管线布局创建信息
         */
        struct PipelineLayoutCreateInfo
        {
            std::vector<void *> descriptorSetLayouts;          // 描述符集布局
            std::vector<PushConstantRange> pushConstantRanges; // 推送常量范围
            std::string name;                                  // 调试名称
        };

        /**
         * @brief 图形管线创建信息
         */
        struct GraphicsPipelineCreateInfo
        {
            std::vector<ShaderModuleInfo> shaderStages;       // 着色器阶段
            std::vector<VertexInputDescription> vertexInputs; // 顶点输入
            InputAssemblyState inputAssembly;                 // 输入装配
            RasterizationState rasterization;                 // 光栅化
            MultisampleState multisample;                     // 多重采样
            DepthStencilState depthStencil;                   // 深度模板
            ColorBlendState colorBlend;                       // 颜色混合
            DynamicState dynamicState;                        // 动态状态
            void *pipelineLayout = nullptr;                   // 管线布局
            void *renderPass = nullptr;                       // 渲染通道
            uint32_t subpass = 0;                             // 子通道
            std::string name;                                 // 调试名称
        };

        /**
         * @brief 计算管线创建信息
         */
        struct ComputePipelineCreateInfo
        {
            ShaderModuleInfo computeShader; // 计算着色器
            void *pipelineLayout = nullptr; // 管线布局
            std::string name;               // 调试名称
        };

        /**
         * @brief 附件描述
         */
        struct AttachmentDescription
        {
            PixelFormat format = PixelFormat::RGBA8_UNORM;          // 格式
            uint32_t samples = 1;                                   // 样本数
            bool loadOp = true;                                     // 加载操作（true=加载，false=清除）
            bool storeOp = true;                                    // 存储操作（true=存储，false=丢弃）
            bool stencilLoadOp = true;                              // 模板加载操作（true=加载，false=清除）
            bool stencilStoreOp = true;                             // 模板存储操作（true=存储，false=丢弃）
            ResourceState initialLayout = ResourceState::Undefined; // 初始布局
            ResourceState finalLayout = ResourceState::ShaderRead;  // 最终布局
            Color4f clearColor = {0.0f, 0.0f, 0.0f, 1.0f};          // 清除颜色
            float clearDepth = 1.0f;                                // 清除深度
            uint32_t clearStencil = 0;                              // 清除模板
        };

        /**
         * @brief 附件引用
         */
        struct AttachmentReference
        {
            uint32_t attachment = 0;                          // 附件索引
            ResourceState layout = ResourceState::ShaderRead; // 布局
        };

        /**
         * @brief 子通道描述
         */
        struct SubpassDescription
        {
            std::vector<AttachmentReference> inputAttachments;   // 输入附件
            std::vector<AttachmentReference> colorAttachments;   // 颜色附件
            std::vector<AttachmentReference> resolveAttachments; // 解析附件
            AttachmentReference depthStencilAttachment;          // 深度模板附件
            std::vector<uint32_t> preserveAttachments;           // 保留附件
        };

        /**
         * @brief 子通道依赖
         */
        struct SubpassDependency
        {
            uint32_t srcSubpass = 0;                             // 源子通道
            uint32_t dstSubpass = 0;                             // 目标子通道
            ResourceState srcStageMask = ResourceState::Common;  // 源阶段掩码
            ResourceState dstStageMask = ResourceState::Common;  // 目标阶段掩码
            ResourceState srcAccessMask = ResourceState::Common; // 源访问掩码
            ResourceState dstAccessMask = ResourceState::Common; // 目标访问掩码
            bool byRegion = false;                               // 按区域
        };

        /**
         * @brief 渲染通道创建信息
         */
        struct RenderPassCreateInfo
        {
            std::vector<AttachmentDescription> attachments; // 附件
            std::vector<SubpassDescription> subpasses;      // 子通道
            std::vector<SubpassDependency> dependencies;    // 依赖
            std::string name;                               // 调试名称
        };

        /**
         * @brief 帧缓冲创建信息
         */
        struct FramebufferCreateInfo
        {
            void *renderPass = nullptr;      // 渲染通道
            std::vector<void *> attachments; // 附件
            uint32_t width = 0;              // 宽度
            uint32_t height = 0;             // 高度
            uint32_t layers = 1;             // 层数
            std::string name;                // 调试名称
        };

        /**
         * @brief 交换链创建信息
         */
        struct SwapChainCreateInfo
        {
            void *surface = nullptr;                       // 表面
            uint32_t width = 0;                            // 宽度
            uint32_t height = 0;                           // 高度
            PixelFormat format = PixelFormat::RGBA8_UNORM; // 格式
            uint32_t imageCount = 2;                       // 图像数量
            bool vsync = true;                             // 垂直同步
            std::string name;                              // 调试名称
        };

        /**
         * @brief 描述符池创建信息
         */
        struct DescriptorPoolCreateInfo
        {
            uint32_t maxSets = 100;                                     // 最大集数
            std::vector<std::pair<DescriptorType, uint32_t>> poolSizes; // 池大小
            std::string name;                                           // 调试名称
        };

        /**
         * @brief 描述符集分配信息
         */
        struct DescriptorSetAllocateInfo
        {
            void *descriptorPool = nullptr;      // 描述符池
            void *descriptorSetLayout = nullptr; // 描述符集布局
            std::string name;                    // 调试名称
        };

        /**
         * @brief 内存统计信息
         */
        struct GPUMemoryStats
        {
            uint64_t totalMemory = 0;                                // 总内存
            uint64_t usedMemory = 0;                                 // 已用内存
            uint64_t availableMemory = 0;                            // 可用内存
            uint64_t largestBlock = 0;                               // 最大块
            uint32_t allocationCount = 0;                            // 分配数量
            uint64_t deviceLocalMemory = 0;                          // 设备本地内存
            uint64_t hostVisibleMemory = 0;                          // 主机可见内存
            std::vector<std::pair<std::string, uint64_t>> typeUsage; // 类型使用情况
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_GRAPHICS_STRUCTS_H