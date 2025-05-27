        └── Platform/
            └── Graphics/
                ├── Common/
                │   ├── GraphicsTypes.h      // 通用数据类型
                │   ├── GraphicsEnums.h      // 通用枚举类型
                │   └── GraphicsConstants.h  // 常量定义
                ├── Interface/               // 替换之前的Core目录
                │   ├── IRenderDevice.h      // 设备接口
                │   ├── IRenderContext.h     // 上下文接口
                │   ├── IRenderPipeline.h    // 管线接口
                │   ├── IRenderPass.h        // 渲染通道接口
                │   └── ISwapChain.h         // 交换链接口
                ├── RenderResources/
                │   ├── IBuffer.h            // 缓冲区接口
                │   ├── ITexture.h           // 纹理接口
                │   ├── ISampler.h           // 采样器接口
                │   └── IShader.h            // 着色器接口
                ├── RenderSync/
                │   ├── IRenderFence.h             // 栅栏接口
                │   ├── IRenderSemaphore.h         // 信号量接口
                │   └── IEvent.h             // 事件接口
                ├── RenderGPUMemory/               // 明确指定为GPU内存管理
                │   ├── IGPUMemoryAllocator.h // GPU内存分配器接口
                │   └── GPUMemoryTypes.h     // GPU内存类型定义
                ├── RenderCommands/
                │   ├── ICommandBuffer.h     // 命令缓冲区接口
                │   └── ICommandPool.h       // 命令池接口
                ├── Vulkan/
                │   ├── VulkanDevice.h       // Vulkan设备实现
                │   ├── VulkanContext.h      // Vulkan上下文实现
                │   ├── VulkanPipeline.h     // Vulkan管线实现
                │   └── ...                  // 其他Vulkan实现
                ├── D3D12/                   // 未来DirectX 12实现
                ├── Metal/                   // 未来Metal实现
                └── GraphicsSystem.h         // 图形系统入口(而非RenderSystem)

                BufferUsageFlags
                ResourceBindingDesc
                BufferViewDesc
                BufferUsageFlagBits
                MemoryPropertyFlags
                MemoryPropertyFlagBits
                SharingMode
                SharingMode
