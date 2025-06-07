# Orange Engine 技术文档

## 概述

Orange Engine 是一个现代化的C++游戏引擎，采用模块化设计，支持跨平台开发。引擎基于Vulkan图形API，提供了完整的应用程序框架、事件系统、数学库、内存管理等核心功能。

## 架构设计

Orange Engine 采用分层架构设计：

- **Layer1/Core**: 核心系统层，提供基础功能
- **Platform**: 平台抽象层，处理不同平台的差异
- **Graphics**: 图形渲染层，基于Vulkan API

## 核心模块

### 1. Application & Layer 系统

#### 1.1 Application 类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Application/Application.h/cpp`

Application 类是引擎的核心入口点，负责：
- 应用程序生命周期管理
- 窗口创建和管理
- 图形系统初始化
- 事件分发
- 主循环控制

**核心功能**:
```cpp
class Application
{
public:
    Application();
    virtual ~Application();
    
    void Run();                    // 主循环
    void Close();                  // 关闭应用程序
    void OnEvent(Event& e);        // 事件处理
    
    // 层级管理
    void PushLayer(const std::shared_ptr<Layer>& layer);
    void PushOverlay(const std::shared_ptr<Layer>& overlay);
    
    // 访问器
    Graphics::IGraphicsSystem* GetGraphicsSystem() const;
    Window* GetWindow() const;
    
    // 单例访问
    static Application& GetInstance();
};
```

**使用示例**:
```cpp
class MyApplication : public Orange::Core::Application
{
public:
    MyApplication()
    {
        auto gameLayer = std::make_shared<GameLayer>();
        PushLayer(gameLayer);
        
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

#### 1.2 Layer 系统

**文件位置**: `Src/OrangeEngine/Layer1/Core/Application/Layer.h/cpp`

Layer 系统提供模块化的功能组织方式：

```cpp
class Layer
{
public:
    Layer(const std::string& name = "Layer");
    virtual ~Layer() = default;
    
    virtual void OnAttach() {}     // 层级附加时调用
    virtual void OnDetach() {}     // 层级分离时调用
    virtual void OnUpdate() {}     // 每帧更新
    virtual void OnEvent(Event& event) {}  // 事件处理
    
    const std::string& GetName() const;
};
```

#### 1.3 LayerStack 管理

**文件位置**: `Src/OrangeEngine/Layer1/Core/Application/LayerStack.h/cpp`

LayerStack 负责管理层级的添加、移除和更新顺序：

```cpp
class LayerStack
{
public:
    void PushLayer(const std::shared_ptr<Layer>& layer);
    void PushOverlay(const std::shared_ptr<Layer>& overlay);
    void PopLayer(const std::shared_ptr<Layer>& layer);
    void PopOverlay(const std::shared_ptr<Layer>& overlay);
    
    // 迭代器支持
    std::vector<std::shared_ptr<Layer>>::iterator begin();
    std::vector<std::shared_ptr<Layer>>::iterator end();
};
```

### 2. 事件系统

#### 2.1 事件基类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Event/Event.h`

事件系统基于观察者模式，支持多种事件类型：

```cpp
enum class EventType
{
    None = 0,
    WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
    AppTick, AppUpdate, AppRender,
    KeyPressed, KeyReleased, KeyTyped,
    MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
};

class Event
{
public:
    virtual ~Event() = default;
    bool Handled = false;
    
    virtual EventType GetEventType() const = 0;
    virtual const char* GetName() const = 0;
    virtual int GetCategoryFlags() const = 0;
    virtual std::string ToString() const;
};
```

#### 2.2 事件分发器

```cpp
class EventDispatcher
{
public:
    EventDispatcher(Event& event);
    
    template<typename T, typename F>
    bool Dispatch(const F& func);
};
```

#### 2.3 具体事件类型

**窗口事件** (`ApplicationEvent.h`):
- `WindowCloseEvent`: 窗口关闭
- `WindowResizeEvent`: 窗口大小改变
- `WindowFocusEvent`: 窗口获得焦点
- `WindowLostFocusEvent`: 窗口失去焦点
- `WindowMovedEvent`: 窗口移动

