/**
 * @file GraphicsEnums.h
 * @brief 定义渲染系统中使用的枚举类型
 */

#ifndef ORANGE_GRAPHICS_ENUMS_H
#define ORANGE_GRAPHICS_ENUMS_H

#include <cstdint>

namespace Orange
{
    namespace Graphics
    {

        /**
         * @brief 支持的图形API类型
         */
        enum class RenderAPI : uint8_t
        {
            Vulkan, ///< Vulkan图形API
            D3D12,  ///< DirectX 12图形API
            Metal,  ///< Metal图形API
            OpenGL, ///< OpenGL图形API
            Unknown ///< 未知或不支持的API
        };

        /**
         * @brief 像素格式枚举
         */
        enum class PixelFormat : uint8_t
        {
            // 8位格式
            R8_UNORM,
            R8_SNORM,
            R8_UINT,
            R8_SINT,

            // 16位格式
            R16_UNORM,
            R16_SNORM,
            R16_UINT,
            R16_SINT,
            R16_FLOAT,
            RG8_UNORM,
            RG8_SNORM,
            RG8_UINT,
            RG8_SINT,

            // 32位格式
            R32_UINT,
            R32_SINT,
            R32_FLOAT,
            RG16_UNORM,
            RG16_SNORM,
            RG16_UINT,
            RG16_SINT,
            RG16_FLOAT,
            RGB8_UNORM,
            RGBA8_UNORM,
            RGBA8_SNORM,
            RGBA8_UINT,
            RGBA8_SINT,
            RGBA8_SRGB,
            BGRA8_UNORM,
            BGRA8_SRGB,

            // 64位格式
            RG32_UINT,
            RG32_SINT,
            RG32_FLOAT,
            RGBA16_UNORM,
            RGBA16_SNORM,
            RGBA16_UINT,
            RGBA16_SINT,
            RGBA16_FLOAT,

            RGB32_FLOAT,

            // 128位格式
            RGBA32_UINT,
            RGBA32_SINT,
            RGBA32_FLOAT,

            // 深度/模板格式
            D16_UNORM,
            D24_UNORM_S8_UINT,
            D32_FLOAT,
            D32_FLOAT_S8_UINT,

            // 压缩格式
            BC1_RGB_UNORM,
            BC1_RGB_SRGB,
            BC1_RGBA_UNORM,
            BC1_RGBA_SRGB,
            BC2_UNORM,
            BC2_SRGB,
            BC3_UNORM,
            BC3_SRGB,
            BC4_UNORM,
            BC4_SNORM,
            BC5_UNORM,
            BC5_SNORM,
            BC6H_UF16,
            BC6H_SF16,
            BC7_UNORM,
            BC7_SRGB,

            // 特殊格式
            UNKNOWN,
            PRESENT // 用于交换链的特殊格式
        };

        /**
         * @brief 缓冲区类型
         */
        enum class BufferType : uint8_t
        {
            Vertex,   ///< 顶点缓冲区
            Index,    ///< 索引缓冲区
            Uniform,  ///< 统一缓冲区
            Storage,  ///< 存储缓冲区
            Indirect, ///< 间接绘制缓冲区
            Staging   ///< 暂存缓冲区(用于传输)
        };

        /**
         * @brief 纹理类型
         */
        enum class TextureType : uint8_t
        {
            Texture1D,       ///< 1D纹理
            Texture2D,       ///< 2D纹理
            Texture3D,       ///< 3D纹理
            TextureCube,     ///< 立方体纹理
            Texture1DArray,  ///< 1D纹理数组
            Texture2DArray,  ///< 2D纹理数组
            TextureCubeArray ///< 立方体纹理数组
        };

        /**
         * @brief 纹理视图类型
         */
        enum class TextureViewType : uint8_t
        {
            View1D,       ///< 1D纹理视图
            View2D,       ///< 2D纹理视图
            View3D,       ///< 3D纹理视图
            ViewCube,     ///< 立方体纹理视图
            View1DArray,  ///< 1D纹理数组视图
            View2DArray,  ///< 2D纹理数组视图
            ViewCubeArray ///< 立方体纹理数组视图
        };

