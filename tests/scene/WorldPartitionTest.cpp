// WorldPartition layer manifest 单元测试。
//
// 覆盖（重点是 MoveLayer reorder 数据语义 —— Editor Layers 面板 up/down
// 按钮的核心逻辑，可 headless 单测，与交互 UI 解耦）：
//   * 构造默认注册 "default" layer
//   * AddLayer 保持插入序；重复 id / 空 id 被拒
//   * MoveLayer 上移 / 下移 / 多步 clamp / 边界返回 false / 不存在返回 false
//   * MoveLayer 后 GetLayer(id) 仍能按新顺序正确解析（索引已重建）

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldPartition.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Scene::LayerInfo;
using Orange::Engine::Scene::WorldPartition;

namespace
{

// 取当前 manifest 的 id 顺序，便于断言比较。
std::vector<std::string> OrderOf(const WorldPartition& p)
{
    std::vector<std::string> ids;
    for (const auto& l : p.GetLayers())
    {
        ids.push_back(l.id);
    }
    return ids;
}

LayerInfo MakeLayer(const char* id)
{
    LayerInfo info;
    info.id          = id;
    info.displayName = id;
    info.visible     = true;
    return info;
}

void TestDefaultAndAdd()
{
    WorldPartition p;
    // 构造即有 default。
    assert(p.LayerCount() == 1);
    assert(p.HasLayer("default"));
    assert(OrderOf(p) == (std::vector<std::string>{"default"}));

    assert(p.AddLayer(MakeLayer("a")));
    assert(p.AddLayer(MakeLayer("b")));
    assert(p.AddLayer(MakeLayer("c")));
    assert(OrderOf(p) == (std::vector<std::string>{"default", "a", "b", "c"}));

    // 重复 id 拒绝；空 id 拒绝。
    assert(!p.AddLayer(MakeLayer("a")));
    assert(!p.AddLayer(MakeLayer("")));
    assert(p.LayerCount() == 4);

    std::fprintf(stdout, "  [PASS] default + AddLayer 插入序\n");
}

void TestMoveLayerBasic()
{
    WorldPartition p;
    p.AddLayer(MakeLayer("a"));
    p.AddLayer(MakeLayer("b"));
    p.AddLayer(MakeLayer("c"));
    // [default, a, b, c]

    // 下移 a 一步 → [default, b, a, c]
    assert(p.MoveLayer("a", +1));
    assert(OrderOf(p) == (std::vector<std::string>{"default", "b", "a", "c"}));

    // 上移 a 一步 → 回到 [default, a, b, c]
    assert(p.MoveLayer("a", -1));
    assert(OrderOf(p) == (std::vector<std::string>{"default", "a", "b", "c"}));

    // 上移 c 两步 → [default, c, a, b]
    assert(p.MoveLayer("c", -2));
    assert(OrderOf(p) == (std::vector<std::string>{"default", "c", "a", "b"}));

    std::fprintf(stdout, "  [PASS] MoveLayer 上移 / 下移 / 多步\n");
}

void TestMoveLayerClampAndBoundary()
{
    WorldPartition p;
    p.AddLayer(MakeLayer("a"));
    p.AddLayer(MakeLayer("b"));
    // [default, a, b]

    // delta 越界向后 → clamp 到末尾：default 移到最后。
    assert(p.MoveLayer("default", +100));
    assert(OrderOf(p) == (std::vector<std::string>{"a", "b", "default"}));

    // delta 越界向前 → clamp 到头：default 移回最前。
    assert(p.MoveLayer("default", -100));
    assert(OrderOf(p) == (std::vector<std::string>{"default", "a", "b"}));

    // 已在边界、净位移为 0 → 返回 false（不算移动）。
    assert(!p.MoveLayer("default", -1));  // 已是第一条
    assert(!p.MoveLayer("b", +1));        // 已是最后一条
    assert(OrderOf(p) == (std::vector<std::string>{"default", "a", "b"}));

    // delta == 0 → false；不存在的 id → false。
    assert(!p.MoveLayer("a", 0));
    assert(!p.MoveLayer("nonexistent", +1));
    assert(OrderOf(p) == (std::vector<std::string>{"default", "a", "b"}));

    std::fprintf(stdout, "  [PASS] MoveLayer clamp / 边界 / delta0 / 不存在\n");
}

void TestMoveLayerRebuildsIndex()
{
    WorldPartition p;
    p.AddLayer(MakeLayer("a"));
    p.AddLayer(MakeLayer("b"));
    p.AddLayer(MakeLayer("c"));

    // 给每条 layer 写个可区分的 displayName，move 后用 GetLayer 验证
    // 索引重建后仍指向正确条目（不是错位到别的 index）。
    p.GetLayer("a")->displayName = "AAA";
    p.GetLayer("c")->displayName = "CCC";

    assert(p.MoveLayer("c", -2));  // [default, c, a, b]
    assert(OrderOf(p) == (std::vector<std::string>{"default", "c", "a", "b"}));

    // 索引应已重建：GetLayer 按 id 仍取到正确条目。
    const LayerInfo* la = p.GetLayer("a");
    const LayerInfo* lc = p.GetLayer("c");
    assert(la != nullptr && la->displayName == "AAA");
    assert(lc != nullptr && lc->displayName == "CCC");
    // 且这两个指针对应 GetLayers() 里新顺序的相应槽位。
    assert(p.GetLayers()[1].id == "c" && p.GetLayers()[1].displayName == "CCC");
    assert(p.GetLayers()[2].id == "a" && p.GetLayers()[2].displayName == "AAA");

    std::fprintf(stdout, "  [PASS] MoveLayer 后索引重建 + GetLayer 解析正确\n");
}

// per-entity hidden override（editor-hide）—— 编辑器 Show/Hide 的核心数据
// 语义，可 headless 单测（与 ImGui 行渲染解耦）。覆盖：
//   * 默认非 hidden；SetEntityHidden(true) 后 IsEntityHidden 翻 true
//   * IsEntityVisible 对 hidden 实体返回 false——即使其 layer 是 visible
//   * Unhide 后恢复可见
//   * 与 layer.visible 正交：hidden 在 layer visible 时仍 false；layer
//     hidden 时无论 entity hidden 与否都 false（layer 优先级被 hidden 短路）
//   * SetEntityHidden 幂等（重复 true 不重复加；对未 hidden 的 false no-op）
//   * Invalid entity 被静默忽略
void TestEntityHiddenOverride()
{
    World          world;
    WorldPartition p;
    Entity         e = world.CreateEntity();

    // 默认：未 hidden，且（在 default visible layer 上）可见。
    assert(!p.IsEntityHidden(e));
    assert(p.IsEntityVisible(world, e));

    // 藏起来：IsEntityHidden 翻 true，IsEntityVisible 即便 layer visible 也 false。
    p.SetEntityHidden(e, true);
    assert(p.IsEntityHidden(e));
    assert(!p.IsEntityVisible(world, e));

    // 幂等：重复 true 不应使 unhide 失效（即不重复入集导致逻辑错乱）。
    p.SetEntityHidden(e, true);
    assert(p.IsEntityHidden(e));

    // 取消隐藏：恢复可见。
    p.SetEntityHidden(e, false);
    assert(!p.IsEntityHidden(e));
    assert(p.IsEntityVisible(world, e));

    // 对未 hidden 的实体再 false → no-op，仍可见。
    p.SetEntityHidden(e, false);
    assert(!p.IsEntityHidden(e));

    // 与 layer 正交：把 default layer 设为不可见，则无论 hidden 与否都不可见。
    p.SetLayerVisible("default", false);
    assert(!p.IsEntityVisible(world, e));        // layer 不可见
    p.SetEntityHidden(e, true);
    assert(!p.IsEntityVisible(world, e));         // 两者都不可见
    p.SetLayerVisible("default", true);
    assert(!p.IsEntityVisible(world, e));         // layer 恢复但 entity 仍 hidden
    p.SetEntityHidden(e, false);
    assert(p.IsEntityVisible(world, e));          // 全部恢复

    // Invalid entity 静默忽略：不崩、不误标 hidden。
    p.SetEntityHidden(Entity::Invalid(), true);
    assert(!p.IsEntityHidden(Entity::Invalid()));

    std::fprintf(stdout, "  [PASS] per-entity hidden override + layer 正交\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[WorldPartitionTest] running\n");
    TestDefaultAndAdd();
    TestMoveLayerBasic();
    TestMoveLayerClampAndBoundary();
    TestMoveLayerRebuildsIndex();
    TestEntityHiddenOverride();
    std::fprintf(stdout, "[WorldPartitionTest] all tests passed.\n");
    return 0;
}
