#ifndef ORANGE_ENGINE_PHYSICS_PHYSICS_WORLD_H
#define ORANGE_ENGINE_PHYSICS_PHYSICS_WORLD_H

// ---------------------------------------------------------------------------
// PhysicsWorld —— "World → 一帧物理步进"的根入口（PIMPL）。
//
// 公共面**不**含任何 b2 类型——CLAUDE.md "Header isolation" 不变量约束
// `<box2d/...>` 只允许出现在 `src/physics/box2d/**`。Phase 4 / Task 05 是
// 接口阶段，.cpp 走 stub 实现（AddBody / RemoveBody 维护内表 + 反写 handle，
// Step 是 no-op）；Task 06 / Box2D 集成时把 .cpp 整体替换为真 b2World 路径，
// 公共面不动。
//
// 设计决策：让接口对 Box2D 之外的 2D physics（Chipmunk / Jolt 2D）也无破坏
// 性改动——所以
//   * BodyHandle 是不透明强类型，后端如何编码（e.g. b2BodyId 的 index +
//     generation）由后端自管；
//   * RigidBodyComponent / ColliderComponent 字段语义对齐 Box2D 3.x（因为它
//     是首选后端），但都是中性物理概念，其它 2D physics 也认；
//   * AddBody 一次提交"body + 单 collider"——多 collider per body 是 Box2D
//     特性，0.x 阶段 ECS 不下放（见 ColliderComponent 注释）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/physics/BodyHandle.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/RigidBodyComponent.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <memory>

namespace Orange::Engine::Physics
{

struct PhysicsWorldDesc
{
    glm::vec2 gravity{0.0f, -9.81f};

    // Box2D 3.x 用 substep（替换 2.x 的 velocity / position iterations）。
    // 4 是 b2 推荐入门值；高速碰撞 / 高质量比场景需要调到 8。
    std::uint32_t substepCount{4};
};

// PhysicsWorld 查询 / 写入 body 状态用的简单 POD pair。
// 与 ECS TransformComponent 解耦：Physics 只关心 2D xy 平面 + 弧度
// 朝向，3D Z 维度由消费方在 ECS 端自己保留 / 同步。
struct BodyTransform
{
    glm::vec2 position{0.0f, 0.0f};
    float     angle{0.0f};   // 弧度
};

class ORANGE_ENGINE_API PhysicsWorld
{
public:
    PhysicsWorld();
    explicit PhysicsWorld(const PhysicsWorldDesc& desc);
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&)            = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    // 推进一帧物理。dt = 本帧时间步（秒）。Phase 4 / Task 05 stub：本帧
    // 内置 step 计数 +1，body 状态不变；Task 06 接 b2World_Step 后真模拟。
    // dt < 0 当 0 处理；过大 dt 由后端自管（Box2D 内部不会自动 sub-clamp）。
    void Step(float dt);

    // 把一个 body + collider 注册进 world。返回值：
    //   * 成功 → 新分配的 BodyHandle（后续 RemoveBody / 查询用）；
    //   * 失败 → BodyHandle::Invalid()（descriptor 形态不合法，例如 polygon
    //     count == 0 / chain count < 2 等；本期 stub 不主动校验，永远成功）。
    // body / collider 按值拷一份进 world 内表；调用方原 component 不被修改
    // （RigidBodyComponent.handle 反写由调用方自己做：拿到返回值后 set 进
    // ECS component）——这条与 EnTT view 内"const &"取 component 的访问模
    // 式一致，避免 PhysicsWorld 暗中持有 ECS 句柄。
    BodyHandle AddBody(const RigidBodyComponent& body,
                       const ColliderComponent&  collider);

    // 移除 handle 对应的 body。idempotent：handle 无效 / 已被 remove → no-op。
    void RemoveBody(BodyHandle handle);

    // handle 是否仍指向 world 中已注册的 body。
    bool IsValid(BodyHandle handle) const noexcept;

    // 读取 body 当前位置 + 朝向（每帧 Step 之后更新；ECS sync 路径在
    // 主循环末尾扫一遍 dynamic body 把这里的值写回 TransformComponent）。
    // handle 无效 / 已 Remove → 返回零初始 BodyTransform。
    BodyTransform GetBodyTransform(BodyHandle handle) const noexcept;

    // 写入 body 位置 + 朝向（kinematic / 强制位移用）。
    // handle 无效 → no-op。
    void SetBodyTransform(BodyHandle handle, const BodyTransform& xf);

    // 读取 body 当前线性速度（自由落体 / 可玩验收的关键查询）。
    // handle 无效 → 返回零 vec2。
    glm::vec2 GetLinearVelocity(BodyHandle handle) const noexcept;

    // 写入 body 线性速度。handle 无效 → no-op。
    void SetLinearVelocity(BodyHandle handle, glm::vec2 velocity);

    // 原子地把 handle 对应 body 上的 collider 整体换成新 desc。
    //   * 旧 shape（含 chain segment）全部销毁；
    //   * 新 shape 按 collider 重新创建；
    //   * dynamic body 的 mass 自动重算（从新形状 + density 推出）。
    // 典型用法：角色变形 / 拾取大件物品 / 状态变身（"swallowing form"等）
    //         需要 collider 中途切换的场景。
    //
    // 调用时点：必须在 Step() 之外（"逻辑阶段"），不要在物理子步中途调；
    // Box2D 文档对此有同样要求。
    //
    // 返回 true 表示替换成功；handle 无效 / 新 shape 形态不合法（参考
    // CreateShapeFor 失败原因）→ 返回 false，body 上的 shape 状态在失败
    // 路径下"已清空但未重建"，此时 body 不参与任何碰撞——调用方需在
    // 失败后 RemoveBody 或重新 ReplaceFixture 一次。
    bool ReplaceFixture(BodyHandle handle, const ColliderComponent& collider);

    // 读取 body 当前总质量（kg）。dynamic body 由 collider density × 形状
    // 面积聚合而来；static / kinematic body 返回 0。handle 无效 → 返回 0。
    // 主要用于诊断 / 单测验证 ReplaceFixture 是否真触发了 mass 重算。
    float GetMass(BodyHandle handle) const noexcept;

    // 当前已注册 body 数（诊断 / 单测用）。
    std::size_t BodyCount() const noexcept;

    // PhysicsWorldDesc 访问。Task 06 起 b2World 重力 / substep 用此值；
    // Task 05 仅按值存储。
    const PhysicsWorldDesc& Desc() const noexcept;

    // 已发生的 Step 次数（诊断 / 单测用——证明 Step 真被调用，等 Task 06
    // 替换 stub 后用于"step 真发到 b2World 了吗"的回归测试）。
    std::size_t StepCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Physics

#endif  // ORANGE_ENGINE_PHYSICS_PHYSICS_WORLD_H
