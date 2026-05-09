// SaveGameRegistry 单元测试 —— 覆盖 Phase 5.5 / Task 01 公共面的全部路
// 径：注册成功 / 输入校验失败 / 重名拒绝 / type-erased entry 端到端
// roundtrip / 注册顺序稳定。
//
// 走 `<cassert>` + 独立 main() 模式，与 OrangeEngine 既有测试约定一致。
// JSON 路径用 Core::Serialization 的 JsonWriter / JsonReader——本测试不
// 直接 include nlohmann。

#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/save/SaveGameRegistry.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

using Orange::Engine::Entity;
using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;
using Orange::Engine::ResultCode;
using Orange::Engine::SchemaVersion;
using Orange::Engine::World;
using Orange::Engine::Save::SaveGameComponentEntry;
using Orange::Engine::Save::SaveGameRegistry;

namespace
{

// 测试用 component —— 故意走最小 POD 字段，让 read/write 实现一目了然。
struct PlayerInventory
{
    std::int64_t goldCoins{0};
    std::int64_t spiritShards{0};
};

// 第二个 component 用来证明多注册的顺序稳定 + 互不串扰。
struct CheckpointTag
{
    std::int64_t checkpointId{0};
};

// 复用的 typed callbacks —— 让多个 test case 共用同一份 read/write，避
// 免每处都重写一遍。
SaveGameRegistry::WriteFn<PlayerInventory> InventoryWriter()
{
    return [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
    {
        const std::string base(path);
        w.WriteInt(base + "/goldCoins",    c.goldCoins);
        w.WriteInt(base + "/spiritShards", c.spiritShards);
    };
}

SaveGameRegistry::ReadFn<PlayerInventory> InventoryReader()
{
    return [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
    {
        const std::string base(path);
        std::int64_t v = 0;
        if (!r.ReadInt(base + "/goldCoins",    v)) { return false; }
        c.goldCoins = v;
        if (!r.ReadInt(base + "/spiritShards", v)) { return false; }
        c.spiritShards = v;
        return true;
    };
}

SaveGameRegistry::WriteFn<CheckpointTag> CheckpointWriter()
{
    return [](JsonWriter& w, std::string_view path, const CheckpointTag& c)
    {
        const std::string base(path);
        w.WriteInt(base + "/checkpointId", c.checkpointId);
    };
}

SaveGameRegistry::ReadFn<CheckpointTag> CheckpointReader()
{
    return [](const JsonReader& r, std::string_view path, CheckpointTag& c) -> bool
    {
        const std::string base(path);
        std::int64_t v = 0;
        if (!r.ReadInt(base + "/checkpointId", v)) { return false; }
        c.checkpointId = v;
        return true;
    };
}

// ---------------------------------------------------------------------------
// 1. 空 registry：Size / Empty / Find / Entries 形态正确。
// ---------------------------------------------------------------------------
void TestEmptyRegistry()
{
    SaveGameRegistry registry;
    assert(registry.Empty());
    assert(registry.Size() == 0);
    assert(registry.Find("Anything") == nullptr);
    assert(registry.Entries().empty());
}

// ---------------------------------------------------------------------------
// 2. 单次注册：成功 + Find 命中 + entry 三件套字段非空。
// ---------------------------------------------------------------------------
void TestSingleRegistration()
{
    SaveGameRegistry registry;
    auto rc = registry.Register<PlayerInventory>(
        "PlayerInventory",
        SchemaVersion{"game/PlayerInventory", 1, 0},
        InventoryWriter(),
        InventoryReader());
    assert(rc.IsOk());

    assert(!registry.Empty());
    assert(registry.Size() == 1);

    const auto* entry = registry.Find("PlayerInventory");
    assert(entry != nullptr);
    assert(entry->name == "PlayerInventory");
    assert(entry->version.Namespace() == "game/PlayerInventory");
    assert(entry->version.Major() == 1);
    assert(entry->version.Minor() == 0);
    assert(static_cast<bool>(entry->Has));
    assert(static_cast<bool>(entry->Write));
    assert(static_cast<bool>(entry->Read));
}

// ---------------------------------------------------------------------------
// 3. 输入校验：name 空 / version invalid / 回调空 → 都返回 InvalidArgument。
// ---------------------------------------------------------------------------
void TestInvalidArguments()
{
    SaveGameRegistry registry;

    // name 空
    {
        auto rc = registry.Register<PlayerInventory>(
            "",
            SchemaVersion{"game/PlayerInventory", 1, 0},
            InventoryWriter(),
            InventoryReader());
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }

    // version 无效（默认构造 → namespace 为空 → IsValid()=false）
    {
        auto rc = registry.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{},
            InventoryWriter(),
            InventoryReader());
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }

    // write 空
    {
        auto rc = registry.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{"game/PlayerInventory", 1, 0},
            {},                       // 空 std::function
            InventoryReader());
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }

    // read 空
    {
        auto rc = registry.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{"game/PlayerInventory", 1, 0},
            InventoryWriter(),
            {});                      // 空 std::function
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }

    // 校验失败时 registry 仍空
    assert(registry.Empty());
}