**键盘事件** (`KeyEvent.h`):
- `KeyPressedEvent`: 按键按下
- `KeyReleasedEvent`: 按键释放
- `KeyTypedEvent`: 字符输入

**鼠标事件** (`MouseEvent.h`):
- `MouseMovedEvent`: 鼠标移动
- `MouseScrolledEvent`: 鼠标滚轮
- `MouseButtonPressedEvent`: 鼠标按钮按下
- `MouseButtonReleasedEvent`: 鼠标按钮释放

**使用示例**:
```cpp
void Application::OnEvent(Event& e)
{
    EventDispatcher dispatcher(e);
    
    dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event)
    {
        this->Close();
        return true;
    });
    
    dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& event)
    {
        if (event.GetKeyCode() == static_cast<int>(KeyCode::Escape))
        {
            this->Close();
            return true;
        }
        return false;
    });
}
```

### 3. 窗口系统

#### 3.1 Window 类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Application/Window.h/cpp`

Window 类封装了GLFW窗口管理：

```cpp
struct WindowProps
{
    std::string title;
    unsigned int width;
    unsigned int height;
    
    WindowProps(const std::string& title = "Orange Engine", 
                unsigned int width = 1280, 
                unsigned int height = 720);
};

class Window
{
public:
    Window(const WindowProps& props);
    virtual ~Window();
    
    virtual void OnUpdate();
    virtual uint32_t GetWidth() const noexcept;
    virtual uint32_t GetHeight() const noexcept;
    virtual std::string GetTitle() const noexcept;
    
    virtual void SetEventCallback(const EventCallbackFn& callback);
    virtual void* GetNativeWindow() const;
    virtual bool ShouldClose() const;
};
```

### 4. 数学库

#### 4.1 向量类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Math/Vector.h` 及相关实现文件

数学库基于GLM，提供Vector2、Vector3、Vector4类：

```cpp
class Vector3
{
public:
    Vector3();
    Vector3(float x, float y, float z);
    
    // 分量访问
    float X() const;
    float Y() const;
    float Z() const;
    void SetX(float x);
    void SetY(float y);
    void SetZ(float z);
    
    // 向量操作
    float Length() const;
    float LengthSquared() const;
    Vector3 Normalized() const;
    void Normalize();
    float Dot(const Vector3& other) const;
    Vector3 Cross(const Vector3& other) const;
    
    // 运算符重载
    Vector3 operator+(const Vector3& other) const;
    Vector3 operator-(const Vector3& other) const;
    Vector3 operator*(float scalar) const;
    Vector3 operator/(float scalar) const;
    
    // 常用向量
    static Vector3 Zero();
    static Vector3 One();
    static Vector3 Up();
    static Vector3 Forward();
    static Vector3 Right();
};
```

#### 4.2 矩阵类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Math/Matrix.h` 及相关实现文件

提供Matrix3和Matrix4类：

```cpp
class Matrix4
{
public:
    Matrix4();
    explicit Matrix4(float scalar);
    
    // 元素访问
    float Get(int row, int col) const;
    void Set(int row, int col, float value);
    
    // 矩阵操作
    Matrix4 Transpose() const;
    Matrix4 Inverse() const;
    float Determinant() const;
    
    // 变换矩阵创建
    static Matrix4 Identity();
    static Matrix4 Translation(const Vector3& translation);
    static Matrix4 Rotation(const Vector3& eulerAngles);
    static Matrix4 Scale(const Vector3& scale);
    
    // 视图和投影矩阵
    static Matrix4 LookAt(const Vector3& eye, const Vector3& center, const Vector3& up);
    static Matrix4 Perspective(float fov, float aspectRatio, float nearPlane, float farPlane);
    static Matrix4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
};
```

#### 4.3 四元数类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Math/Quaternion.h/cpp`