        /**
         * @brief 纹理寻址模式
         */
        enum class TextureAddressMode : uint8_t
        {
            Repeat,         ///< 重复纹理
            MirroredRepeat, ///< 镜像重复
            ClampToEdge,    ///< 钳制到边缘
            ClampToBorder,  ///< 钳制到边框
            MirrorOnce      ///< 镜像一次
        };

        /**
         * @brief 纹理过滤模式
         */
        enum class TextureFilterMode : uint8_t
        {
            Nearest, ///< 最近点过滤
            Linear,  ///< 线性过滤
            Cubic    ///< 三次方过滤
        };

        /**
         * @brief Mipmap过滤模式
         */
        enum class MipmapFilterMode : uint8_t
        {
            Nearest, ///< 最近点Mipmap
            Linear   ///< 线性Mipmap
        };

        /**
         * @brief 比较操作
         */
        enum class CompareOp : uint8_t
        {
            Never,          ///< 永不通过
            Less,           ///< 小于时通过
            Equal,          ///< 等于时通过
            LessOrEqual,    ///< 小于等于时通过
            Greater,        ///< 大于时通过
            NotEqual,       ///< 不等于时通过
            GreaterOrEqual, ///< 大于等于时通过
            Always          ///< 总是通过
        };

        /**
         * @brief 过滤组合模式
         */
        enum class SamplerFilterMode : uint8_t
        {
            MinMagMipNearest,
            MinMagNearestMipLinear,
            MinNearestMagLinearMipNearest,
            MinNearestMagMipLinear,
            MinLinearMagMipNearest,
            MinLinearMagNearestMipLinear,
            MinMagLinearMipNearest,
            MinMagMipLinear,
            Anisotropic
        };

        /**
         * @brief 渲染管线类型
         */
        enum class PipelineType : uint8_t
        {
            Graphics,  ///< 图形管线
            Compute,   ///< 计算管线
            RayTracing ///< 光线追踪管线
        };

        /**
         * @brief 图元拓扑类型
         */
        enum class PrimitiveTopology : uint8_t
        {
            PointList,     ///< 点列表
            LineList,      ///< 线列表
            LineStrip,     ///< 线带
            TriangleList,  ///< 三角形列表
            TriangleStrip, ///< 三角形带
            TriangleFan,   ///< 三角形扇
            PatchList      ///< 补丁列表(用于曲面细分)
        };

        /**
         * @brief 多边形模式
         */
        enum class PolygonMode : uint8_t
        {
            Fill, ///< 填充
            Line, ///< 线框
            Point ///< 点
        };

        /**
         * @brief 正面定义
         */
        enum class FrontFace : uint8_t
        {
            CounterClockwise, ///< 逆时针
            Clockwise         ///< 顺时针
        };

        /**
         * @brief 面剔除模式
         */
        enum class CullMode : uint8_t
        {
            None,  ///< 不剔除
            Front, ///< 剔除正面
            Back,  ///< 剔除背面
            All    ///< 剔除所有面
        };

        /**
         * @brief 逻辑操作
         */
        enum class LogicOp : uint8_t
        {
            Clear,        ///< 0
            Set,          ///< 1
            Copy,         ///< s
            CopyInverted, ///< ~s
            NoOp,         ///< d
            Invert,       ///< ~d
            And,          ///< s & d
            Nand,         ///< ~(s & d)
            Or,           ///< s | d
            Nor,          ///< ~(s | d)
            Xor,          ///< s ^ d
            Equiv,        ///< ~(s ^ d)
            AndReverse,   ///< s & ~d
            AndInverted,  ///< ~s & d
            OrReverse,    ///< s | ~d
            OrInverted    ///< ~s | d
        };