// ---------------------------------------------------------------------------
// 4. 重名注册：第二次同名注册返回 AlreadyExists；原 entry 不被替换。
// ---------------------------------------------------------------------------
void TestDuplicateName()
{
    SaveGameRegistry registry;

    auto rc1 = registry.Register<PlayerInventory>(
        "PlayerInventory",
        SchemaVersion{"game/PlayerInventory", 1, 0},
        InventoryWriter(),
        InventoryReader());
    assert(rc1.IsOk());

    auto rc2 = registry.Register<PlayerInventory>(
        "PlayerInventory",
        SchemaVersion{"game/PlayerInventory", 2, 0},   // 故意改不同 version
        InventoryWriter(),
        InventoryReader());
    assert(rc2.IsErr());
    assert(rc2.Error() == ResultCode::AlreadyExists);

    // 原 entry 仍是 v1.0 —— 不被替换
    const auto* entry = registry.Find("PlayerInventory");
    assert(entry != nullptr);
    assert(entry->version.Major() == 1);
    assert(registry.Size() == 1);
}

// ---------------------------------------------------------------------------
// 5. 注册顺序稳定：Entries() 顺序 == 注册顺序。后续 Save 主流程依赖这
//    个顺序产出 diff 友好的 JSON。
// ---------------------------------------------------------------------------
void TestEntriesOrder()
{
    SaveGameRegistry registry;
    auto rc1 = registry.Register<PlayerInventory>(
        "PlayerInventory",
        SchemaVersion{"game/PlayerInventory", 1, 0},
        InventoryWriter(),
        InventoryReader());
    assert(rc1.IsOk());

    auto rc2 = registry.Register<CheckpointTag>(
        "CheckpointTag",
        SchemaVersion{"game/CheckpointTag", 1, 0},
        CheckpointWriter(),
        CheckpointReader());
    assert(rc2.IsOk());

    const auto& entries = registry.Entries();
    assert(entries.size() == 2);
    assert(entries[0].name == "PlayerInventory");
    assert(entries[1].name == "CheckpointTag");
}

// ---------------------------------------------------------------------------
// 6. type-erased entry 端到端 roundtrip：在一个 World 里给 entity 装上
//    PlayerInventory，调 entry.Has → entry.Write 得到 JSON；从 JSON 反
//    序列化到第二个 World 的另一个 entity，验证字段还原。
// ---------------------------------------------------------------------------
void TestRoundtripThroughEntry()
{
    SaveGameRegistry registry;
    auto rc = registry.Register<PlayerInventory>(
        "PlayerInventory",
        SchemaVersion{"game/PlayerInventory", 1, 0},
        InventoryWriter(),
        InventoryReader());
    assert(rc.IsOk());

    const auto* entry = registry.Find("PlayerInventory");
    assert(entry != nullptr);

    // ---- Source 端：World A，给 entity 挂上 PlayerInventory，调 entry.Has +
    //       entry.Write 把字段写到 JsonWriter。
    World worldA;
    Entity entA = worldA.CreateEntity();
    PlayerInventory inv{};
    inv.goldCoins    = 1234;
    inv.spiritShards = 7;
    worldA.AddComponent(entA, inv);

    assert(entry->Has(worldA, entA));
    // 没装 component 的 entity Has() 应返回 false
    Entity emptyEnt = worldA.CreateEntity();
    assert(!entry->Has(worldA, emptyEnt));

    JsonWriter writer;
    const std::string path = "save/0/components/PlayerInventory";
    entry->Write(writer, path, worldA, entA);

    // ---- 把 JSON 文本捞出来，再喂给 JsonReader（FromString）。
    const std::string text = writer.Dump();
    auto readerResult = JsonReader::FromString(text);
    assert(readerResult.IsOk());
    JsonReader reader = std::move(readerResult).Value();

    // ---- Sink 端：World B 的另一个 entity，调 entry.Read 反序列化。
    World worldB;
    Entity entB = worldB.CreateEntity();
    const bool readOk = entry->Read(reader, path, worldB, entB);
    assert(readOk);

    const auto* restored = worldB.GetComponent<PlayerInventory>(entB);
    assert(restored != nullptr);
    assert(restored->goldCoins    == 1234);
    assert(restored->spiritShards == 7);

    // ---- 退化路径：JSON 缺字段 → entry.Read 返回 false（不污染 World）
    JsonWriter brokenWriter;
    brokenWriter.WriteInt(path + "/goldCoins", 999);  // 故意只写一半
    auto brokenReaderResult = JsonReader::FromString(brokenWriter.Dump());
    assert(brokenReaderResult.IsOk());
    JsonReader brokenReader = std::move(brokenReaderResult).Value();

    World worldC;
    Entity entC = worldC.CreateEntity();
    const bool brokenReadOk = entry->Read(brokenReader, path, worldC, entC);
    assert(!brokenReadOk);
    // entity 上不应留下半成品 component（typed Read 失败时 .h 模板里
    // 不会调用 AddComponent）
    assert(worldC.GetComponent<PlayerInventory>(entC) == nullptr);
}

}  // namespace

int main()
{
    TestEmptyRegistry();
    TestSingleRegistration();
    TestInvalidArguments();
    TestDuplicateName();
    TestEntriesOrder();
    TestRoundtripThroughEntry();

    std::printf("[save_game_registry_test] all 6 cases passed\n");
    return 0;
}