```cpp
class Quaternion
{
public:
    Quaternion();
    Quaternion(float x, float y, float z, float w);
    Quaternion(const Vector3& axis, float angleRadians);
    
    // 从欧拉角创建
    static Quaternion FromEulerAngles(const Vector3& eulerAngles);
    static Quaternion FromEulerAngles(float pitch, float yaw, float roll);
    
    // 转换
    Vector3 ToEulerAngles() const;
    Matrix3 ToMatrix3() const;
    Matrix4 ToMatrix4() const;
    
    // 四元数操作
    float Length() const;
    Quaternion Normalized() const;
    Quaternion Conjugate() const;
    Quaternion Inverse() const;
    Vector3 RotateVector(const Vector3& vec) const;
    
    // 插值
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);
    static Quaternion Lerp(const Quaternion& a, const Quaternion& b, float t);
};
```

#### 4.4 变换类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Math/Transform.h/cpp`

```cpp
class Transform
{
public:
    Transform();
    Transform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);
    
    // 属性访问
    Vector3 GetPosition() const;
    void SetPosition(const Vector3& position);
    Quaternion GetRotation() const;
    void SetRotation(const Quaternion& rotation);
    Vector3 GetScale() const;
    void SetScale(const Vector3& scale);
    
    // 变换操作
    void Translate(const Vector3& translation);
    void Rotate(const Quaternion& rotation);
    void Scale(const Vector3& scale);
    
    // 空间转换
    Vector3 TransformPoint(const Vector3& point) const;
    Vector3 TransformVector(const Vector3& vector) const;
    Vector3 TransformDirection(const Vector3& localDirection) const;
    
    // 矩阵转换
    Matrix4 GetMatrix() const;
    void SetFromMatrix(const Matrix4& matrix);
    
    // 插值
    static Transform Lerp(const Transform& a, const Transform& b, float t);
};
```

#### 4.5 几何类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Math/Geometry.h`

提供几何图形和碰撞检测：

```cpp
class Ray
{
public:
    Ray(const Vector3& origin, const Vector3& direction);
    
    Vector3 GetOrigin() const;
    Vector3 GetDirection() const;
    Vector3 GetPoint(float distance) const;
    Ray Transform(const Matrix4& matrix) const;
};

class AABB
{
public:
    AABB(const Vector3& min, const Vector3& max);
    
    Vector3 GetMin() const;
    Vector3 GetMax() const;
    Vector3 GetCenter() const;
    Vector3 GetSize() const;
    
    bool Contains(const Vector3& point) const;
    bool Intersects(const AABB& other) const;
};

class Sphere
{
public:
    Sphere(const Vector3& center, float radius);
    
    Vector3 GetCenter() const;
    float GetRadius() const;
    
    bool Contains(const Vector3& point) const;
    bool Intersects(const Sphere& other) const;
};

// 交点检测
class Intersection
{
public:
    static bool RayPlane(const Ray& ray, const Plane& plane, float& outDistance);
    static bool RaySphere(const Ray& ray, const Sphere& sphere, float& outDistance);
    static bool RayAABB(const Ray& ray, const AABB& aabb, float& outDistance);
};
```

### 5. 内存管理

#### 5.1 内存分配器接口

**文件位置**: `Src/OrangeEngine/Layer1/Core/Memory/MemoryAllocator.h`

```cpp
class MemoryAllocator
{
public:
    virtual ~MemoryAllocator() = default;
    
    virtual void* Allocate(size_t size, size_t alignment = 8) = 0;
    virtual void Deallocate(void* ptr) = 0;
    virtual void* Reallocate(void* ptr, size_t newSize, size_t alignment = 8) = 0;
    
    virtual const char* GetName() const = 0;
    virtual size_t GetTotalAllocated() const = 0;
    virtual size_t GetMaxAllocated() const = 0;
};
```

#### 5.2 内存池

**文件位置**: `Src/OrangeEngine/Layer1/Core/Memory/MemoryPool.h/cpp`

