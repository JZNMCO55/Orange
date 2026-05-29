// WorldPartition layer manifest 单元测试。
//
// 覆盖（重点是 MoveLayer reorder 数据语义 —— Editor Layers 面板 up/down
// 按钮的核心逻辑，可 headless 单测，与交互 UI 解耦）：
//   * 构造默认注册 "default" layer
//   * AddLayer 保持插入序；重复 id / 空 id 被拒
//   * MoveLayer 上移 / 下移 / 多步 clamp / 边界返回 false / 不存在返回 false
//   * MoveLayer 后 GetLayer(id) 仍能按新顺序正确解析（索引已重建）

#include <orange/engine/scene/WorldPartition.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

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

}  // namespace

int main()
{
    std::fprintf(stdout, "[WorldPartitionTest] running\n");
    TestDefaultAndAdd();
    TestMoveLayerBasic();
    TestMoveLayerClampAndBoundary();
    TestMoveLayerRebuildsIndex();
    std::fprintf(stdout, "[WorldPartitionTest] all tests passed.\n");
    return 0;
}
