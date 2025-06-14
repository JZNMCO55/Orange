# Hazel 渲染器模块头文件功能总结

## 概述

Hazel 渲染器模块是一个现代化的3D渲染引擎，采用模块化设计，支持多种图形API（主要是Vulkan），提供了完整的渲染管线和资源管理系统。本文档总结了该模块下所有头文件的功能和作用。

## 核心架构文件

### 1. RendererAPI.h
**功能**: 渲染API抽象接口
- 定义了 `RendererAPIType` 枚举（None, Vulkan）
- 提供 `RendererAPI` 抽象基类，定义了所有渲染API实现必须提供的接口
- 支持图元类型定义（Triangles, Lines）
- 采用单例模式管理当前使用的渲染API类型

### 2. RendererContext.h
**功能**: 渲染上下文抽象接口
- 定义了平台无关的渲染上下文抽象基类 `RendererContext`
- 负责管理图形API的生命周期，包括设备创建、交换链管理等
- 提供了创建渲染上下文实例的静态工厂方法

### 3. Renderer.h
**功能**: 渲染器主控制类
- 提供了渲染系统的主要接口和功能
- 支持多线程渲染命令队列系统
- 包含完整的渲染管线API（RenderPass, ComputePass）
- 提供GPU性能标记和调试功能
- 管理全局着色器宏和着色器重载机制

### 4. RendererTypes.h
**功能**: 基础类型定义
- 定义了 `RendererID` 类型（uint32_t），用于标识渲染器对象的唯一ID

### 5. RendererCapabilities.h
**功能**: 硬件能力查询
- 定义了 `RendererCapabilities` 结构体，存储图形硬件和驱动程序的能力信息
- 包含厂商信息、设备名称、驱动版本、最大采样数、各向异性过滤等信息

### 6. RendererConfig.h
**功能**: 渲染器配置参数
- 定义了 `RendererConfig` 结构体，包含渲染器初始化和运行时的配置参数
- 包含飞行帧数、环境贴图设置、分层设置、着色器包路径等配置选项

## 渲染管线文件

### 7. RenderPass.h
**功能**: 渲染通道管理
- 定义了 `RenderPass` 类，管理单个渲染通道
- 支持输入资源绑定（UniformBuffer, StorageBuffer, Texture, Image）
- 提供输出管理和验证功能

### 8. ComputePass.h
**功能**: 计算通道管理
- 管理GPU计算着色器的执行
- 提供计算工作组调度功能

### 9. Pipeline.h
**功能**: 渲染管线状态
- 定义了 `Pipeline` 类，管理图形渲染管线状态
- 支持多种图元拓扑（Points, Lines, Triangles等）
- 包含深度测试、背面剔除、线框模式等渲染状态
- 提供管线统计信息和资源访问控制

### 10. PipelineCompute.h
**功能**: 计算管线
- 管理计算着色器的管线状态
- 支持计算着色器的编译和绑定

## 资源管理文件

### 11. VertexBuffer.h
**功能**: 顶点缓冲区系统
- 定义了着色器数据类型枚举 `ShaderDataType`
- 提供 `VertexBufferElement` 结构体描述顶点属性
- `VertexBufferLayout` 类管理顶点布局信息
- `VertexBuffer` 类管理GPU顶点数据存储

### 12. IndexBuffer.h
**功能**: 索引缓冲区系统
- 管理GPU上的索引数据存储
- 支持32位索引格式
- 提供索引数量计算和数据更新功能

### 13. UniformBuffer.h
**功能**: 统一缓冲区
- 提供着色器统一变量的缓冲区管理
- 支持数据更新和渲染线程操作

### 14. StorageBuffer.h
**功能**: 存储缓冲区
- 管理着色器存储缓冲区对象（SSBO）
- 支持GPU专用和可调整大小的缓冲区

### 15. UniformBufferSet.h
**功能**: 统一缓冲区集合
- 管理多个统一缓冲区的集合
- 提供批量绑定和管理功能

### 16. StorageBufferSet.h
**功能**: 存储缓冲区集合
- 管理多个存储缓冲区的集合

## 纹理和图像文件

### 17. Texture.h
**功能**: 纹理系统
- 定义了 `TextureSpecification` 纹理规格
- `Texture2D` 类管理2D纹理资源
- `TextureCube` 类管理立方体贴图
- 支持多种纹理格式、过滤和包装模式
- 提供Mipmap生成和数据传输功能

### 18. Image.h
**功能**: 图像系统
- 定义了图像格式枚举 `ImageFormat`
- `Image2D` 类管理2D图像资源
- 支持多种图像用途（纹理、附件、存储等）
- 提供图像视图和层视图管理

### 19. Framebuffer.h
**功能**: 帧缓冲区
- 管理渲染目标和深度缓冲区
- 支持多重采样和多个颜色附件
- 提供动态调整大小和清除操作
- 支持多种混合模式

## 几何和网格文件

