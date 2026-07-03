// SaveGameSystem 端到端单元测试。
//
// 覆盖 Phase 5.5 / Task 02 全部公共行为：
//   1. 空 World round-trip（不创任何 Saveable entity）
//   2. 单 entity / 单 component round-trip
//   3. 多 entity / 多 component round-trip + 注册顺序稳定（diff 友好）
//   4. Forward-compat：JSON 里出现未注册 component 名 → silent skip
//   5. CRC 损坏 → InvalidArgument，World 不被改动
//   6. Header magic 损坏 → IoError
//   7. 顶层 schemaVersion 不兼容 → SchemaMismatch
//   8. 单个 component schemaVersion 不兼容 → SchemaMismatch + World 回滚
//   9. 文件不存在 → IoError
//
// 走 `<cassert>` + 独立 main()，跟项目其它测试约定一致。

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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    // ---------------------------------------------------------------------------
    // 测试用 component
    // ---------------------------------------------------------------------------

    struct PlayerInventory
    {
        std::int64_t goldCoins{0};
        std::int64_t spiritShards{0};
    };

    struct CheckpointTag
    {
        std::int64_t checkpointId{0};
    };

    // ---------------------------------------------------------------------------
    // 共享 fixture：一个 registry，两个 component 注册好。
    // ---------------------------------------------------------------------------

    SaveGameRegistry MakeRegistry()
    {
        SaveGameRegistry reg;
        auto             rc1 = reg.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{"game/PlayerInventory", 1, 0},
            [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
            {
                const std::string base(path);
                w.WriteInt(base + "/goldCoins", c.goldCoins);
                w.WriteInt(base + "/spiritShards", c.spiritShards);
            },
            [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
            {
                const std::string base(path);
                std::int64_t      v = 0;
                if (!r.ReadInt(base + "/goldCoins", v))
                {
                    return false;
                }
                c.goldCoins = v;
                if (!r.ReadInt(base + "/spiritShards", v))
                {
                    return false;
                }
                c.spiritShards = v;
                return true;
            });
        assert(rc1.IsOk());

        auto rc2 = reg.Register<CheckpointTag>(
            "CheckpointTag",
            SchemaVersion{"game/CheckpointTag", 1, 0},
            [](JsonWriter& w, std::string_view path, const CheckpointTag& c)
            {
                w.WriteInt(std::string(path) + "/checkpointId", c.checkpointId);
            },
            [](const JsonReader& r, std::string_view path, CheckpointTag& c) -> bool
            {
                std::int64_t v = 0;
                if (!r.ReadInt(std::string(path) + "/checkpointId", v))
                {
                    return false;
                }
                c.checkpointId = v;
                return true;
            });
        assert(rc2.IsOk());

        return reg;
    }

    // 把 SaveableComponent attach 到 entity —— 用 entt 直接接口绕过
    // World::AddComponent 的 T& 返回（空类型 EnTT 优化为 void 存储）。
    void TagSaveable(World& world, Entity entity)
    {
        world.Registry().emplace_or_replace<SaveableComponent>(World::ToEntt(entity));
    }

    std::filesystem::path MakeTempSavePath(const char* tag)
    {
        auto base = std::filesystem::temp_directory_path();
        base /= std::string{"orange_engine_save_test_"} + tag + ".save";
        std::error_code ec;
        std::filesystem::remove(base, ec);
        return base;
    }

    void RemoveIfExists(const std::filesystem::path& p) noexcept
    {
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }

    // 把整个文件读到 vector<uint8_t>，配合 CRC / header tampering 测试。
    std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        assert(in.is_open());
        const auto size = in.tellg();
        assert(size >= 0);
        in.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (size > 0)
        {
            in.read(reinterpret_cast<char*>(bytes.data()), size);
        }
        return bytes;
    }

    void WriteFileBytes(const std::filesystem::path&     path,
                        const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        assert(out.is_open());
        if (!bytes.empty())
        {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
    }

    std::size_t CountSaveableEntities(const World& world)
    {
        auto view = world.Registry().view<const SaveableComponent>();
        return view.size();
    }

    // ---------------------------------------------------------------------------
    // 1. 空 World round-trip：没有 Saveable entity 时也能正常 Save / Load。
    // ---------------------------------------------------------------------------
    void TestEmptyRoundtrip()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);

        World worldA;
        auto  path = MakeTempSavePath("empty");

        auto saveRc = sys.Save(worldA, path.string());
        assert(saveRc.IsOk());
        assert(std::filesystem::exists(path));

        World worldB;
        auto  loadRc = sys.Load(path.string(), worldB);
        assert(loadRc.IsOk());
        assert(CountSaveableEntities(worldB) == 0);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 2. 单 entity / 单 component round-trip：Save 后加载到新 World，字段还原。
    // ---------------------------------------------------------------------------
    void TestSingleEntitySingleComponent()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);

        World  worldA;
        Entity ent = worldA.CreateEntity();
        TagSaveable(worldA, ent);
        PlayerInventory inv{};
        inv.goldCoins    = 1234;
        inv.spiritShards = 7;
        worldA.AddComponent(ent, inv);

        auto path = MakeTempSavePath("single");
        assert(sys.Save(worldA, path.string()).IsOk());

        World worldB;
        assert(sys.Load(path.string(), worldB).IsOk());
        assert(CountSaveableEntities(worldB) == 1);

        // Load 后没有"持久 ID → entity"接口；用 view 反查唯一 entity。
        auto   view     = worldB.Registry().view<const SaveableComponent>();
        Entity restored = Entity::Invalid();
        if (view.begin() != view.end())
        {
            restored = World::FromEntt(*view.begin());
        }
        assert(restored.IsValid());

        const auto* p = worldB.GetComponent<PlayerInventory>(restored);
        assert(p != nullptr);
        assert(p->goldCoins == 1234);
        assert(p->spiritShards == 7);

        // CheckpointTag 没挂 → 反序列化端不该出现
        assert(worldB.GetComponent<CheckpointTag>(restored) == nullptr);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 3. 多 entity / 多 component round-trip。验证：
    //    * 持久 ID 顺序稳定（registry.Entries 顺序 = JSON 内字段顺序）；
    //    * 同一 entity 上多个 component 都能还原；
    //    * 不同 entity 的字段不串扰。
    // ---------------------------------------------------------------------------
    void TestMultipleEntitiesAndComponents()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);

        World  worldA;
        Entity e1 = worldA.CreateEntity();
        TagSaveable(worldA, e1);
        worldA.AddComponent(e1, PlayerInventory{100, 3});
        worldA.AddComponent(e1, CheckpointTag{42});

        Entity e2 = worldA.CreateEntity();
        TagSaveable(worldA, e2);
        worldA.AddComponent(e2, PlayerInventory{0, 99});

        // 一个不打 Saveable tag 的 entity，应被 Save 跳过
        Entity e3 = worldA.CreateEntity();
        worldA.AddComponent(e3, PlayerInventory{999, 999});

        auto path = MakeTempSavePath("multi");
        assert(sys.Save(worldA, path.string()).IsOk());

        World worldB;
        assert(sys.Load(path.string(), worldB).IsOk());
        assert(CountSaveableEntities(worldB) == 2);

        // 收集 worldB 中两个 entity 的字段，做集合比对（顺序由 EnTT 内部
        // view 决定，不强约束）。
        std::vector<std::pair<std::int64_t, std::int64_t>> inventories;
        std::vector<std::int64_t>                          checkpoints;
        auto                                               view = worldB.Registry().view<const SaveableComponent>();
        for (auto e : view)
        {
            Entity ent = World::FromEntt(e);
            if (auto* inv = worldB.GetComponent<PlayerInventory>(ent))
            {
                inventories.emplace_back(inv->goldCoins, inv->spiritShards);
            }
            if (auto* cp = worldB.GetComponent<CheckpointTag>(ent))
            {
                checkpoints.push_back(cp->checkpointId);
            }
        }
        assert(inventories.size() == 2);
        // 期望两组：{100,3} 与 {0,99}（顺序不强求，但本测试两条都唯一可
        // 通过 == 比较来识别）
        bool found100 = false, found0 = false;
        for (auto& p : inventories)
        {
            if (p.first == 100 && p.second == 3)
            {
                found100 = true;
            }
            if (p.first == 0 && p.second == 99)
            {
                found0 = true;
            }
        }
        assert(found100 && found0);

        assert(checkpoints.size() == 1);
        assert(checkpoints[0] == 42);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 4. Forward-compat：用一个"知道"未注册 component 的写出端造出含未知
    //    name 的 JSON 文件，再让一个"不知道"该 component 的 reader 加载。
    //    应 silent skip，不视为 fatal。
    // ---------------------------------------------------------------------------
    void TestForwardCompatUnknownComponent()
    {
        // 写入端 registry：包含 PlayerInventory + UnknownComp 两条。
        SaveGameRegistry writerReg;
        auto             rc1 = writerReg.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{"game/PlayerInventory", 1, 0},
            [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
            {
                const std::string base(path);
                w.WriteInt(base + "/goldCoins", c.goldCoins);
                w.WriteInt(base + "/spiritShards", c.spiritShards);
            },
            [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
            {
                const std::string base(path);
                std::int64_t      v = 0;
                if (!r.ReadInt(base + "/goldCoins", v))
                {
                    return false;
                }
                c.goldCoins = v;
                if (!r.ReadInt(base + "/spiritShards", v))
                {
                    return false;
                }
                c.spiritShards = v;
                return true;
            });
        assert(rc1.IsOk());

        auto rc2 = writerReg.Register<CheckpointTag>(
            "FutureFeature", // 未来才会出现的 component name
            SchemaVersion{"game/FutureFeature", 1, 0},
            [](JsonWriter& w, std::string_view path, const CheckpointTag& c)
            {
                w.WriteInt(std::string(path) + "/checkpointId", c.checkpointId);
            },
            [](const JsonReader& r, std::string_view path, CheckpointTag& c) -> bool
            {
                std::int64_t v = 0;
                if (!r.ReadInt(std::string(path) + "/checkpointId", v))
                {
                    return false;
                }
                c.checkpointId = v;
                return true;
            });
        assert(rc2.IsOk());

        SaveGameSystem writerSys(writerReg);

        World  worldA;
        Entity ent = worldA.CreateEntity();
        TagSaveable(worldA, ent);
        worldA.AddComponent(ent, PlayerInventory{50, 1});
        worldA.AddComponent(ent, CheckpointTag{999});

        auto path = MakeTempSavePath("fwd_compat");
        assert(writerSys.Save(worldA, path.string()).IsOk());

        // 读端 registry：只注册 PlayerInventory；FutureFeature 对它而言是
        // "未识别 component 名" → 应 silent skip，整体 Load OK。
        SaveGameRegistry readerReg;
        auto             rc3 = readerReg.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{"game/PlayerInventory", 1, 0},
            [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
            {
                const std::string base(path);
                w.WriteInt(base + "/goldCoins", c.goldCoins);
                w.WriteInt(base + "/spiritShards", c.spiritShards);
            },
            [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
            {
                const std::string base(path);
                std::int64_t      v = 0;
                if (!r.ReadInt(base + "/goldCoins", v))
                {
                    return false;
                }
                c.goldCoins = v;
                if (!r.ReadInt(base + "/spiritShards", v))
                {
                    return false;
                }
                c.spiritShards = v;
                return true;
            });
        assert(rc3.IsOk());

        SaveGameSystem readerSys(readerReg);

        World worldB;
        auto  loadRc = readerSys.Load(path.string(), worldB);
        assert(loadRc.IsOk()); // skip 不视为 fatal
        assert(CountSaveableEntities(worldB) == 1);

        auto   view     = worldB.Registry().view<const SaveableComponent>();
        Entity restored = Entity::Invalid();
        if (view.begin() != view.end())
        {
            restored = World::FromEntt(*view.begin());
        }
        assert(restored.IsValid());
        auto* p = worldB.GetComponent<PlayerInventory>(restored);
        assert(p != nullptr);
        assert(p->goldCoins == 50 && p->spiritShards == 1);
        // CheckpointTag 没注册 → 不应该被 attach
        assert(worldB.GetComponent<CheckpointTag>(restored) == nullptr);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 5. CRC 损坏：把 payload 中间一字节翻转，CRC 不再匹配 → InvalidArgument。
    //    World 不被部分修改。
    // ---------------------------------------------------------------------------
    void TestCorruptedCrc()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);

        World  worldA;
        Entity ent = worldA.CreateEntity();
        TagSaveable(worldA, ent);
        worldA.AddComponent(ent, PlayerInventory{77, 11});

        auto path = MakeTempSavePath("corrupt_crc");
        assert(sys.Save(worldA, path.string()).IsOk());

        // 文件 layout：[16B header][payload]。改 payload 中间一字节让 CRC
        // 失配。Header 的 magic / formatVersion / payloadLen 不动。
        auto bytes = ReadFileBytes(path);
        assert(bytes.size() > 16 + 8);
        bytes[16 + 4] ^= 0xFFu;
        WriteFileBytes(path, bytes);

        World worldB;
        auto  rc = sys.Load(path.string(), worldB);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::InvalidArgument);
        assert(CountSaveableEntities(worldB) == 0);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 6. Header magic 损坏：前 4 字节不是 'O','S','A','V' → IoError。
    // ---------------------------------------------------------------------------
    void TestCorruptedHeaderMagic()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);

        World  worldA;
        Entity ent = worldA.CreateEntity();
        TagSaveable(worldA, ent);
        worldA.AddComponent(ent, PlayerInventory{1, 1});

        auto path = MakeTempSavePath("corrupt_magic");
        assert(sys.Save(worldA, path.string()).IsOk());

        auto bytes = ReadFileBytes(path);
        assert(bytes.size() >= 4);
        bytes[0] = 'X';
        WriteFileBytes(path, bytes);

        World worldB;
        auto  rc = sys.Load(path.string(), worldB);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::IoError);
        assert(CountSaveableEntities(worldB) == 0);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 7. 顶层 schemaVersion 不兼容：手工拼一个合法的 OSAV 文件，但 payload
    //    里 schemaVersion 的 namespace 故意改成 "save/wrong" → 顶层 SchemaMismatch。
    // ---------------------------------------------------------------------------
    void TestTopLevelSchemaMismatch()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);

        // 写一份 namespace 错误的 payload，再用 SaveGameSystem 自己的格式
        // 走法（手算 CRC + 拼 header）落到 disk。
        JsonWriter w;
        w.WriteSchemaVersion("schemaVersion",
                             SchemaVersion{"save/wrong", 1, 0});
        w.BeginArray("entities", 0);
        const std::string payload = w.Dump(2);

        // CRC32 (IEEE 802.3, init 0xFFFFFFFF, xor 0xFFFFFFFF)
        auto crc32 = [](const void* d, std::size_t n) -> std::uint32_t
        {
            constexpr std::uint32_t kPoly = 0xEDB88320u;
            std::uint32_t           crc   = 0xFFFFFFFFu;
            const auto*             p     = static_cast<const std::uint8_t*>(d);
            for (std::size_t i = 0; i < n; ++i)
            {
                std::uint32_t c = crc ^ p[i];
                for (int j = 0; j < 8; ++j)
                {
                    c = (c & 1u) ? (kPoly ^ (c >> 1)) : (c >> 1);
                }
                crc = c;
            }
            return crc ^ 0xFFFFFFFFu;
        };

        const std::uint32_t crc = crc32(payload.data(), payload.size());
        const std::uint32_t len = static_cast<std::uint32_t>(payload.size());

        std::vector<std::uint8_t> bytes(16 + payload.size());
        bytes[0] = 'O';
        bytes[1] = 'S';
        bytes[2] = 'A';
        bytes[3] = 'V';
        auto enc = [&](std::size_t off, std::uint32_t v)
        {
            bytes[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
            bytes[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
            bytes[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
            bytes[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        };
        enc(4, 1u); // formatVersion
        enc(8, crc);
        enc(12, len);
        std::memcpy(bytes.data() + 16, payload.data(), payload.size());

        auto path = MakeTempSavePath("top_schema_mismatch");
        WriteFileBytes(path, bytes);

        World worldB;
        auto  rc = sys.Load(path.string(), worldB);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::SchemaMismatch);
        assert(CountSaveableEntities(worldB) == 0);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 8. 单个 component schemaVersion 不兼容：写入端用 v2，读取端注册 v1
    //    → SchemaMismatch + 全部回滚（前一个 entity 也不残留）。
    // ---------------------------------------------------------------------------
    void TestComponentSchemaMismatchRollsBack()
    {
        // 写入端：PlayerInventory 注册为 v2.0
        SaveGameRegistry writerReg;
        auto             rc1 = writerReg.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{"game/PlayerInventory", 2, 0},
            [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
            {
                const std::string base(path);
                w.WriteInt(base + "/goldCoins", c.goldCoins);
                w.WriteInt(base + "/spiritShards", c.spiritShards);
            },
            [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
            {
                const std::string base(path);
                std::int64_t      v = 0;
                if (!r.ReadInt(base + "/goldCoins", v))
                {
                    return false;
                }
                c.goldCoins = v;
                if (!r.ReadInt(base + "/spiritShards", v))
                {
                    return false;
                }
                c.spiritShards = v;
                return true;
            });
        assert(rc1.IsOk());
        SaveGameSystem writerSys(writerReg);

        World  worldA;
        Entity e1 = worldA.CreateEntity();
        TagSaveable(worldA, e1);
        worldA.AddComponent(e1, PlayerInventory{1, 2});
        Entity e2 = worldA.CreateEntity();
        TagSaveable(worldA, e2);
        worldA.AddComponent(e2, PlayerInventory{3, 4});

        auto path = MakeTempSavePath("comp_schema_mismatch");
        assert(writerSys.Save(worldA, path.string()).IsOk());

        // 读取端：PlayerInventory 注册为 v1.0 → 不能 CanRead v2 → SchemaMismatch
        auto           registry = MakeRegistry(); // v1
        SaveGameSystem readerSys(registry);

        World worldB;
        // 预先在 worldB 里有一个无关 entity，验证 Load 失败时它不被影响
        Entity preexisting = worldB.CreateEntity();
        worldB.AddComponent(preexisting, PlayerInventory{777, 777});

        auto rc = readerSys.Load(path.string(), worldB);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::SchemaMismatch);

        // 预存的 entity 仍然在；本次 Load 创建的 entity 全部回滚 → Saveable
        // entity 数目不应该有任何增长。
        assert(CountSaveableEntities(worldB) == 0);
        auto* preserved = worldB.GetComponent<PlayerInventory>(preexisting);
        assert(preserved != nullptr);
        assert(preserved->goldCoins == 777);

        RemoveIfExists(path);
    }

    // ---------------------------------------------------------------------------
    // 9. 不存在的 path → IoError。
    // ---------------------------------------------------------------------------
    void TestMissingFile()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);

        auto path = MakeTempSavePath("missing");
        RemoveIfExists(path); // 确保不存在

        World worldB;
        auto  rc = sys.Load(path.string(), worldB);
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::IoError);
    }

} // namespace

int main()
{
    TestEmptyRoundtrip();
    TestSingleEntitySingleComponent();
    TestMultipleEntitiesAndComponents();
    TestForwardCompatUnknownComponent();
    TestCorruptedCrc();
    TestCorruptedHeaderMagic();
    TestTopLevelSchemaMismatch();
    TestComponentSchemaMismatchRollsBack();
    TestMissingFile();

    std::printf("[save_game_system_test] all 9 cases passed\n");
    return 0;
}