```cpp
class MemoryPool : public MemoryAllocator
{
public:
    MemoryPool(size_t blockSize, size_t initialBlocks = 16);
    
    void* Allocate(size_t size, size_t alignment = 8) override;
    void Deallocate(void* ptr) override;
    void* Reallocate(void* ptr, size_t newSize, size_t alignment = 8) override;
    
    size_t GetTotalAllocated() const override;
    size_t GetMaxAllocated() const override;
};
```

#### 5.3 内存泄漏检测

**文件位置**: `Src/OrangeEngine/Layer1/Core/Memory/MemoryLeakDetector.h/cpp`

```cpp
class MemoryLeakDetector
{
public:
    static MemoryLeakDetector& GetInstance();
    
    void TrackAllocation(void* ptr, size_t size, const char* file, int line);
    void TrackDeallocation(void* ptr);
    void ReportLeaks();
    void Reset();
};

// 调试模式下的宏
#ifdef _DEBUG
#define TRACK_ALLOCATION(ptr, size) \
    Orange::MemoryLeakDetector::GetInstance().TrackAllocation(ptr, size, __FILE__, __LINE__)
#define TRACK_DEALLOCATION(ptr) \
    Orange::MemoryLeakDetector::GetInstance().TrackDeallocation(ptr)
#endif
```

### 6. 文件系统

#### 6.1 FileSystem 类

**文件位置**: `Src/OrangeEngine/Layer1/Core/FileSystem/FileSystem.h/cpp`

```cpp
class FileSystem
{
public:
    // 文本文件操作
    static std::string ReadTextFile(const std::string& filename);
    
    // 二进制文件操作
    static std::vector<uint8_t> ReadBinaryFile(const std::string& filename);
    static std::vector<uint32_t> ReadBinaryFileAsUint32(const std::string& filename);
    
    // 文件检查
    static bool FileExists(const std::string& filename);
    static std::string GetFileExtension(const std::string& filename);
};
```

**使用示例**:
```cpp
// 读取着色器文件
std::string vertexShaderSource = FileSystem::ReadTextFile("shaders/vertex.glsl");

// 读取SPIR-V字节码
std::vector<uint32_t> spirvCode = FileSystem::ReadBinaryFileAsUint32("shaders/fragment.spv");

// 检查文件是否存在
if (FileSystem::FileExists("config.json"))
{
    std::string config = FileSystem::ReadTextFile("config.json");
}
```

### 7. 调试和日志系统

#### 7.1 Logger 类

**文件位置**: `Src/OrangeEngine/Layer1/Core/Debug/Logger.h/cpp`

```cpp
class Logger
{
public:
    static void Init();
    
    // 编译时格式检查接口
    template<typename... Args>
    static void LogError(std::format_string<Args...> fmt, Args&&... args);
    
    template<typename... Args>
    static void LogWarn(std::format_string<Args...> fmt, Args&&... args);
    
    template<typename... Args>
    static void LogInfo(std::format_string<Args...> fmt, Args&&... args);
    
    template<typename... Args>
    static void LogDebug(std::format_string<Args...> fmt, Args&&... args);
    
    template<typename... Args>
    static void LogTrace(std::format_string<Args...> fmt, Args&&... args);
    
    template<typename... Args>
    static void LogCritical(std::format_string<Args...> fmt, Args&&... args);
};

// 便捷宏定义
#ifdef ORANGE_DEBUG
#define ORG_LOG_ERROR(...) Orange::Logger::LogError(__VA_ARGS__)
#define ORG_LOG_WARN(...) Orange::Logger::LogWarn(__VA_ARGS__)
#define ORG_LOG_INFO(...) Orange::Logger::LogInfo(__VA_ARGS__)
#define ORG_LOG_DEBUG(...) Orange::Logger::LogDebug(__VA_ARGS__)
#define ORG_LOG_TRACE(...) Orange::Logger::LogTrace(__VA_ARGS__)
#define ORG_LOG_CRITICAL(...) Orange::Logger::LogCritical(__VA_ARGS__)
#endif
```

