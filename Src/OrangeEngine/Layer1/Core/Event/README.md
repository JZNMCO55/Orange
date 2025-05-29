# Orange Engine 事件系统

## 概述

Orange Engine 的事件系统是一个基于观察者模式的事件处理框架，支持窗口事件、键盘事件、鼠标事件等多种类型的事件。

## 架构设计

### 核心组件

1. **Event 基类** - 所有事件的基类
2. **EventDispatcher** - 事件分发器，用于将事件分发给相应的处理函数
3. **具体事件类** - 继承自Event的具体事件实现
4. **事件回调函数** - 处理事件的函数

### 事件类型

#### 窗口事件 (ApplicationEvent.h)
- `WindowCloseEvent` - 窗口关闭事件
- `WindowResizeEvent` - 窗口大小改变事件
- `WindowFocusEvent` - 窗口获得焦点事件
- `WindowLostFocusEvent` - 窗口失去焦点事件
- `WindowMovedEvent` - 窗口移动事件

#### 键盘事件 (KeyEvent.h)
- `KeyPressedEvent` - 按键按下事件
- `KeyReleasedEvent` - 按键释放事件
- `KeyTypedEvent` - 字符输入事件

#### 鼠标事件 (MouseEvent.h)
- `MouseMovedEvent` - 鼠标移动事件
- `MouseScrolledEvent` - 鼠标滚轮事件
- `MouseButtonPressedEvent` - 鼠标按钮按下事件
- `MouseButtonReleasedEvent` - 鼠标按钮释放事件

## 使用方法

### 1. 在Application中处理事件

```cpp
#include "../Event/Events.h"

void Application::OnEvent(Event& e)
{
    EventDispatcher dispatcher(e);
    
    // 处理窗口关闭事件
    dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event)
    {
        std::cout << "Window close event received" << std::endl;
        this->Close();
        return true; // 事件已处理
    });

    // 处理键盘事件
    dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event)
    {
        if (event.GetKeyCode() == static_cast<int>(KeyCode::Escape))
        {
            this->Close();
            return true;
        }
        return false; // 事件未处理，继续传播
    });

    // 处理鼠标事件
    dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& event)
    {
        if (event.GetMouseButton() == static_cast<int>(MouseCode::ButtonLeft))
        {
            std::cout << "Left mouse button pressed" << std::endl;
        }
        return false;
    });
}
```

### 2. 设置窗口事件回调

```cpp
// 在Application构造函数中
m_window->SetEventCallback([this](Event& event)
{
    this->OnEvent(event);
});
```

### 3. 使用键盘和鼠标代码

```cpp
#include "KeyCodes.h"

// 检查特定按键
if (keyCode == static_cast<int>(KeyCode::W)) {
    // W键被按下
}

// 检查鼠标按钮
if (mouseButton == static_cast<int>(MouseCode::ButtonLeft)) {
    // 左键被按下
}
```

## 事件分类

事件系统支持事件分类，可以用于过滤特定类型的事件：

- `EventCategoryApplication` - 应用程序事件
- `EventCategoryInput` - 输入事件
- `EventCategoryKeyboard` - 键盘事件
- `EventCategoryMouse` - 鼠标事件
- `EventCategoryMouseButton` - 鼠标按钮事件

```cpp
// 检查事件是否属于某个分类
if (event.IsInCategory(EventCategoryKeyboard))
{
    // 这是一个键盘事件
}
```

## 扩展事件系统

### 添加新的事件类型

1. 在相应的头文件中定义新的事件类
2. 继承自Event基类
3. 使用EVENT_CLASS_TYPE和EVENT_CLASS_CATEGORY宏
4. 在EventType枚举中添加新的事件类型

```cpp
class CustomEvent : public Event
{
public:
    CustomEvent(int data) : m_Data(data) {}
    
    int GetData() const { return m_Data; }
    
    std::string ToString() const override
    {
        return "CustomEvent: " + std::to_string(m_Data);
    }
    
    EVENT_CLASS_TYPE(Custom) // 需要在EventType枚举中添加Custom
    EVENT_CLASS_CATEGORY(EventCategoryApplication)
    
private:
    int m_Data;
};
```

## 注意事项

1. 事件处理函数返回true表示事件已被处理，不会继续传播
2. 返回false表示事件未被处理，可以继续传播给其他处理器
3. 鼠标移动事件频率较高，建议谨慎处理以避免性能问题
4. 事件对象在处理完成后会被自动销毁，不要保存事件的引用 