### 20. Mesh.h
**功能**: 网格系统
- 定义了顶点结构 `Vertex` 和骨骼影响 `BoneInfluence`
- `Submesh` 类管理子网格信息
- `MeshSource` 类表示磁盘上的网格资源文件
- `Mesh` 和 `StaticMesh` 类管理渲染用的网格实例
- 支持骨骼动画和层次结构

### 21. MeshFactory.h
**功能**: 网格工厂
- 提供基础几何体的创建功能（如立方体、球体等）

## 材质和着色器文件

### 22. Shader.h
**功能**: 着色器系统
- 定义了着色器统一变量类型和缓冲区结构
- `Shader` 类管理着色器程序的加载、编译和重载
- `ShaderLibrary` 类管理着色器库
- 支持着色器包加载和宏定义

### 23. Material.h
**功能**: 材质系统
- 定义了材质标志 `MaterialFlag`
- `Material` 类管理着色器参数和纹理绑定
- 支持材质参数的动态设置和获取
- 提供材质复制和无效化功能

### 24. MaterialAsset.h
**功能**: 材质资源
- 管理材质作为资源的加载和保存

### 25. ShaderPack.h
**功能**: 着色器包
- 管理预编译着色器包的加载

### 26. ShaderUniform.h
**功能**: 着色器统一变量
- 定义着色器资源声明和统一变量管理

### 27. ShaderDefs.h
**功能**: 着色器定义
- 包含着色器相关的宏定义和常量

## 场景渲染文件

### 28. SceneRenderer.h
**功能**: 场景渲染器
- 高级场景渲染管理器，集成了完整的渲染管线
- 支持阴影映射、环境光遮蔽（GTAO）、屏幕空间反射（SSR）
- 包含后处理效果（Bloom、DOF）
- 提供多种渲染选项和统计信息
- 管理光照环境和相机设置

### 29. SceneEnvironment.h
**功能**: 场景环境
- 管理场景的环境设置

## 相机和控制文件

### 30. Camera.h
**功能**: 相机系统
- 管理3D渲染中的相机投影矩阵
- 支持透视投影和正交投影
- 使用反向Z技术提高深度精度
- 包含HDR曝光控制

## 2D渲染文件

### 31. Renderer2D.h
**功能**: 2D渲染器
- 专门用于2D图形渲染的系统
- 支持四边形、圆形、线条、文字渲染
- 提供变换和billboard渲染
- 包含批处理优化和统计信息

## 调试和工具文件

### 32. DebugRenderer.h
**功能**: 调试渲染器
- 提供调试可视化功能
- 支持调试线条、包围盒等的渲染

## 设备管理文件

### 33. DeviceManager.h
**功能**: 设备管理器
- 管理图形设备的创建和初始化
- 支持多种图形API（Vulkan、DirectX）
- 提供适配器枚举和DPI缩放
- 管理窗口和输入事件

## 命令和队列文件

### 34. RenderCommandBuffer.h
**功能**: 渲染命令缓冲区
- 管理渲染命令的记录和执行

### 35. RenderCommandQueue.h
**功能**: 渲染命令队列
- 管理渲染命令的队列化执行

## 资源管理基础文件

### 36. RendererResource.h
**功能**: 渲染器资源基类
- 定义渲染器资源的基础接口

### 37. RendererStats.h
**功能**: 渲染器统计
- 提供渲染统计信息的收集和管理

### 38. GPUStats.h
**功能**: GPU统计
- 管理GPU内存使用统计

## 模块特点

1. **现代化设计**: 使用C++17/20特性，RAII资源管理，智能指针
2. **API抽象**: 支持多种图形API，目前主要支持Vulkan
3. **多线程支持**: 渲染命令队列系统支持多线程渲染
4. **资源管理**: 完善的资源生命周期管理和缓存系统
5. **高级特性**: 支持PBR渲染、阴影映射、后处理等现代渲染技术
6. **调试支持**: 集成调试和性能分析工具
7. **可扩展性**: 模块化设计，易于扩展新功能

该渲染器模块为Hazel引擎提供了完整的现代化渲染能力，适合开发高质量的3D应用程序和游戏。 

# 实现顺序
## 第一阶段：基础架构（核心层）
### 1.1 基础类型和工具
```cpp
// 基础设施
Base.h              // 基础宏定义、断言、位操作等
Ref.h               // 智能指针系统（RefCounted, Ref, WeakRef）
Buffer.h            // 内存缓冲区管理
Log.h               // 日志系统

// 渲染器基础类型
RendererTypes.h     // RendererID 等基础类型定义
RendererCapabilities.h  // 硬件能力查询结构
RendererConfig.h    // 渲染器配置参数
```

### 1.2 API抽象层
```cpp
// API抽象层
RendererAPI.h      // 渲染API抽象接口
RendererContext.h  // 渲染上下文抽象
```

### 1.3 设备管理
```cpp
DeviceManager.h     // 设备创建、窗口管理、输入处理
```

### 1.4 渲染器
```cpp
Renderer.h           // 主渲染器接口（命令提交，资源管理）
```