        /**
         * @brief 混合因子
         */
        enum class BlendFactor : uint8_t
        {
            Zero,                  ///< 0
            One,                   ///< 1
            SrcColor,              ///< src color
            OneMinusSrcColor,      ///< 1 - src color
            DstColor,              ///< dst color
            OneMinusDstColor,      ///< 1 - dst color
            SrcAlpha,              ///< src alpha
            OneMinusSrcAlpha,      ///< 1 - src alpha
            DstAlpha,              ///< dst alpha
            OneMinusDstAlpha,      ///< 1 - dst alpha
            ConstantColor,         ///< constant color
            OneMinusConstantColor, ///< 1 - constant color
            ConstantAlpha,         ///< constant alpha
            OneMinusConstantAlpha, ///< 1 - constant alpha
            SrcAlphaSaturate       ///< src alpha saturate
        };

        /**
         * @brief 混合操作
         */
        enum class BlendOp : uint8_t
        {
            Add,             ///< src + dst
            Subtract,        ///< src - dst
            ReverseSubtract, ///< dst - src
            Min,             ///< min(src, dst)
            Max              ///< max(src, dst)
        };

        /**
         * @brief 颜色组件掩码
         */
        enum class ColorComponentFlag : uint8_t
        {
            R = 0x01,           ///< R通道
            G = 0x02,           ///< G通道
            B = 0x04,           ///< B通道
            A = 0x08,           ///< A通道
            All = R | G | B | A ///< 所有通道
        };

        /**
         * @brief 资源状态/屏障
         */
        enum class ResourceState : uint16_t
        {
            Unknown, ///< 未知状态
            // 通用状态
            Common, ///< 通用状态
            // 着色器访问
            ShaderRead,      ///< 着色器读取
            ShaderWrite,     ///< 着色器写入
            ShaderReadWrite, ///< 着色器读写
            // 渲染目标
            RenderTarget,      ///< 渲染目标
            DepthStencilRead,  ///< 深度模板只读
            DepthStencilWrite, ///< 深度模板写入
            // 资源转移
            CopySource, ///< 拷贝源
            CopyDest,   ///< 拷贝目标
            // 呈现
            Present, ///< 呈现
            // 特殊状态
            IndirectArgument, ///< 间接参数
            IndexBuffer,      ///< 索引缓冲区
            VertexBuffer,     ///< 顶点缓冲区
            ConstantBuffer,   ///< 常量缓冲区
            // 光线追踪
            AccelerationStructure, ///< 加速结构
            // 主机访问
            HostRead,  ///< 主机读取
            HostWrite, ///< 主机写入
            Undefined
        };

        /**
         * @brief 着色器类型
         */
        enum class ShaderType : uint8_t
        {
            Vertex,         ///< 顶点着色器
            TessControl,    ///< 曲面细分控制着色器
            TessEvaluation, ///< 曲面细分评估着色器
            Geometry,       ///< 几何着色器
            Fragment,       ///< 片段着色器
            Compute,        ///< 计算着色器
            RayGen,         ///< 光线生成着色器
            AnyHit,         ///< 任意命中着色器
            ClosestHit,     ///< 最近命中着色器
            Miss,           ///< 未命中着色器
            Intersection,   ///< 相交着色器
            Callable,       ///< 可调用着色器
            Task,           ///< 任务着色器
            Mesh            ///< 网格着色器
        };

        /**
         * @brief 着色器语言
         */
        enum class ShaderLanguage : uint8_t
        {
            GLSL,        ///< GLSL
            HLSL,        ///< HLSL
            MSL,         ///< Metal Shading Language
            SPIRV,       ///< SPIR-V
            ShaderBinary ///< 平台特定二进制
        };

        /**
         * @brief 描述符类型
         */
        enum class DescriptorType : uint8_t
        {
            Sampler,              ///< 采样器
            CombinedImageSampler, ///< 组合图像采样器
            SampledImage,         ///< 采样图像
            StorageImage,         ///< 存储图像
            UniformTexelBuffer,   ///< 统一纹素缓冲区
            StorageTexelBuffer,   ///< 存储纹素缓冲区
            UniformBuffer,        ///< 统一缓冲区
            StorageBuffer,        ///< 存储缓冲区
            UniformBufferDynamic, ///< 动态统一缓冲区
            StorageBufferDynamic, ///< 动态存储缓冲区
            InputAttachment,      ///< 输入附件
            AccelerationStructure ///< 加速结构
        };