**使用示例**:
```cpp
Logger::Init();

ORG_LOG_INFO("Application started");
ORG_LOG_WARN("Low memory warning: {} MB remaining", memoryMB);
ORG_LOG_ERROR("Failed to load texture: {}", filename);
ORG_LOG_DEBUG("Player position: ({}, {}, {})", x, y, z);
```

### 8. 随机数生成

#### 8.1 Random 类

**文件位置**: `Src/OrangeEngine/Layer1/Core/RandomNum/Random.h/cpp`

```cpp
class Random
{
public:
    static Random& GetInstance();
    
    void Initialize(uint64_t seed = 0);
    
    // 基本随机数生成
    int32_t GetInt(int32_t min, int32_t max);
    float GetFloat(float min, float max);
    double GetDouble(double min, double max);
    bool GetBool();
    
    // 分布函数
    float GetNormal(float mean = 0.0f, float stddev = 1.0f);
    float GetUniform(float min = 0.0f, float max = 1.0f);
    
    uint64_t GetSeed() const;
};
```

**使用示例**:
```cpp
Random& rng = Random::GetInstance();
rng.Initialize(); // 使用当前时间作为种子

int diceRoll = rng.GetInt(1, 6);
float randomFloat = rng.GetFloat(0.0f, 1.0f);
bool coinFlip = rng.GetBool();
float gaussianValue = rng.GetNormal(0.0f, 1.0f);
```

## 设计模式和最佳实践

### 1. PIMPL 模式

引擎大量使用PIMPL（Pointer to Implementation）模式来：
- 隐藏实现细节
- 减少编译依赖
- 提供稳定的ABI

```cpp
class Vector3
{
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};
```

### 2. 单例模式

关键系统使用单例模式：
- Application
- Logger
- Random
- MemoryLeakDetector

### 3. 观察者模式

事件系统基于观察者模式，支持事件的分发和处理。

### 4. 工厂模式

图形系统使用工厂模式创建不同的图形API实现。

## 编译和构建

### 依赖项

- **GLM**: 数学库
- **GLFW**: 窗口和输入管理
- **Vulkan**: 图形API
- **C++20**: 现代C++特性支持

### 编译配置

引擎支持多种编译配置：
- `ORANGE_DEBUG`: 调试模式，启用所有日志和调试功能
- `ORANGE_RELEASE`: 发布模式，禁用调试日志但保留错误日志
- `ORANGE_DIST`: 分发模式，禁用所有日志

## 使用指南

### 1. 创建基本应用程序

```cpp
#include "OrangeEngine.h"

class MyGameLayer : public Orange::Core::Layer
{
public:
    MyGameLayer() : Layer("GameLayer") {}
    
    void OnAttach() override
    {
        ORG_LOG_INFO("Game layer attached");
    }
    
    void OnUpdate() override
    {
        // 游戏逻辑更新
    }
    
    void OnEvent(Orange::Core::Event& event) override
    {
        Orange::Core::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<Orange::Core::KeyPressedEvent>(
            [this](Orange::Core::KeyPressedEvent& e) -> bool
            {
                return OnKeyPressed(e);
            });
    }
    
private:
    bool OnKeyPressed(Orange::Core::KeyPressedEvent& e)
    {
        if (e.GetKeyCode() == static_cast<int>(Orange::Core::KeyCode::Space))
        {
            ORG_LOG_INFO("Space key pressed!");
            return true;
        }
        return false;
    }
};

class MyApplication : public Orange::Core::Application
{
public:
    MyApplication()
    {
        PushLayer(std::make_shared<MyGameLayer>());
    }
};

int main()
{
    Orange::Logger::Init();
    
    auto app = std::make_unique<MyApplication>();
    app->Run();
    
    return 0;
}
```

### 2. 数学库使用

