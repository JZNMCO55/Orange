# VulkanContext 实现文档

## 概述

VulkanContext是Orange引擎中Vulkan渲染上下文的完整实现，负责管理命令缓冲区的记录、提交和执行。它实现了IRenderContext接口，提供了统一的渲染命令API。

## 主要功能

### 1. 命令缓冲区管理
- **初始化**: 从VulkanDevice的命令池分配主要命令缓冲区
- **开始/结束记录**: 管理命令缓冲区的记录状态
- **重置**: 重置命令缓冲区以供重用
- **提交**: 将命令缓冲区提交到GPU队列执行

### 2. 渲染通道控制
- **BeginRenderPass**: 开始渲染通道，设置帧缓冲和清除值
- **EndRenderPass**: 结束渲染通道
- **状态跟踪**: 跟踪是否在渲染通道中，防止错误操作

### 3. 管线和资源绑定
- **BindPipeline**: 绑定图形或计算管线
- **BindVertexBuffer**: 绑定顶点缓冲区
- **BindIndexBuffer**: 绑定索引缓冲区
- **BindDescriptorSet**: 绑定描述符集（待完善）

### 4. 动态状态设置
- **SetViewports**: 设置视口
- **SetScissors**: 设置裁剪矩形
- **SetLineWidth**: 设置线宽
- **SetDepthBias**: 设置深度偏移
- **SetBlendConstants**: 设置混合常量
- **SetStencilReference**: 设置模板参考值

### 5. 绘制命令
- **Draw**: 基本绘制命令
- **DrawIndexed**: 索引绘制命令
- **DrawIndirect**: 间接绘制命令
- **DrawIndexedIndirect**: 间接索引绘制命令

### 6. 计算着色器
- **Dispatch**: 执行计算着色器
- **DispatchIndirect**: 间接执行计算着色器

### 7. 资源复制
- **CopyBuffer**: 缓冲区间复制
- **CopyBufferToTexture**: 缓冲区到纹理复制
- **CopyTextureToBuffer**: 纹理到缓冲区复制
- **CopyTexture**: 纹理间复制

### 8. 资源屏障
- **TextureBarrier**: 纹理资源状态转换
- **BufferBarrier**: 缓冲区资源状态转换
- **自动转换**: 支持Orange资源状态到Vulkan状态的自动转换

### 9. Mipmap生成
- **GenerateMipmaps**: 自动生成纹理mipmap链
- **线性过滤**: 使用线性过滤进行mipmap生成

### 10. 调试支持
- **BeginDebugMarker**: 开始调试标记
- **EndDebugMarker**: 结束调试标记
- **InsertDebugMarker**: 插入调试标记
- **扩展检查**: 自动检查调试扩展支持

## 实现特点

### 1. 状态管理
- 跟踪命令缓冲区记录状态
- 跟踪渲染通道状态
- 防止无效操作

### 2. 错误处理
- 完善的状态检查
- 详细的错误日志
- 优雅的错误恢复

### 3. 类型转换
- Orange枚举到Vulkan枚举的自动转换
- 资源状态到管线阶段/访问标志的转换
- 图像布局的智能转换

### 4. 性能优化
- 最小化状态切换
- 批量操作支持
- 高效的资源绑定

## 工具方法

### 格式转换方法
- `ConvertClearValues`: 清除值转换
- `ConvertRect2D`: 矩形转换
- `ConvertViewport`: 视口转换
- `ConvertIndexType`: 索引类型转换
- `ConvertShaderStageFlags`: 着色器阶段标志转换

### 资源状态转换方法
- `ConvertResourceStateToPipelineStage`: 资源状态到管线阶段
- `ConvertResourceStateToAccessFlags`: 资源状态到访问标志
- `ConvertResourceStateToImageLayout`: 资源状态到图像布局

## 使用示例

```cpp
// 创建上下文
auto context = device->CreateContext();

// 开始记录
context->Begin();

// 开始渲染通道
context->BeginRenderPass(renderPass, framebuffer, clearValues, renderArea);

// 绑定管线和资源
context->BindPipeline(pipeline);
context->BindVertexBuffer(vertexBuffer, 0);
context->BindIndexBuffer(indexBuffer, 32);

// 设置视口
context->SetViewports({viewport});

// 绘制
context->DrawIndexed(indexCount);

// 结束渲染通道
context->EndRenderPass();

// 结束记录
context->End();

// 提交执行
context->Submit(waitSemaphores, signalSemaphores, fence);
```

## 待完善功能

1. **描述符集绑定**: 完整的描述符集管理
2. **推送常量**: 推送常量的完整支持
3. **多线程支持**: 次要命令缓冲区支持
4. **查询对象**: 时间戳和遮挡查询
5. **条件渲染**: 条件渲染支持

## 依赖关系

- **VulkanDevice**: 设备和命令池
- **VulkanRenderPass**: 渲染通道管理
- **VulkanPipeline**: 管线绑定
- **VulkanBuffer**: 缓冲区操作
- **VulkanTexture**: 纹理操作
- **VulkanSync**: 同步对象

## 注意事项

1. 命令缓冲区必须在记录状态才能执行命令
2. 绘制命令必须在渲染通道中执行
3. 计算和复制命令不能在渲染通道中执行
4. 资源屏障需要正确的状态转换
5. 调试标记需要扩展支持 