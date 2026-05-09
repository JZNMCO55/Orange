// SaveGameSystem migrator chain 单元测试 —— Phase 5.5 / Task 05。
//
// 覆盖：
//   1. RegisterMigrator 输入校验 5 路径
//   2. 单跳迁移：写 v1，读 v2 注册 + v1→v2 migrator → 字段补齐
//   3. 多跳迁移：写 v1，读 v3 注册 + v1→v2 + v2→v3 → 链式补齐
//   4. 无 migrator 路径：写 v1，读 v3 不注册 migrator → SchemaMismatch
//      （Task 02 既有行为不破）
//   5. migrator 返回 false → InvalidArgument + 全部回滚
//   6. 环形 migrator (v1↔v2) 而 entry.version=v3 → 触发 hops cap →
//      SchemaMismatch
//   7. CanRead 直读路径不走 migrator（minor bump 不应触发链）
//
// 测试用临时文件放 std::filesystem::temp_directory_path()，跑完即删。

#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/save/SaveGameRegistry.h>
#include <orange/engine/save/SaveGameSystem.h>
#include <orange/engine/save/SaveableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

using Orange::Engine::Entity;
using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;
using Orange::Engine::ResultCode;
using Orange::Engine::SchemaVersion;
using Orange::Engine::World;
using Orange::Engine::Save::SaveableComponent;
using Orange::Engine::Save::SaveGameRegistry;
using Orange::Engine::Save::SaveGameSystem;

