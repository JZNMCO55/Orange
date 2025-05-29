# Orange Engine Application & Layer 系统

## 概述

Orange Engine 的 Application 和 Layer 系统提供了一个灵活的应用程序架构，支持层级化的事件处理和更新逻辑。

## 核心组件

### Application 类
- **职责**: 应用程序的主要入口点和生命周期管理
- **功能**: 
  - 窗口管理
  - 事件分发
  - 层级管理
  - 主循环控制

### Layer 类
- **职责**: 应用程序功能的模块化单元
- **功能**:
  - 独立的更新逻辑
  - 事件处理
  - 生命周期管理（OnAttach/OnDetach）

### LayerStack 类
- **职责**: 管理层级的容器
- **功能**:
  - 层级的添加和移除
  - 正确的更新和事件传播顺序

## 架构设计

```
Application
├── Window (窗口管理)
├── LayerStack (层级管理)
│   ├── Layer 1 (游戏逻辑层)
│   ├── Layer 2 (渲染层)
│   └── Overlay (UI层)
└── Event System (事件系统)
```

## 事件处理流程

1. **窗口事件产生** → GLFW回调
2. **Application接收** → OnEvent()
3. **Application处理** → 窗口关闭、大小改变
4. **层级处理** → 从后往前传播（Overlay优先）
5. **事件完成** → 继续主循环

## 使用方法

### 1. 创建自定义应用程序

```cpp
#include "Application.h"
#include "ExampleLayer.h"

class MyApplication : public Orange::Core::Application
{
public:
    MyApplication()
    {
        // 添加游戏层
        auto gameLayer = std::make_shared<GameLayer>();
        PushLayer(gameLayer);
        
        // 添加UI层（Overlay）
        auto uiLayer = std::make_shared<UILayer>();
        PushOverlay(uiLayer);
    }
};

int main()
{
    auto app = std::make_unique<MyApplication>();
    app->Run();
    return 0;
}
```

### 2. 创建自定义Layer

```cpp
#include "Layer.h"
#include "../Event/Events.h"

class GameLayer : public Orange::Core::Layer
{
public:
    GameLayer() : Layer("GameLayer") {}

    virtual void OnAttach() override
    {
        // 初始化游戏资源
        std::cout << "Game layer attached" << std::endl;
    }

    virtual void OnDetach() override
    {
        // 清理游戏资源
        std::cout << "Game layer detached" << std::endl;
    }

    virtual void OnUpdate() override
    {
        // 每帧更新游戏逻辑
        UpdateGameObjects();
        UpdatePhysics();
    }

    virtual void OnEvent(Event& event) override
    {
        EventDispatcher dispatcher(event);
        
        dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& e) -> bool
        {
            return OnKeyPressed(e);
        });
    }

private:
    bool OnKeyPressed(KeyPressedEvent& e)
    {
        // 处理游戏相关的按键
        if (e.GetKeyCode() == static_cast<int>(KeyCode::Space))
        {
            Jump();
            return true; // 事件已处理
        }
        return false; // 事件未处理，继续传播
    }
};
```

### 3. Layer vs Overlay

- **Layer**: 普通层级，按添加顺序更新，事件处理优先级较低
- **Overlay**: 覆盖层，通常用于UI，事件处理优先级较高

```cpp
// 添加普通层级
app->PushLayer(gameLayer);      // 游戏逻辑层
app->PushLayer(renderLayer);    // 渲染层

// 添加覆盖层
app->PushOverlay(debugLayer);   // 调试信息层
app->PushOverlay(uiLayer);      // UI层
```

## 事件处理优先级

事件处理顺序（从高到低）：
1. Application 系统事件（窗口关闭、大小改变）
2. Overlay 层级（从后往前）
3. Layer 层级（从后往前）

## 生命周期

### Application 生命周期
1. **构造函数** → 创建窗口、初始化系统
2. **Run()** → 进入主循环
3. **主循环** → 更新层级、处理事件、更新窗口
4. **析构函数** → 清理资源

### Layer 生命周期
1. **构造函数** → 创建Layer对象
2. **OnAttach()** → 添加到LayerStack时调用
3. **OnUpdate()** → 每帧调用
4. **OnEvent()** → 有事件时调用
5. **OnDetach()** → 从LayerStack移除时调用
6. **析构函数** → 销毁Layer对象

## 最佳实践

### 1. 层级设计
- **游戏逻辑层**: 处理游戏核心逻辑
- **渲染层**: 处理图形渲染
- **物理层**: 处理物理模拟
- **UI层**: 处理用户界面（Overlay）
- **调试层**: 处理调试信息（Overlay）

### 2. 事件处理
- UI层应该优先处理输入事件
- 如果UI处理了事件，应该返回true阻止传播
- 游戏层处理游戏相关的输入
- 系统事件由Application处理

### 3. 性能考虑
- 避免在OnUpdate中进行重复的资源加载
- 鼠标移动事件频率很高，谨慎处理
- 使用事件的Handled标志避免不必要的处理

## 扩展示例

### 多层级游戏架构
```cpp
class GameApplication : public Application
{
public:
    GameApplication()
    {
        // 核心游戏层
        PushLayer(std::make_shared<GameLogicLayer>());
        PushLayer(std::make_shared<PhysicsLayer>());
        PushLayer(std::make_shared<RenderLayer>());
        
        // UI和调试层
        PushOverlay(std::make_shared<UILayer>());
        PushOverlay(std::make_shared<DebugLayer>());
    }
};
```

这种架构提供了：
- 清晰的职责分离
- 灵活的事件处理
- 易于扩展和维护的代码结构
- 模块化的功能组织 