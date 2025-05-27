# VulkanSemaphore 实现说明

## 概述

VulkanSemaphore 是 Vulkan 信号量的封装实现，提供了二进制信号量和时间线信号量的支持。

## 实现特性

### 1. 基本功能
- ✅ **二进制信号量创建** - 支持标准的二进制信号量
- ✅ **时间线信号量创建** - 支持 Vulkan 1.2+ 的时间线信号量
- ✅ **资源管理** - 自动管理 VkSemaphore 的生命周期
- ✅ **调试支持** - 支持设置调试名称

### 2. 接口实现
- ✅ `GetType()` - 获取信号量类型
- ✅ `GetDevice()` - 获取所属设备
- ✅ `GetNativeSemaphore()` - 获取原生 VkSemaphore 句柄
- ✅ `SetName()/GetName()` - 调试名称管理
- ✅ `Initialize()` - 初始化信号量
- ✅ `Shutdown()` - 清理资源

### 3. 高级功能（部分实现）
- ⚠️ `GetCounterValue()` - 获取时间线信号量计数值（TODO）
- ⚠️ `Wait()` - 等待时间线信号量达到指定值（TODO）
- ⚠️ `Signal()` - 主机端信号触发（TODO）

## 工厂模式实现

### VulkanSemaphoreFactory
- ✅ `CreateSemaphore()` - 创建信号量实例
- ✅ `DestroySemaphore()` - 销毁信号量实例
- ⚠️ `WaitSemaphores()` - 批量等待时间线信号量（TODO）
- ⚠️ `SignalSemaphores()` - 批量信号触发（TODO）

## 使用示例

### 创建二进制信号量
```cpp
SemaphoreCreateInfo createInfo{};
createInfo.type = SemaphoreType::Binary;
createInfo.debugName = "RenderComplete";

auto semaphore = device->CreateSemaphore();
// 或使用工厂
auto factory = new VulkanSemaphoreFactory(vulkanDevice);
auto semaphore = factory->CreateSemaphore(createInfo);
```

### 创建时间线信号量
```cpp
SemaphoreCreateInfo createInfo{};
createInfo.type = SemaphoreType::Timeline;
createInfo.initialValue = 0;
createInfo.debugName = "FrameCounter";

auto factory = new VulkanSemaphoreFactory(vulkanDevice);
auto semaphore = factory->CreateSemaphore(createInfo);
```

### 在命令队列中使用
```cpp
// 二进制信号量主要用于命令队列同步
VkSubmitInfo submitInfo{};
submitInfo.signalSemaphoreCount = 1;
submitInfo.pSignalSemaphores = &semaphore->GetVkSemaphore();

vkQueueSubmit(queue, 1, &submitInfo, fence);
```

## 技术细节

### 1. Vulkan API 映射
```cpp
// 二进制信号量创建
VkSemaphoreCreateInfo semaphoreInfo{};
semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

// 时间线信号量创建
VkSemaphoreTypeCreateInfo typeInfo{};
typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
typeInfo.initialValue = createInfo.initialValue;
semaphoreInfo.pNext = &typeInfo;
```

### 2. 资源管理
- 构造函数中不分配资源
- `Initialize()` 方法中创建 VkSemaphore
- 析构函数中自动调用 `Shutdown()`
- `Shutdown()` 方法中安全释放 VkSemaphore

### 3. 错误处理
- 所有 Vulkan API 调用都检查返回值
- 失败时输出错误信息到 stderr
- 初始化失败时正确清理资源

## 待实现功能

### 1. 时间线信号量高级功能
需要 Vulkan 1.2+ 支持：
```cpp
// 获取计数值
vkGetSemaphoreCounterValue(device, semaphore, &value);

// 等待信号量
VkSemaphoreWaitInfo waitInfo{};
vkWaitSemaphores(device, &waitInfo, timeout);

// 信号触发
VkSemaphoreSignalInfo signalInfo{};
vkSignalSemaphore(device, &signalInfo);
```

### 2. 批量操作
- 批量等待多个时间线信号量
- 批量信号触发多个时间线信号量

### 3. 调试支持增强
- Vulkan 调试标签设置
- 性能标记支持

## 兼容性说明

- **Vulkan 1.0**: 支持二进制信号量的所有功能
- **Vulkan 1.2+**: 支持时间线信号量创建，高级功能待实现
- **平台**: Windows/Linux/macOS 通用

## 性能考虑

1. **零拷贝设计** - 直接返回 VkSemaphore 句柄
2. **最小开销** - 避免不必要的状态检查
3. **内存效率** - 使用栈分配的创建信息结构体
4. **异常安全** - RAII 模式确保资源正确释放

## 测试建议

1. **基本功能测试**
   - 二进制信号量创建/销毁
   - 时间线信号量创建/销毁
   - 调试名称设置

2. **集成测试**
   - 与命令队列的集成
   - 多线程环境下的使用
   - 错误条件处理

3. **性能测试**
   - 大量信号量创建/销毁
   - 高频率使用场景 