        /**
         * @brief 查询类型
         */
        enum class QueryType : uint8_t
        {
            Occlusion,          ///< 遮挡查询
            PipelineStatistics, ///< 管线统计
            Timestamp           ///< 时间戳
        };

        /**
         * @brief 着色器阶段标志
         */
        enum class ShaderStageFlag : uint16_t
        {
            None = 0,
            Vertex = 1 << 0,
            TessControl = 1 << 1,
            TessEvaluation = 1 << 2,
            Geometry = 1 << 3,
            Fragment = 1 << 4,
            Compute = 1 << 5,
            RayGen = 1 << 6,
            AnyHit = 1 << 7,
            ClosestHit = 1 << 8,
            Miss = 1 << 9,
            Intersection = 1 << 10,
            Callable = 1 << 11,
            Task = 1 << 12,
            Mesh = 1 << 13,
            AllGraphics = Vertex | TessControl | TessEvaluation | Geometry | Fragment,
            AllRayTracing = RayGen | AnyHit | ClosestHit | Miss | Intersection | Callable,
            All = AllGraphics | Compute | AllRayTracing | Task | Mesh
        };

        // 定义类型别名以保持兼容性
        using ShaderStageFlags = uint16_t;

        // 位运算操作符重载
        inline ShaderStageFlag operator|(ShaderStageFlag a, ShaderStageFlag b)
        {
            return static_cast<ShaderStageFlag>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
        }

        inline ShaderStageFlag operator&(ShaderStageFlag a, ShaderStageFlag b)
        {
            return static_cast<ShaderStageFlag>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
        }

        inline ShaderStageFlag &operator|=(ShaderStageFlag &a, ShaderStageFlag b)
        {
            a = a | b;
            return a;
        }

        inline ShaderStageFlag &operator&=(ShaderStageFlag &a, ShaderStageFlag b)
        {
            a = a & b;
            return a;
        }

        /**
         * @brief 渲染API特性
         */
        enum class RenderFeature : uint16_t
        {
            Geometry,                ///< 几何着色器
            TessellationShader,      ///< 曲面细分着色器
            LogicOp,                 ///< 逻辑操作
            MultiViewport,           ///< 多视口
            SamplerAnisotropy,       ///< 各向异性采样
            TextureCompressionBC,    ///< BC纹理压缩
            TextureCompressionETC2,  ///< ETC2纹理压缩
            TextureCompressionASTC,  ///< ASTC纹理压缩
            TextureCubeArray,        ///< 立方体纹理数组
            Raytracing,              ///< 光线追踪
            MeshShading,             ///< 网格着色
            DrawIndirectCount,       ///< 间接绘制计数
            DepthClipEnable,         ///< 深度裁剪启用
            DepthBoundsTest,         ///< 深度边界测试
            WideLines,               ///< 宽线
            FillModeNonSolid,        ///< 非实体填充模式
            VariableMultisampleRate, ///< 可变多重采样率
            InheritedQueries         ///< 继承查询
        };

        /**
         * @brief 创建标志
         */
        enum class CreateFlag : uint32_t
        {
            None = 0,
            Debug = 1 << 0,      ///< 启用调试
            Validation = 1 << 1, ///< 启用验证
            Performance = 1 << 2 ///< 启用性能分析
        };

        // 位运算操作符重载
        inline CreateFlag operator|(CreateFlag a, CreateFlag b)
        {
            return static_cast<CreateFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
        }

        inline CreateFlag operator&(CreateFlag a, CreateFlag b)
        {
            return static_cast<CreateFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
        }

        inline CreateFlag &operator|=(CreateFlag &a, CreateFlag b)
        {
            a = a | b;
            return a;
        }

        inline CreateFlag &operator&=(CreateFlag &a, CreateFlag b)
        {
            a = a & b;
            return a;
        }

        // 添加与uint32_t的位运算操作符重载
        inline bool operator&(uint32_t a, CreateFlag b)
        {
            return (a & static_cast<uint32_t>(b)) != 0;
        }

        inline bool operator&(CreateFlag a, uint32_t b)
        {
            return (static_cast<uint32_t>(a) & b) != 0;
        }

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_GRAPHICS_ENUMS_H