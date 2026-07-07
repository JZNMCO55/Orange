#ifndef ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H
#define ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H

// ---------------------------------------------------------------------------
// src 内部头（不安装到 include/）。
//
// 暴露内置组件调度表与 backend-dependent 组件的 Read 辅助函数。
// 公共类型（ComponentSerializerEntry / SaveContext / LoadContext 等）
// 已迁移到 include/orange/engine/scene/ComponentSerializerEntry.h；
// 本头仅保留内部实现所需的声明。
// ---------------------------------------------------------------------------

#include "orange/engine/scene/ComponentSerializerEntry.h"

#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine::Physics
{
    struct RigidBodyComponent;
    struct ColliderComponent;
} // namespace Orange::Engine::Physics

namespace Orange::Engine::Scene
{

    // ---------------------------------------------------------------------------
    // 资产路径虚拟化 / 反虚拟化辅助（M6 路径虚拟化）。
    //
    // 契约（详见 ComponentSerializerEntry.h 的字段注释）：虚拟路径（project:// 等
    // scheme）**只**存在于 scene JSON；内存态（AssetRegistry key / material id）始终是
    // real（cwd 相对）路径。故写端 real→virtual、读端 virtual→real，二者严格对称——
    // 任一侧漏包装都会破坏 round-trip（写虚拟却读不回真、或反之丢资产）。
    //
    // 两处 ctx 转换器空 = 恒等 = 零回归。空字符串（未设资产）一律原样透传，不虚拟化
    // （避免把 "" 变成 "project://"）。放在共享私有头里让 ComponentSerializers.cpp 与
    // SceneSerialization.cpp（clipSource 的读在后者的 Load Pass 2）都能复用同一份。
    inline std::string ToVirtual(const SaveContext& ctx, std::string_view real)
    {
        if (real.empty() || !ctx.assetPathToVirtual)
        {
            return std::string(real);
        }
        return ctx.assetPathToVirtual(real);
    }

    inline std::string FromVirtual(const LoadContext& ctx, std::string_view stored)
    {
        if (stored.empty() || !ctx.assetPathResolve)
        {
            return std::string(stored);
        }
        return ctx.assetPathResolve(stored);
    }

    const std::vector<ComponentSerializerEntry>& GetBuiltinComponentSerializers();

    // ---------------------------------------------------------------------------
    // Backend-dependent 组件的 Read 辅助函数。
    // SceneSerialization Pass 2 用这些 helper 把 desc 读到本地 var，
    // 再统一调用 PhysicsWorld::AddBody / AnimatorRegistry::Create 完成 backend 绑定。
    // 返回 false 表示数据格式坏（必填字段缺失 / 类型不匹配）。
    // ---------------------------------------------------------------------------

    bool ReadRigidBodyDesc(const JsonReader&            reader,
                           std::string_view             componentPath,
                           Physics::RigidBodyComponent& out);

    bool ReadColliderDesc(const JsonReader&           reader,
                          std::string_view            componentPath,
                          Physics::ColliderComponent& out);

    // Animator 的持久化数据仅是 backend 名字；具体初始化参数由游戏端在
    // AnimatorRegistry 注册 factory 时 capture，序列化层不下钻。
    bool ReadAnimatorBackendName(const JsonReader& reader,
                                 std::string_view  componentPath,
                                 std::string&      outBackendName);

    // "clip" backend 例外（B2.2）：ClipAnimator 的关键帧数据可纯数据化，故除
    // backend 名外额外以"形态 B"嵌入 clip 的 JSON 字符串（见 AnimationClipToJson）。
    // Load 端读出后 AnimationClipFromJson 重建 + SetTarget 指向 entity 自身 Transform，
    // 绕过 AnimatorRegistry（clip 数据 per-entity，不适合 factory 模型）。
    // componentPath 处无 clipJson 字段（非 clip backend / 旧 scene）→ 返回 false。
    bool ReadAnimatorClipJson(const JsonReader& reader,
                              std::string_view  componentPath,
                              std::string&      outClipJson);

    // "clip" backend 的资产引用变体（B2.6 改点 1）：clip 来自 .anim 资产时，scene 只存
    // 路径引用（clipSource）。componentPath 处无 clipSource → false（走 clipJson 内联路径）。
    bool ReadAnimatorClipSource(const JsonReader& reader,
                                std::string_view  componentPath,
                                std::string&      outClipSource);

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H
