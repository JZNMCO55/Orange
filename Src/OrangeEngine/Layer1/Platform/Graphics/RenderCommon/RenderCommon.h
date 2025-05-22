/**
 * @file RenderCommon.h
 * @brief 渲染系统通用定义
 */

#ifndef ORANGE_RENDER_COMMON_H
#define ORANGE_RENDER_COMMON_H

#include <stdint.h>

namespace Orange
{
    namespace Graphics
    {
/**
 * @brief 定义枚举标志位运算符
 */
#define DEFINE_ENUM_FLAG_OPERATORS(T, TType) \
    inline TType operator|(T a, T b) { return static_cast<TType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); } \
    inline TType operator&(T a, T b) { return static_cast<TType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); } \
    inline TType operator^(T a, T b) { return static_cast<TType>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b)); } \
    inline TType operator~(T a) { return static_cast<TType>(~static_cast<uint32_t>(a)); } \
    inline TType& operator|=(TType& a, T b) { return a = static_cast<TType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); } \
    inline TType& operator&=(TType& a, T b) { return a = static_cast<TType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); } \
    inline TType& operator^=(TType& a, T b) { return a = static_cast<TType>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b)); } 

        /**
         * @brief 渲染后端类型
         */
        enum class RenderBackend
        {
            None,      ///< 无后端
            Vulkan,    ///< Vulkan后端
            DirectX12, ///< DirectX 12后端
            Metal      ///< Metal后端
        };

        /**
         * @brief 设备特性标志位
         */
        enum class DeviceFeatureFlagBits : uint32_t
        {
            None = 0,
            Geometry = 0x00000001,                  ///< 几何着色器
            Tessellation = 0x00000002,              ///< 曲面细分着色器
            ComputeShader = 0x00000004,             ///< 计算着色器
            MultiViewport = 0x00000008,             ///< 多视口
            TextureCompressionBC = 0x00000010,      ///< BC压缩纹理格式
            TextureCompressionETC2 = 0x00000020,    ///< ETC2压缩纹理格式
            TextureCompressionASTC = 0x00000040,    ///< ASTC压缩纹理格式
            WideLines = 0x00000080,                 ///< 宽线
            FillModeNonSolid = 0x00000100,          ///< 非实体填充模式
            DepthClamp = 0x00000200,                ///< 深度钳制
            DepthBounds = 0x00000400,               ///< 深度边界测试
            AlphaToOne = 0x00000800,                ///< Alpha to One
            MultiDrawIndirect = 0x00001000,         ///< 多重间接绘制
            SamplerAnisotropy = 0x00002000,         ///< 各向异性过滤
            TextureCubeArray = 0x00004000,          ///< 立方体纹理数组
            IndependentBlend = 0x00008000,          ///< 独立混合
            GeometryShader = 0x00010000,            ///< 几何着色器
            TessellationShader = 0x00020000,        ///< 曲面细分着色器
            SampleRateShading = 0x00040000,         ///< 采样率着色
            DualSrcBlend = 0x00080000,              ///< 双源混合
            LogicOp = 0x00100000,                   ///< 逻辑操作
            DrawIndirectFirstInstance = 0x00200000, ///< 间接绘制第一个实例
            DepthBiasClamp = 0x00400000,            ///< 深度偏移钳制
            FullDrawIndexUint32 = 0x00800000,       ///< 完整的 uint32 索引
            ImageCubeArray = 0x01000000,            ///< 立方体图像数组
            PipelineStatisticsQuery = 0x02000000,   ///< 管线统计查询
            SparseBinding = 0x04000000,             ///< 稀疏绑定
            SparseResidencyBuffer = 0x08000000,     ///< 稀疏常驻缓冲区
            SparseResidencyImage2D = 0x10000000,    ///< 稀疏常驻2D图像
            SparseResidencyImage3D = 0x20000000     ///< 稀疏常驻3D图像
        };
        // 定义类型别名
        using DeviceFeatureFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(DeviceFeatureFlagBits, DeviceFeatureFlags);

        /**
         * @brief GPU属性
         */
        struct GPUProperties
        {
            char deviceName[256];                         ///< 设备名称
            char driverVersion[256];                      ///< 驱动版本
            char vendorName[256];                         ///< 供应商名称
            uint32_t vendorId;                            ///< 供应商ID
            uint32_t deviceId;                            ///< 设备ID
            uint32_t deviceType;                          ///< 设备类型（0=其他，1=集成，2=离散，3=虚拟，4=CPU）
            uint64_t deviceLocalMemory;                   ///< 设备本地内存大小
            uint64_t hostVisibleMemory;                   ///< 主机可见内存大小
            DeviceFeatureFlags supportedFeatures;         ///< 支持的特性
            bool isDiscreteGPU;                           ///< 是否为独立GPU
            float timestampPeriod;                        ///< 时间戳周期（纳秒）
            uint32_t maxImageDimension2D;                 ///< 最大2D图像尺寸
            uint32_t maxImageArrayLayers;                 ///< 最大图像数组层数
            uint32_t maxTexelBufferElements;              ///< 最大纹素缓冲区元素
            uint32_t maxUniformBufferRange;               ///< 最大统一缓冲区范围
            uint32_t maxStorageBufferRange;               ///< 最大存储缓冲区范围
            uint32_t maxPushConstantsSize;                ///< 最大推送常量大小
            uint32_t maxMemoryAllocationCount;            ///< 最大内存分配数量
            uint32_t maxSamplerAllocationCount;           ///< 最大采样器分配数量
            uint32_t maxBoundDescriptorSets;              ///< 最大绑定描述符集数量
            uint32_t maxPerStageDescriptorSamplers;       ///< 每个阶段最大描述符采样器数量
            uint32_t maxPerStageDescriptorUniformBuffers; ///< 每个阶段最大描述符统一缓冲区数量
        };

        /**
         * @brief 队列标志
         */
        enum class QueueFlagBits : uint32_t
        {
            None = 0,
            Graphics = 0x00000001,
            Compute = 0x00000002,
            Transfer = 0x00000004,
            SparseBinding = 0x00000008,
            Protected = 0x00000010,
            All = Graphics | Compute | Transfer | SparseBinding | Protected
        };
        // 定义类型别名
        using QueueFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(QueueFlagBits, QueueFlags);

        /**
         * @brief 依赖标志
         */
        enum class DependencyFlagBits : uint32_t
        {
            None = 0,
            ByRegion = 0x00000001,
            DeviceGroup = 0x00000004,
            ViewLocal = 0x00000002
        };
        // 定义类型别名
        using DependencyFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(DependencyFlagBits, DependencyFlags);

        /**
         * @brief 管线阶段标志
         */
        enum class PipelineStageFlagBits : uint32_t
        {
            None = 0,
            TopOfPipe = 0x00000001,
            DrawIndirect = 0x00000002,
            VertexInput = 0x00000004,
            VertexShader = 0x00000008,
            TessellationControlShader = 0x00000010,
            TessellationEvaluationShader = 0x00000020,
            GeometryShader = 0x00000040,
            FragmentShader = 0x00000080,
            EarlyFragmentTests = 0x00000100,
            LateFragmentTests = 0x00000200,
            ColorAttachmentOutput = 0x00000400,
            ComputeShader = 0x00000800,
            Transfer = 0x00001000,
            BottomOfPipe = 0x00002000,
            Host = 0x00004000,
            AllGraphics = 0x00008000,
            AllCommands = 0x00010000
        };
        // 定义类型别名
        using PipelineStageFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(PipelineStageFlagBits, PipelineStageFlags);

        /**
         * @brief 访问标志
         */
        enum class AccessFlagBits : uint32_t
        {
            None = 0,
            IndirectCommandRead = 0x00000001,
            IndexRead = 0x00000002,
            VertexAttributeRead = 0x00000004,
            UniformRead = 0x00000008,
            InputAttachmentRead = 0x00000010,
            ShaderRead = 0x00000020,
            ShaderWrite = 0x00000040,
            ColorAttachmentRead = 0x00000080,
            ColorAttachmentWrite = 0x00000100,
            DepthStencilAttachmentRead = 0x00000200,
            DepthStencilAttachmentWrite = 0x00000400,
            TransferRead = 0x00000800,
            TransferWrite = 0x00001000,
            HostRead = 0x00002000,
            HostWrite = 0x00004000,
            MemoryRead = 0x00008000,
            MemoryWrite = 0x00010000
        };
        // 定义类型别名
        using AccessFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(AccessFlagBits, AccessFlags);

        /**
         * @brief 图像布局
         */
        enum class ImageLayout
        {
            Undefined = 0,
            General = 1,
            ColorAttachmentOptimal = 2,
            DepthStencilAttachmentOptimal = 3,
            DepthStencilReadOnlyOptimal = 4,
            ShaderReadOnlyOptimal = 5,
            TransferSrcOptimal = 6,
            TransferDstOptimal = 7,
            Preinitialized = 8,
            PresentSrc = 1000001002
        };

        /**
         * @brief 纹理子资源范围
         */
        struct TextureSubresourceRange
        {
            uint32_t baseMipLevel = 0;   ///< 基础Mip级别
            uint32_t levelCount = 1;     ///< Mip级别数量
            uint32_t baseArrayLayer = 0; ///< 基础数组层
            uint32_t layerCount = 1;     ///< 数组层数量
        };

        /**
         * @brief 缓冲区复制区域
         */
        struct BufferCopyRegion
        {
            uint64_t srcOffset = 0; ///< 源偏移
            uint64_t dstOffset = 0; ///< 目标偏移
            uint64_t size = 0;      ///< 大小
        };

        /**
         * @brief 纹理复制区域
         */
        struct TextureCopyRegion
        {
            uint32_t srcMipLevel = 0;       ///< 源Mip级别
            uint32_t srcBaseArrayLayer = 0; ///< 源基础数组层
            uint32_t srcLayerCount = 1;     ///< 源数组层数量
            int32_t srcX = 0;               ///< 源X坐标
            int32_t srcY = 0;               ///< 源Y坐标
            int32_t srcZ = 0;               ///< 源Z坐标
            uint32_t dstMipLevel = 0;       ///< 目标Mip级别
            uint32_t dstBaseArrayLayer = 0; ///< 目标基础数组层
            uint32_t dstLayerCount = 1;     ///< 目标数组层数量
            int32_t dstX = 0;               ///< 目标X坐标
            int32_t dstY = 0;               ///< 目标Y坐标
            int32_t dstZ = 0;               ///< 目标Z坐标
            uint32_t width = 0;             ///< 宽度
            uint32_t height = 0;            ///< 高度
            uint32_t depth = 1;             ///< 深度
        };

        /**
         * @brief 缓冲区纹理复制区域
         */
        struct BufferTextureCopyRegion
        {
            uint64_t bufferOffset = 0;      ///< 缓冲区偏移
            uint32_t bufferRowLength = 0;   ///< 缓冲区行长度
            uint32_t bufferImageHeight = 0; ///< 缓冲区图像高度
            uint32_t mipLevel = 0;          ///< Mip级别
            uint32_t baseArrayLayer = 0;    ///< 基础数组层
            uint32_t layerCount = 1;        ///< 数组层数量
            int32_t imageOffsetX = 0;       ///< 图像X偏移
            int32_t imageOffsetY = 0;       ///< 图像Y偏移
            int32_t imageOffsetZ = 0;       ///< 图像Z偏移
            uint32_t imageExtentWidth = 0;  ///< 图像宽度
            uint32_t imageExtentHeight = 0; ///< 图像高度
            uint32_t imageExtentDepth = 1;  ///< 图像深度
        };

        /**
         * @brief 纹理解析区域
         */
        struct TextureResolveRegion
        {
            uint32_t srcMipLevel = 0;       ///< 源Mip级别
            uint32_t srcBaseArrayLayer = 0; ///< 源基础数组层
            uint32_t srcLayerCount = 1;     ///< 源数组层数量
            int32_t srcX = 0;               ///< 源X坐标
            int32_t srcY = 0;               ///< 源Y坐标
            int32_t srcZ = 0;               ///< 源Z坐标
            uint32_t dstMipLevel = 0;       ///< 目标Mip级别
            uint32_t dstBaseArrayLayer = 0; ///< 目标基础数组层
            uint32_t dstLayerCount = 1;     ///< 目标数组层数量
            int32_t dstX = 0;               ///< 目标X坐标
            int32_t dstY = 0;               ///< 目标Y坐标
            int32_t dstZ = 0;               ///< 目标Z坐标
            uint32_t width = 0;             ///< 宽度
            uint32_t height = 0;            ///< 高度
            uint32_t depth = 1;             ///< 深度
        };

        /**
         * @brief 索引类型
         */
        enum class IndexType
        {
            Uint16, ///< 16位无符号整数
            Uint32  ///< 32位无符号整数
        };

        /**
         * @brief 模板面标志
         */
        enum class StencilFaceFlagBits : uint32_t
        {
            None = 0,
            Front = 0x00000001,
            Back = 0x00000002,
            FrontAndBack = 0x00000003
        };
        // 定义类型别名
        using StencilFaceFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(StencilFaceFlagBits, StencilFaceFlags);

        /**
         * @brief 视口
         */
        struct Viewport
        {
            float x = 0.0f;        ///< X坐标
            float y = 0.0f;        ///< Y坐标
            float width = 0.0f;    ///< 宽度
            float height = 0.0f;   ///< 高度
            float minDepth = 0.0f; ///< 最小深度
            float maxDepth = 1.0f; ///< 最大深度
        };

        /**
         * @brief 矩形
         */
        struct Rect2D
        {
            int32_t x = 0;       ///< X坐标
            int32_t y = 0;       ///< Y坐标
            uint32_t width = 0;  ///< 宽度
            uint32_t height = 0; ///< 高度
        };

        /**
         * @brief 像素格式
         */
        enum class PixelFormat
        {
            Undefined = 0,
            R8Unorm = 1,
            R8Snorm = 2,
            R8Uint = 3,
            R8Sint = 4,
            R16Unorm = 5,
            R16Snorm = 6,
            R16Uint = 7,
            R16Sint = 8,
            R16Float = 9,
            RG8Unorm = 10,
            RG8Snorm = 11,
            RG8Uint = 12,
            RG8Sint = 13,
            R32Uint = 14,
            R32Sint = 15,
            R32Float = 16,
            RG16Unorm = 17,
            RG16Snorm = 18,
            RG16Uint = 19,
            RG16Sint = 20,
            RG16Float = 21,
            RGB8Unorm = 22,
            RGB8Snorm = 23,
            RGB8Uint = 24,
            RGB8Sint = 25,
            BGR8Unorm = 26,
            BGRA8Unorm = 27,
            RGBA8Unorm = 28,
            RGBA8Snorm = 29,
            RGBA8Uint = 30,
            RGBA8Sint = 31,
            RGB10A2Unorm = 32,
            RG32Uint = 33,
            RG32Sint = 34,
            RG32Float = 35,
            RGB32Uint = 36,
            RGB32Sint = 37,
            RGB32Float = 38,
            RGBA16Unorm = 39,
            RGBA16Snorm = 40,
            RGBA16Uint = 41,
            RGBA16Sint = 42,
            RGBA16Float = 43,
            RGBA32Uint = 44,
            RGBA32Sint = 45,
            RGBA32Float = 46,
            D16Unorm = 47,
            D32Float = 48,
            S8Uint = 49,
            D24UnormS8Uint = 50,
            D32FloatS8Uint = 51,
            BC1RGBUnorm = 52,
            BC1RGBAUnorm = 53,
            BC2Unorm = 54,
            BC3Unorm = 55,
            BC4Unorm = 56,
            BC5Unorm = 57,
            BC6HUfloat = 58,
            BC7Unorm = 59
        };

        /**
         * @brief 清除值
         */
        struct ClearValue
        {
            union
            {
                float color[4]; ///< 颜色值 (RGBA)
                struct
                {
                    float depth;      ///< 深度值
                    uint32_t stencil; ///< 模板值
                } depthStencil;
            };
        };

        /**
         * @brief 着色器阶段标志
         */
        enum class ShaderStageFlagBits : uint32_t
        {
            None = 0,
            Vertex = 0x00000001,
            TessellationControl = 0x00000002,
            TessellationEvaluation = 0x00000004,
            Geometry = 0x00000008,
            Fragment = 0x00000010,
            Compute = 0x00000020,
            AllGraphics = 0x0000001F,
            All = 0x7FFFFFFF
        };
        // 定义类型别名
        using ShaderStageFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(ShaderStageFlagBits, ShaderStageFlags);

        /**
         * @brief 缓冲区用途标志
         */
        enum class BufferUsageFlagBits : uint32_t
        {
            None = 0,
            TransferSrc = 0x00000001,
            TransferDst = 0x00000002,
            UniformTexelBuffer = 0x00000004,
            StorageTexelBuffer = 0x00000008,
            UniformBuffer = 0x00000010,
            StorageBuffer = 0x00000020,
            IndexBuffer = 0x00000040,
            VertexBuffer = 0x00000080,
            IndirectBuffer = 0x00000100,
            ShaderDeviceAddress = 0x00020000
        };
        // 定义类型别名
        using BufferUsageFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(BufferUsageFlagBits, BufferUsageFlags);

        /**
         * @brief 内存属性标志
         */
        enum class MemoryPropertyFlagBits : uint32_t
        {
            None = 0,
            DeviceLocal = 0x00000001,
            HostVisible = 0x00000002,
            HostCoherent = 0x00000004,
            HostCached = 0x00000008,
            LazilyAllocated = 0x00000010,
            Protected = 0x00000020
        };
        // 定义类型别名
        using MemoryPropertyFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(MemoryPropertyFlagBits, MemoryPropertyFlags);

        /**
         * @brief 共享模式
         */
        enum class SharingMode
        {
            Exclusive = 0,
            Concurrent = 1
        };

        /**
         * @brief 资源绑定描述
         */
        struct ResourceBindingDesc
        {
            uint32_t binding;            ///< 绑定点
            ShaderStageFlags stageFlags; ///< 着色器阶段标志
            uint32_t descriptorCount;    ///< 描述符数量
        };

        /**
         * @brief 缓冲区视图描述
         */
        struct BufferViewDesc
        {
            uint64_t offset;    ///< 偏移
            uint64_t range;     ///< 范围
            PixelFormat format; ///< 格式
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_RENDER_COMMON_H