# OrangeEngine 编码规范（C++）

本文档定义 OrangeEngine 仓库 C++ 代码的命名与头文件约定。**规范主体与 OrangeRender 完全一致**，权威定义见 [`vendor/OrangeRender/.cursor/rules/orange-cpp-coding-standards.mdc`](../vendor/OrangeRender/.cursor/rules/orange-cpp-coding-standards.mdc) 与 [`vendor/OrangeRender/docs/coding-standards.md`](../vendor/OrangeRender/docs/coding-standards.md)。

本文件只列出 **OrangeEngine 相对 OrangeRender 的差异（命名空间、guard 前缀、API 宏）**，其余约定（命名风格、成员前缀、指针前缀、大括号对齐、include guard 形式等）一律遵循 OrangeRender。

---

## 命名空间

- **库级公共 API 使用 `Orange::Engine` 命名空间**（OrangeRender 用 `Orange`；OrangeEngine 用 `Orange::Engine` 以避免符号冲突）
- 子命名空间按模块嵌套：`Orange::Engine::Render` / `Orange::Engine::Scene` / `Orange::Engine::Animation` / `Orange::Engine::Physics` / `Orange::Engine::Audio` / `Orange::Engine::Input` / `Orange::Engine::Asset` / `Orange::Engine::Platform` / `Orange::Engine::Core`
- 顶层频繁使用的类型（`World`、`Entity`、`AppHost`、`FrameContext`、`Layer`）放在 `Orange::Engine` 直接命名空间，不再下沉到子命名空间，便于游戏代码引用

```cpp
namespace Orange::Engine
{

class AppHost
{
    // ...
};

}  // namespace Orange::Engine

namespace Orange::Engine::Render
{

class Pipeline
{
    // ...
};

}  // namespace Orange::Engine::Render
```

## Include guard 前缀

- 所有公共头使用 `ORANGE_ENGINE_<UPPER_PATH>_H` 形式（OrangeRender 用 `ORANGE_<UPPER_PATH>_H`）
- 路径以 `include/` 之后为起点，下划线分隔

| 文件 | Guard |
| --- | --- |
| `include/orange/engine/core/Result.h` | `ORANGE_ENGINE_CORE_RESULT_H` |
| `include/orange/engine/scene/World.h` | `ORANGE_ENGINE_SCENE_WORLD_H` |
| `include/orange/engine/render/Pipeline.h` | `ORANGE_ENGINE_RENDER_PIPELINE_H` |
| `include/orange/engine/animation/IAnimator.h` | `ORANGE_ENGINE_ANIMATION_I_ANIMATOR_H` |
| `include/orange/engine/OrangeEngineExport.h` | `ORANGE_ENGINE_ORANGE_ENGINE_EXPORT_H` |

```cpp
#ifndef ORANGE_ENGINE_SCENE_WORLD_H
#define ORANGE_ENGINE_SCENE_WORLD_H

namespace Orange::Engine
{

class World
{
    // ...
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_SCENE_WORLD_H
```

不使用 `#pragma once`（与 OrangeRender 保持一致）。

## API 导出宏

- 使用 `ORANGE_ENGINE_API`（OrangeRender 用 `ORANGE_API`）
- 静态库默认空展开，dll 时切换 dllexport/dllimport
- 任何被外部消费者直接构造或调用的类必须加 `ORANGE_ENGINE_API`
- 内部 helper 类型不加，避免 dll 边界问题扩散

```cpp
// include/orange/engine/OrangeEngineExport.h
#ifndef ORANGE_ENGINE_ORANGE_ENGINE_EXPORT_H
#define ORANGE_ENGINE_ORANGE_ENGINE_EXPORT_H

#if defined(ORANGE_ENGINE_BUILD_SHARED)
    #if defined(_WIN32)
        #if defined(ORANGE_ENGINE_BUILD_LIBRARY)
            #define ORANGE_ENGINE_API __declspec(dllexport)
        #else
            #define ORANGE_ENGINE_API __declspec(dllimport)
        #endif
    #else
        #define ORANGE_ENGINE_API __attribute__((visibility("default")))
    #endif
#else
    #define ORANGE_ENGINE_API
#endif

#endif  // ORANGE_ENGINE_ORANGE_ENGINE_EXPORT_H
```

```cpp
// include/orange/engine/scene/World.h
namespace Orange::Engine
{

class ORANGE_ENGINE_API World
{
public:
    World();
    ~World();
    // ...
};

}  // namespace Orange::Engine
```

## 模块文件夹布局

每个模块的公共头在 `include/orange/engine/<module>/`，私有实现在 `src/<module>/`。第三方 runtime 适配独占子目录：

```text
include/orange/engine/animation/IAnimator.h        ← 公共抽象
src/animation/dragonbones/DragonBonesAnimator.cpp  ← 私有适配（唯一允许 #include <dragonBones/...> 的位置）
```

## 公共头依赖收紧（项目级 invariant）

公共头**禁止** `#include`：
- `<orange/...>`（OrangeRender RHI / RenderGraph 头）
- `<box2d/...>`、`<box2d.h>`
- `<dragonBones/...>`
- `"miniaudio.h"`
- `<vulkan/...>`、`<volk.h>`、`<vk_mem_alloc.h>`

公共头**允许** `#include`：
- `<glm/...>`（数学类型透传）
- `<entt/...>`（公共组件类型必须出现 EnTT 类型）
- `<nlohmann/json.hpp>`（schema 公共字段使用）
- 标准库 / `<orange/engine/...>` 自身

具体强制规则在 `docs/extension-points.md` "越界禁令" 一节给出。

## 反面示例

```cpp
// ❌ 命名空间漏到全局；guard 前缀未带 ENGINE
#ifndef ORANGE_SCENE_WORLD_H
#define ORANGE_SCENE_WORLD_H

class World;  // 应在 Orange::Engine 命名空间内

#endif

// ❌ 公共头泄露 Box2D
#ifndef ORANGE_ENGINE_PHYSICS_RIGID_BODY_H
#define ORANGE_ENGINE_PHYSICS_RIGID_BODY_H

#include <box2d/b2_body.h>     // ★ 违反：公共头不许出现 Box2D
namespace Orange::Engine::Physics
{
class RigidBodyComponent
{
    b2Body* mpBody;            // ★ 违反同上
};
}  // namespace Orange::Engine::Physics

#endif

// ✅ 正确做法：用 PIMPL 隐藏后端
namespace Orange::Engine::Physics
{
class ORANGE_ENGINE_API RigidBodyComponent
{
public:
    void ApplyImpulse(const Vec2& impulse);
    Vec2 GetVelocity() const;

private:
    struct Impl;
    Impl* mpImpl{nullptr};   // 私有实现指针，Box2D 类型在 .cpp 内
};
}  // namespace Orange::Engine::Physics
```