```cpp
using namespace Orange::Math;

// 向量操作
Vector3 position(1.0f, 2.0f, 3.0f);
Vector3 direction = Vector3::Forward();
Vector3 result = position + direction * 5.0f;

// 矩阵变换
Matrix4 translation = Matrix4::Translation(position);
Matrix4 rotation = Matrix4::RotationY(DegToRad(45.0f));
Matrix4 scale = Matrix4::Scale(Vector3(2.0f, 2.0f, 2.0f));
Matrix4 transform = translation * rotation * scale;

// 四元数旋转
Quaternion quat = Quaternion::FromEulerAngles(DegToRad(30.0f), DegToRad(45.0f), 0.0f);
Vector3 rotatedVector = quat.RotateVector(Vector3::Up());

// 变换对象
Transform transform;
transform.SetPosition(Vector3(10.0f, 0.0f, 0.0f));
transform.SetRotation(quat);
transform.SetScale(Vector3(2.0f, 2.0f, 2.0f));

Vector3 worldPoint = transform.TransformPoint(Vector3::Zero());
```

### 3. 内存管理

```cpp
// 使用内存池
Orange::MemoryPool pool(sizeof(MyObject), 100);
void* ptr = pool.Allocate(sizeof(MyObject));
// 使用对象...
pool.Deallocate(ptr);

// 内存泄漏检测（调试模式）
#ifdef _DEBUG
void* debugPtr = malloc(100);
TRACK_ALLOCATION(debugPtr, 100);
// ... 使用内存
TRACK_DEALLOCATION(debugPtr);
free(debugPtr);
#endif
```

## 扩展和自定义

### 1. 添加自定义Layer

继承Layer类并实现所需的虚函数：

```cpp
class CustomLayer : public Orange::Core::Layer
{
public:
    CustomLayer() : Layer("CustomLayer") {}
    
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnEvent(Orange::Core::Event& event) override;
};
```

### 2. 添加自定义事件

```cpp
class CustomEvent : public Orange::Core::Event
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

### 3. 自定义内存分配器

```cpp
class CustomAllocator : public Orange::MemoryAllocator
{
public:
    void* Allocate(size_t size, size_t alignment = 8) override;
    void Deallocate(void* ptr) override;
    void* Reallocate(void* ptr, size_t newSize, size_t alignment = 8) override;
    
    const char* GetName() const override { return "CustomAllocator"; }
    size_t GetTotalAllocated() const override;
    size_t GetMaxAllocated() const override;
};
```

## 性能优化建议

### 1. 内存管理
- 使用内存池减少内存分配开销
- 避免频繁的动态内存分配
- 使用对象池重用对象

### 2. 事件处理
- 避免在高频事件（如鼠标移动）中进行重计算
- 使用事件的Handled标志避免不必要的处理
- 合理组织Layer的优先级

### 3. 数学计算
- 缓存计算结果，避免重复计算
- 使用四元数进行旋转操作
- 利用SIMD指令优化（GLM内部支持）

### 4. 文件I/O
- 异步加载大文件
- 使用二进制格式减少解析开销
- 实现资源缓存机制

## 调试和测试

### 1. 日志系统
- 使用不同级别的日志进行调试
- 在发布版本中禁用调试日志
- 使用格式化字符串提供详细信息

### 2. 内存调试
- 启用内存泄漏检测
- 定期检查内存使用情况
- 使用内存分析工具

### 3. 单元测试
引擎提供了基本的测试框架，位于`UnitTesting`目录下。

## 总结

Orange Engine 提供了一个完整的游戏引擎框架，具有以下特点：

- **模块化设计**: 清晰的模块分离，易于维护和扩展
- **现代C++**: 使用C++20特性，提供类型安全和性能优化
- **跨平台支持**: 基于标准库和跨平台库构建
- **内存安全**: 完善的内存管理和泄漏检测
- **事件驱动**: 灵活的事件系统支持复杂的交互逻辑
- **数学库**: 完整的3D数学支持，基于成熟的GLM库
- **调试友好**: 完善的日志系统和调试工具

引擎适合用于开发各种类型的游戏和图形应用程序，提供了从底层系统到高层抽象的完整解决方案。