namespace
{

// 测试用 component —— 字段集合是 v3 schema 的全部字段。不同版本的 reader
// 仅消费其中一部分；migrator 负责补齐缺失字段。
struct PlayerInventory
{
    std::int64_t goldCoins{0};      // v1+
    std::int64_t spiritShards{0};   // v2+
    std::int64_t maxHp{0};          // v3+
};

const SchemaVersion kV1{"game/PlayerInventory", 1, 0};
const SchemaVersion kV2{"game/PlayerInventory", 2, 0};
const SchemaVersion kV3{"game/PlayerInventory", 3, 0};

// v1 writer：只写 goldCoins。
SaveGameRegistry::WriteFn<PlayerInventory> WriteV1()
{
    return [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
    {
        w.WriteInt(std::string(path) + "/goldCoins", c.goldCoins);
    };
}

// v3 reader：读全部 3 个字段。任一缺失即 false。
SaveGameRegistry::ReadFn<PlayerInventory> ReadV3()
{
    return [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
    {
        const std::string b(path);
        std::int64_t v = 0;
        if (!r.ReadInt(b + "/goldCoins",    v)) { return false; }
        c.goldCoins = v;
        if (!r.ReadInt(b + "/spiritShards", v)) { return false; }
        c.spiritShards = v;
        if (!r.ReadInt(b + "/maxHp",        v)) { return false; }
        c.maxHp = v;
        return true;
    };
}

// v3 writer：写全部字段（仅给 reader 注册时占位用，不真正调用）。
SaveGameRegistry::WriteFn<PlayerInventory> WriteV3()
{
    return [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
    {
        const std::string b(path);
        w.WriteInt(b + "/goldCoins",    c.goldCoins);
        w.WriteInt(b + "/spiritShards", c.spiritShards);
        w.WriteInt(b + "/maxHp",        c.maxHp);
    };
}

// v2 reader：读 goldCoins + spiritShards。
SaveGameRegistry::ReadFn<PlayerInventory> ReadV2()
{
    return [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
    {
        const std::string b(path);
        std::int64_t v = 0;
        if (!r.ReadInt(b + "/goldCoins",    v)) { return false; }
        c.goldCoins = v;
        if (!r.ReadInt(b + "/spiritShards", v)) { return false; }
        c.spiritShards = v;
        return true;
    };
}

SaveGameRegistry::WriteFn<PlayerInventory> WriteV2()
{
    return [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
    {
        const std::string b(path);
        w.WriteInt(b + "/goldCoins",    c.goldCoins);
        w.WriteInt(b + "/spiritShards", c.spiritShards);
    };
}

// v1 → v2：把 goldCoins 复制过去 + spiritShards 默认 0。
auto MigratorV1ToV2()
{
    return [](const JsonReader& src, std::string_view srcPath,
              JsonWriter& dst, std::string_view dstPath) -> bool
    {
        const std::string sb(srcPath);
        const std::string db(dstPath);
        std::int64_t v = 0;
        if (!src.ReadInt(sb + "/goldCoins", v)) { return false; }
        dst.WriteInt(db + "/goldCoins",    v);
        dst.WriteInt(db + "/spiritShards", 0);
        return true;
    };
}

// v2 → v3：透传 + maxHp 默认 100。
auto MigratorV2ToV3()
{
    return [](const JsonReader& src, std::string_view srcPath,
              JsonWriter& dst, std::string_view dstPath) -> bool
    {
        const std::string sb(srcPath);
        const std::string db(dstPath);
        std::int64_t v = 0;
        if (!src.ReadInt(sb + "/goldCoins",    v)) { return false; }
        dst.WriteInt(db + "/goldCoins", v);
        if (!src.ReadInt(sb + "/spiritShards", v)) { return false; }
        dst.WriteInt(db + "/spiritShards", v);
        dst.WriteInt(db + "/maxHp", 100);
        return true;
    };
}

// 空 reader / writer —— 只想测注册路径不实际跑 Save / Load 时用。
SaveGameRegistry::ReadFn<PlayerInventory> ReadNoop()
{
    return [](const JsonReader&, std::string_view, PlayerInventory&) -> bool { return true; };
}

void TagSaveable(World& world, Entity ent)
{
    world.Registry().emplace_or_replace<SaveableComponent>(World::ToEntt(ent));
}

std::filesystem::path MakeTempSavePath(const char* tag)
{
    auto base = std::filesystem::temp_directory_path();
    base /= std::string{"orange_engine_migrator_test_"} + tag + ".save";
    std::error_code ec;
    std::filesystem::remove(base, ec);
    return base;
}

void RemoveIfExists(const std::filesystem::path& p) noexcept
{
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

std::size_t CountSaveableEntities(const World& world)
{
    return world.Registry().view<const SaveableComponent>().size();
}

// 写一个 v1 schema 的 save file —— 共 5 个 case 都用同一份。返回 path
// 由调用方 Remove。
std::filesystem::path WriteV1Save(const char* tag, std::int64_t goldCoins)
{
    SaveGameRegistry writerReg;
    auto rc = writerReg.Register<PlayerInventory>(
        "PlayerInventory", kV1, WriteV1(),
        // Reader 在 Save 路径不被调用，但 Register 校验要求非空。
        [](const JsonReader&, std::string_view, PlayerInventory&) { return true; });
    assert(rc.IsOk());
    SaveGameSystem writerSys(writerReg);

    World world;
    Entity ent = world.CreateEntity();
    TagSaveable(world, ent);
    world.AddComponent(ent, PlayerInventory{goldCoins, 0, 0});

    auto path = MakeTempSavePath(tag);
    auto saveRc = writerSys.Save(world, path.string());
    assert(saveRc.IsOk());
    return path;
}

// ---------------------------------------------------------------------------
// 1. RegisterMigrator 输入校验
// ---------------------------------------------------------------------------
void TestRegisterMigratorValidation()
{
    SaveGameRegistry reg;
    auto regRc = reg.Register<PlayerInventory>(
        "PlayerInventory", kV3, WriteV3(), ReadV3());
    assert(regRc.IsOk());

    auto okFn = MigratorV1ToV2();

    // 未注册 component → NotFound
    {
        auto rc = reg.RegisterMigrator("DoesNotExist", kV1, kV2, okFn);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::NotFound);
    }
    // 空 componentName → InvalidArgument（先 reject 再 NotFound）
    {
        auto rc = reg.RegisterMigrator("", kV1, kV2, okFn);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }
    // 无效 from
    {
        auto rc = reg.RegisterMigrator("PlayerInventory", SchemaVersion{}, kV2, okFn);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }
    // 无效 to
    {
        auto rc = reg.RegisterMigrator("PlayerInventory", kV1, SchemaVersion{}, okFn);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }
    // from == to
    {
        auto rc = reg.RegisterMigrator("PlayerInventory", kV1, kV1, okFn);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }
    // 空 fn
    {
        auto rc = reg.RegisterMigrator("PlayerInventory", kV1, kV2, {});
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
    }
    // 第一次注册 v1→v2 OK
    {
        auto rc = reg.RegisterMigrator("PlayerInventory", kV1, kV2, okFn);
        assert(rc.IsOk());
    }
    // 同 component 同 from 重复 → AlreadyExists
    {
        auto rc = reg.RegisterMigrator("PlayerInventory", kV1, kV2, okFn);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::AlreadyExists);
    }
    // 同 component 不同 from 允许（构成 chain）
    {
        auto rc = reg.RegisterMigrator("PlayerInventory", kV2, kV3, MigratorV2ToV3());
        assert(rc.IsOk());
    }
}

// ---------------------------------------------------------------------------
// 2. 单跳迁移：写 v1，读 v2 + v1→v2 migrator
// ---------------------------------------------------------------------------
void TestSingleHopMigration()
{
    auto path = WriteV1Save("single_hop", /*goldCoins*/ 1234);

    SaveGameRegistry readerReg;
    auto rc1 = readerReg.Register<PlayerInventory>(
        "PlayerInventory", kV2, WriteV2(), ReadV2());
    assert(rc1.IsOk());
    auto rc2 = readerReg.RegisterMigrator(
        "PlayerInventory", kV1, kV2, MigratorV1ToV2());
    assert(rc2.IsOk());
    SaveGameSystem readerSys(readerReg);

    World world;
    auto loadRc = readerSys.Load(path.string(), world);
    assert(loadRc.IsOk());
    assert(CountSaveableEntities(world) == 1);

    auto view = world.Registry().view<const SaveableComponent>();
    Entity restored = World::FromEntt(*view.begin());
    auto* p = world.GetComponent<PlayerInventory>(restored);
    assert(p != nullptr);
    assert(p->goldCoins    == 1234);
    assert(p->spiritShards == 0);   // migrator 默认值
    assert(p->maxHp        == 0);   // 没参与迁移；POD 默认

    RemoveIfExists(path);
}

// ---------------------------------------------------------------------------
// 3. 多跳迁移：写 v1，读 v3 + v1→v2 + v2→v3 链
// ---------------------------------------------------------------------------
void TestMultiHopMigration()
{
    auto path = WriteV1Save("multi_hop", /*goldCoins*/ 4321);

    SaveGameRegistry readerReg;
    auto rc1 = readerReg.Register<PlayerInventory>(
        "PlayerInventory", kV3, WriteV3(), ReadV3());
    assert(rc1.IsOk());
    auto rc2 = readerReg.RegisterMigrator("PlayerInventory", kV1, kV2, MigratorV1ToV2());
    assert(rc2.IsOk());
    auto rc3 = readerReg.RegisterMigrator("PlayerInventory", kV2, kV3, MigratorV2ToV3());
    assert(rc3.IsOk());
    SaveGameSystem readerSys(readerReg);

    World world;
    assert(readerSys.Load(path.string(), world).IsOk());
    auto view = world.Registry().view<const SaveableComponent>();
    Entity restored = World::FromEntt(*view.begin());
    auto* p = world.GetComponent<PlayerInventory>(restored);
    assert(p != nullptr);
    assert(p->goldCoins    == 4321);   // 来自原始 v1 数据
    assert(p->spiritShards == 0);      // v1→v2 默认
    assert(p->maxHp        == 100);    // v2→v3 默认

    RemoveIfExists(path);
}

// ---------------------------------------------------------------------------
// 4. 无可用 migrator → SchemaMismatch（Task 02 既有行为不破）
// ---------------------------------------------------------------------------
void TestNoMigratorAvailable()
{
    auto path = WriteV1Save("no_migrator", /*goldCoins*/ 1);

    SaveGameRegistry readerReg;
    auto rc1 = readerReg.Register<PlayerInventory>(
        "PlayerInventory", kV3, WriteV3(), ReadV3());
    assert(rc1.IsOk());
    // 故意不注册任何 migrator
    SaveGameSystem readerSys(readerReg);

    World world;
    auto loadRc = readerSys.Load(path.string(), world);
    assert(loadRc.IsErr());
    assert(loadRc.Error() == ResultCode::SchemaMismatch);
    assert(CountSaveableEntities(world) == 0);   // 全回滚

    RemoveIfExists(path);
}

// ---------------------------------------------------------------------------
// 5. migrator 返回 false → InvalidArgument + 回滚
// ---------------------------------------------------------------------------
void TestMigratorReturnsFalse()
{
    auto path = WriteV1Save("migrator_fails", /*goldCoins*/ 1);

    SaveGameRegistry readerReg;
    auto rc1 = readerReg.Register<PlayerInventory>(
        "PlayerInventory", kV2, WriteV2(), ReadV2());
    assert(rc1.IsOk());
    auto rc2 = readerReg.RegisterMigrator(
        "PlayerInventory", kV1, kV2,
        [](const JsonReader&, std::string_view,
           JsonWriter&,        std::string_view) -> bool { return false; });
    assert(rc2.IsOk());
    SaveGameSystem readerSys(readerReg);

    World world;
    auto loadRc = readerSys.Load(path.string(), world);
    assert(loadRc.IsErr());
    assert(loadRc.Error() == ResultCode::InvalidArgument);
    assert(CountSaveableEntities(world) == 0);

    RemoveIfExists(path);
}

// ---------------------------------------------------------------------------
// 6. 环形 migrator (v1↔v2) 而 entry.version=v3 → 触发 hops cap
// ---------------------------------------------------------------------------
void TestCycleGuardHits()
{
    auto path = WriteV1Save("cycle", /*goldCoins*/ 1);

    SaveGameRegistry readerReg;
    auto rc1 = readerReg.Register<PlayerInventory>(
        "PlayerInventory", kV3, WriteV3(), ReadV3());
    assert(rc1.IsOk());
    // v1→v2 + v2→v1 两个 migrator 形成回环；entry.version=v3 永远不命中
    auto rc2 = readerReg.RegisterMigrator(
        "PlayerInventory", kV1, kV2,
        [](const JsonReader& src, std::string_view srcPath,
           JsonWriter& dst, std::string_view dstPath) -> bool
        {
            std::int64_t v = 0;
            (void)src.ReadInt(std::string(srcPath) + "/goldCoins", v);
            dst.WriteInt(std::string(dstPath) + "/goldCoins", v);
            dst.WriteInt(std::string(dstPath) + "/spiritShards", 0);
            return true;
        });
    assert(rc2.IsOk());
    auto rc3 = readerReg.RegisterMigrator(
        "PlayerInventory", kV2, kV1,
        [](const JsonReader& src, std::string_view srcPath,
           JsonWriter& dst, std::string_view dstPath) -> bool
        {
            std::int64_t v = 0;
            (void)src.ReadInt(std::string(srcPath) + "/goldCoins", v);
            dst.WriteInt(std::string(dstPath) + "/goldCoins", v);
            return true;
        });
    assert(rc3.IsOk());
    SaveGameSystem readerSys(readerReg);

    World world;
    auto loadRc = readerSys.Load(path.string(), world);
    assert(loadRc.IsErr());
    assert(loadRc.Error() == ResultCode::SchemaMismatch);
    assert(CountSaveableEntities(world) == 0);

    RemoveIfExists(path);
}

// ---------------------------------------------------------------------------
// 7. CanRead 直读路径不走 migrator —— minor bump 不触发链
// ---------------------------------------------------------------------------
void TestMinorBumpSkipsMigrator()
{
    // 写 schema v2.0；读 v2.1 + 一个 v1→v2 migrator（不应被触发）
    SaveGameRegistry writerReg;
    auto wRc = writerReg.Register<PlayerInventory>(
        "PlayerInventory", kV2, WriteV2(),
        [](const JsonReader&, std::string_view, PlayerInventory&) { return true; });
    assert(wRc.IsOk());
    SaveGameSystem writerSys(writerReg);

    World worldA;
    Entity ent = worldA.CreateEntity();
    TagSaveable(worldA, ent);
    worldA.AddComponent(ent, PlayerInventory{777, 5, 0});

    auto path = MakeTempSavePath("minor_bump");
    assert(writerSys.Save(worldA, path.string()).IsOk());

    SaveGameRegistry readerReg;
    auto rc1 = readerReg.Register<PlayerInventory>(
        "PlayerInventory",
        SchemaVersion{"game/PlayerInventory", 2, 1},   // minor 提升一档
        WriteV2(),
        ReadV2());
    assert(rc1.IsOk());
    bool migratorTriggered = false;
    auto rc2 = readerReg.RegisterMigrator(
        "PlayerInventory", kV1, kV2,
        [&](const JsonReader&, std::string_view,
            JsonWriter&,        std::string_view) -> bool
        {
            migratorTriggered = true;
            return true;
        });
    assert(rc2.IsOk());
    SaveGameSystem readerSys(readerReg);

    World worldB;
    assert(readerSys.Load(path.string(), worldB).IsOk());
    assert(!migratorTriggered);   // minor bump 走 CanRead 直通

    auto view = worldB.Registry().view<const SaveableComponent>();
    Entity restored = World::FromEntt(*view.begin());
    auto* p = worldB.GetComponent<PlayerInventory>(restored);
    assert(p != nullptr);
    assert(p->goldCoins    == 777);
    assert(p->spiritShards == 5);

    RemoveIfExists(path);
}

}  // namespace

int main()
{
    TestRegisterMigratorValidation();
    TestSingleHopMigration();
    TestMultiHopMigration();
    TestNoMigratorAvailable();
    TestMigratorReturnsFalse();
    TestCycleGuardHits();
    TestMinorBumpSkipsMigrator();

    std::printf("[save_game_migrator_test] all 7 cases passed\n");
    return 0;
}