## 第二阶段：基础资源管理

### 2.1 缓冲区系统
```cpp
VertexBuffer.h      // 顶点缓冲区（ShaderDataType, VertexBufferElement, VertexBufferLayout）
IndexBuffer.h       // 索引缓冲区
UniformBuffer.h     // 统一缓冲区
RendererResource.h  // 渲染资源基类
```

### 2.2 着色器系统
```cpp
ShaderUniform.h     // 着色器统一变量管理
Shader.h           // 着色器编译、加载、重载
ShaderDefs.h       // 着色器相关宏定义
```

### 2.3 基础纹理系统
```cpp
Image.h            // 基础图像管理（ImageFormat, ImageSpecification, Image2D）
Texture.h          // 纹理资源（TextureSpecification, Texture2D）
```

## 第三阶段：渲染管线

### 3.1 渲染状态管理
```cpp
Pipeline.h         // 渲染管线状态（PipelineSpecification, DepthCompareOperator）
```

### 3.2 帧缓冲区
```cpp
Framebuffer.h      // 帧缓冲区管理（FramebufferSpecification, 多重采样）
```

### 3.3 渲染通道
```cpp
RenderCommandBuffer.h   // 渲染命令记录
RenderCommandQueue.h    // 渲染命令队列
RenderPass.h           // 渲染通道抽象
```

## 第四阶段：高级资源和材质

### 4.1 缓冲区集合
```cpp
UniformBufferSet.h     // 统一缓冲区集合管理
StorageBuffer.h        // 存储缓冲区（SSBO）
StorageBufferSet.h     // 存储缓冲区集合
```

### 4.2 材质系统
```cpp
Material.h            // 材质参数管理（MaterialFlag, 参数绑定）
MaterialAsset.h       // 材质资源加载
```

### 4.3 几何系统
```cpp
Mesh.h               // 网格系统（Vertex, Submesh, MeshSource, StaticMesh）
MeshFactory.h        // 基础几何体生成
```

### 4.4 相机系统
```cpp
Camera.h             // 相机投影矩阵（透视/正交投影，反向Z）
```

## 第五阶段：渲染器核心

### 5.1 主渲染器
```cpp
Renderer.h           // 主渲染器接口（命令提交，资源管理）
RendererStats.h      // 渲染统计信息
GPUStats.h          // GPU内存统计
```

### 5.2 2D渲染器
```cpp
Renderer2D.h         // 2D图形渲染（四边形、线条、文字）
```

### 5.3 着色器管理
```cpp
ShaderPack.h         // 预编译着色器包
```

## 第六阶段：高级特性

### 6.1 计算着色器
```cpp
ComputePass.h        // 计算通道管理
PipelineCompute.h    // 计算管线状态
```

### 6.2 场景渲染
```cpp
SceneEnvironment.h   // 场景环境设置
SceneRenderer.h      // 高级场景渲染器（阴影、GTAO、SSR、后处理）
```

### 6.3 调试和工具
```cpp
DebugRenderer.h      // 调试可视化工具
```

## 📋 **开发检查清单**

### 每个阶段的验证目标：

**阶段1完成** ✅
- [ ] 能够创建窗口和渲染上下文
- [ ] 基础类型和配置系统工作正常
- [ ] 设备初始化成功

**阶段2完成** ✅  
- [ ] 能够创建和使用顶点缓冲区
- [ ] 简单着色器编译和使用
- [ ] 基础纹理加载

**阶段3完成** ✅
- [ ] 能够渲染一个三角形
- [ ] 渲染管线状态切换正常
- [ ] 帧缓冲区工作正常

**阶段4完成** ✅
- [ ] 能够渲染带纹理的立方体
- [ ] 材质系统工作正常
- [ ] 网格加载和渲染

**阶段5完成** ✅
- [ ] 完整的渲染循环
- [ ] 3D场景渲染
- [ ] 2D UI渲染

**阶段6完成** ✅
- [ ] 高级渲染特性
- [ ] 性能优化
- [ ] 调试工具

## 💡 **实用开发提示**

### 依赖关系参考：
```
Base.h → RendererTypes.h → VertexBuffer.h → Pipeline.h → Renderer.h
     → Ref.h → Image.h → Texture.h → Material.h
          → Shader.h → Material.h
               → Mesh.h → SceneRenderer.h
```

### 每个头文件的核心类/结构：
- **VertexBuffer.h**: `ShaderDataType`, `VertexBufferElement`, `VertexBufferLayout`, `VertexBuffer`
- **Shader.h**: `ShaderUniform`, `ShaderBuffer`, `Shader`, `ShaderLibrary`
- **Pipeline.h**: `PipelineSpecification`, `Pipeline`
- **Material.h**: `MaterialFlag`, `Material`
- **Renderer.h**: `Renderer` (主要的静态接口类)

这个清单可以作为您开发过程中的参考指南，每完成一个头文件就可以打勾，确保按照正确的依赖顺序进行开发！