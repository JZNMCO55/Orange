// 损坏存档 + 错误路径覆盖 —— Phase 5.5 / Task 06。
//
// Task 02-05 各 test 已覆盖一批"主流程 + 简单失败路径"；本文件集中补
// 齐剩余的损坏 / 边角错误路径，让"故意 fuzz"的覆盖意图集中可见，并对
// 每条失败路径都验证：
//   1. 引擎返回明确的 ResultCode（不崩溃 / 不静默 corrupt）
//   2. World / 文件系统不被部分修改（与 Task 02 / 04 的回滚约定一致）
//
// 覆盖：
//   A. SaveGameSystem 文件层面错误：
//      1. < 16 字节文件 → IoError
//      2. 完全空文件   → IoError
//      3. header formatVersion 不匹配 → IoError
//      4. payload 长度声明 < 实际（trailing garbage）→ InvalidArgument
//      5. payload 长度声明 > 实际（截断）→ InvalidArgument
//      6. payload 合法字节但不是 JSON → InvalidArgument
//   B. SaveGameSystem JSON 层面错误：
//      7. 顶层 schemaVersion 字段完全缺失 → InvalidArgument
//      8. per-component "version" 子字段缺失 → InvalidArgument + 回滚
//      9. typed read 返 false（schema 合法但字段读失败）→ InvalidArgument + 回滚
//   C. SlotManager sidecar 错误：
//      10. sidecar JSON 完全损坏 → ReadMetadata InvalidArgument；
//          ListSlots 退化到 placeholder
//      11. sidecar schemaVersion 不兼容 → ReadMetadata SchemaMismatch
//      12. ListSlots 目录里只有 sidecar 无 .save → 空 vector
//      13. DeleteSlot 只有 sidecar 无 .save → 仍 Ok 且 sidecar 被清

#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/save/SaveGameRegistry.h>
#include <orange/engine/save/SaveGameSystem.h>
#include <orange/engine/save/SavePath.h>
#include <orange/engine/save/SaveableComponent.h>
#include <orange/engine/save/SlotManager.h>
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
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include <process.h>
#endif

using Orange::Engine::Entity;
using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;
using Orange::Engine::ResultCode;
using Orange::Engine::SchemaVersion;
using Orange::Engine::World;
using Orange::Engine::Save::ResolveSaveDirectory;
using Orange::Engine::Save::ResolveUserDataRoot;
using Orange::Engine::Save::SaveableComponent;
using Orange::Engine::Save::SaveGameRegistry;
using Orange::Engine::Save::SaveGameSystem;
using Orange::Engine::Save::SavePathOptions;
using Orange::Engine::Save::SlotManager;
using Orange::Engine::Save::SlotMetadata;

namespace
{

// ---------------------------------------------------------------------------
// 共用：测试 component + 注册 + 文件 IO helper
// ---------------------------------------------------------------------------

struct PlayerInventory
{
    std::int64_t goldCoins{0};
    std::int64_t spiritShards{0};
};

const SchemaVersion kInvV1{"game/PlayerInventory", 1, 0};

SaveGameRegistry MakeRegistry()
{
    SaveGameRegistry reg;
    auto rc = reg.Register<PlayerInventory>(
        "PlayerInventory", kInvV1,
        [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
        {
            const std::string b(path);
            w.WriteInt(b + "/goldCoins",    c.goldCoins);
            w.WriteInt(b + "/spiritShards", c.spiritShards);
        },
        [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
        {
            const std::string b(path);
            std::int64_t v = 0;
            if (!r.ReadInt(b + "/goldCoins",    v)) { return false; }
            c.goldCoins = v;
            if (!r.ReadInt(b + "/spiritShards", v)) { return false; }
            c.spiritShards = v;
            return true;
        });
    assert(rc.IsOk());
    return reg;
}

// 注册一个 read 总是返 false 的 component —— 用来测"schema 合法但 typed
// read 失败 → InvalidArgument + 回滚"路径。带一个占位字段避免空类型走
// EnTT 存储优化（World::AddComponent 模板对空类型的 T& 返回不兼容）。
struct AlwaysFails { std::int64_t marker{0}; };
const SchemaVersion kAlwaysFailsV1{"game/AlwaysFails", 1, 0};

SaveGameRegistry MakeAlwaysFailsRegistry()
{
    SaveGameRegistry reg;
    auto rc = reg.Register<AlwaysFails>(
        "AlwaysFails", kAlwaysFailsV1,
        [](JsonWriter& w, std::string_view path, const AlwaysFails&)
        {
            // 写一个空 data 子对象 —— 仅占位让 Has(.../version) 之后 Has(.../data) 也成立
            w.WriteInt(std::string(path) + "/marker", 1);
        },
        [](const JsonReader&, std::string_view, AlwaysFails&) -> bool
        {
            return false;   // 永远拒读
        });
    assert(rc.IsOk());
    return reg;
}

void TagSaveable(World& world, Entity ent)
{
    world.Registry().emplace_or_replace<SaveableComponent>(World::ToEntt(ent));
}

std::filesystem::path MakeTempPath(const char* tag)
{
    auto base = std::filesystem::temp_directory_path();
    base /= std::string{"orange_engine_corruption_test_"} + tag + ".save";
    std::error_code ec;
    std::filesystem::remove(base, ec);
    return base;
}

void RemoveIfExists(const std::filesystem::path& p) noexcept
{
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

void WriteFileBytes(const std::filesystem::path& path,
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

// IEEE 802.3 CRC32（与 SaveGameSystem 内部的实现等价）—— 用来给手工拼
// 装的 OSAV 文件算合法 CRC，让 corrup test 仅在我们想损坏的字段失败。
std::uint32_t Crc32(const void* data, std::size_t size) noexcept
{
    constexpr std::uint32_t kPoly = 0xEDB88320u;
    std::uint32_t           crc   = 0xFFFFFFFFu;
    const auto*             p     = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        std::uint32_t c = crc ^ p[i];
        for (int j = 0; j < 8; ++j)
        {
            c = (c & 1u) ? (kPoly ^ (c >> 1)) : (c >> 1);
        }
        crc = c;
    }
    return crc ^ 0xFFFFFFFFu;
}

// 工具：手工拼一份完整 OSAV 文件（header + payload）落到 path。
// formatVersion / crc / payloadLen 全部由调用方控制，便于针对性地损
// 坏某个字段。
void WriteOsavFile(const std::filesystem::path& path,
                   std::uint32_t                 formatVersion,
                   std::uint32_t                 crc,
                   std::uint32_t                 payloadLen,
                   std::string_view              payloadBytes)
{
    auto enc = [](std::vector<std::uint8_t>& dst, std::size_t off, std::uint32_t v)
    {
        dst[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
        dst[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
        dst[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
        dst[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    };
    std::vector<std::uint8_t> bytes(16 + payloadBytes.size());
    bytes[0] = 'O'; bytes[1] = 'S'; bytes[2] = 'A'; bytes[3] = 'V';
    enc(bytes,  4, formatVersion);
    enc(bytes,  8, crc);
    enc(bytes, 12, payloadLen);
    if (!payloadBytes.empty())
    {
        std::memcpy(bytes.data() + 16, payloadBytes.data(), payloadBytes.size());
    }
    WriteFileBytes(path, bytes);
}

std::size_t CountSaveable(const World& world)
{
    return world.Registry().view<const SaveableComponent>().size();
}

// ---------------------------------------------------------------------------
// A. 文件层面错误
// ---------------------------------------------------------------------------

// 1. < 16 字节 → IoError（连 header 都装不下）
void TestFileTooShort()
{
    auto path = MakeTempPath("too_short");
    WriteFileBytes(path, std::vector<std::uint8_t>{'O', 'S', 'A'});  // 仅 3 字节

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::IoError);
    assert(CountSaveable(world) == 0);
    RemoveIfExists(path);
}

// 2. 完全空文件 → IoError
void TestEmptyFile()
{
    auto path = MakeTempPath("empty");
    WriteFileBytes(path, std::vector<std::uint8_t>{});

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::IoError);
    assert(CountSaveable(world) == 0);
    RemoveIfExists(path);
}

// 3. header formatVersion 不匹配 → IoError
void TestHeaderFormatVersionMismatch()
{
    // payload 是合法 JSON 且 CRC 也对 —— 失败应当出在 ParseHeader 阶段
    JsonWriter w;
    w.WriteSchemaVersion("schemaVersion", SchemaVersion{"save/game", 1, 0});
    w.BeginArray("entities", 0);
    const std::string payload = w.Dump(2);
    const auto crc = Crc32(payload.data(), payload.size());
    auto path = MakeTempPath("format_version");
    WriteOsavFile(path, /*formatVersion*/ 99, crc,
                  static_cast<std::uint32_t>(payload.size()), payload);

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::IoError);
    assert(CountSaveable(world) == 0);
    RemoveIfExists(path);
}

// 4. payload 长度声明 < 实际（trailing garbage）→ InvalidArgument
void TestPayloadLengthLessThanActual()
{
    JsonWriter w;
    w.WriteSchemaVersion("schemaVersion", SchemaVersion{"save/game", 1, 0});
    w.BeginArray("entities", 0);
    const std::string payload = w.Dump(2);
    const auto crc = Crc32(payload.data(), payload.size());
    auto path = MakeTempPath("payload_short");
    // 故意把 payloadLen 减一 —— 实际文件多出 1 字节 trailing garbage
    WriteOsavFile(path, /*formatVersion*/ 1, crc,
                  static_cast<std::uint32_t>(payload.size() - 1), payload);

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::InvalidArgument);
    assert(CountSaveable(world) == 0);
    RemoveIfExists(path);
}

// 5. payload 长度声明 > 实际（截断）→ InvalidArgument
void TestPayloadLengthMoreThanActual()
{
    JsonWriter w;
    w.WriteSchemaVersion("schemaVersion", SchemaVersion{"save/game", 1, 0});
    w.BeginArray("entities", 0);
    const std::string payload = w.Dump(2);
    const auto crc = Crc32(payload.data(), payload.size());
    auto path = MakeTempPath("payload_long");
    // 故意把 payloadLen 加一 —— 实际文件少 1 字节
    WriteOsavFile(path, /*formatVersion*/ 1, crc,
                  static_cast<std::uint32_t>(payload.size() + 1), payload);

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::InvalidArgument);
    RemoveIfExists(path);
}

// 6. payload 合法字节但不是 JSON → InvalidArgument
void TestPayloadNotJson()
{
    const std::string_view payload{"this-is-not-json"};
    const auto crc = Crc32(payload.data(), payload.size());
    auto path = MakeTempPath("not_json");
    WriteOsavFile(path, /*formatVersion*/ 1, crc,
                  static_cast<std::uint32_t>(payload.size()), payload);

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::InvalidArgument);
    assert(CountSaveable(world) == 0);
    RemoveIfExists(path);
}

// ---------------------------------------------------------------------------
// B. JSON 层面错误
// ---------------------------------------------------------------------------

// 7. 顶层 schemaVersion 完全缺失 → InvalidArgument
void TestTopLevelSchemaVersionMissing()
{
    // 合法 JSON，但故意不写 schemaVersion 字段
    JsonWriter w;
    w.BeginArray("entities", 0);
    const std::string payload = w.Dump(2);
    const auto crc = Crc32(payload.data(), payload.size());
    auto path = MakeTempPath("top_schema_missing");
    WriteOsavFile(path, 1, crc, static_cast<std::uint32_t>(payload.size()), payload);

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::InvalidArgument);
    assert(CountSaveable(world) == 0);
    RemoveIfExists(path);
}

// 8. per-component "version" 子字段缺失 → InvalidArgument + 回滚
void TestComponentVersionMissing()
{
    // 一个 entity 持有 PlayerInventory，components/PlayerInventory 下故意只
    // 给 data 不给 version
    JsonWriter w;
    w.WriteSchemaVersion("schemaVersion", SchemaVersion{"save/game", 1, 0});
    w.BeginArray("entities", 1);
    w.WriteInt   ("entities/0/id", 0);
    w.WriteInt   ("entities/0/components/PlayerInventory/data/goldCoins",    50);
    w.WriteInt   ("entities/0/components/PlayerInventory/data/spiritShards", 5);
    // 故意不写 entities/0/components/PlayerInventory/version
    const std::string payload = w.Dump(2);
    const auto crc = Crc32(payload.data(), payload.size());
    auto path = MakeTempPath("comp_version_missing");
    WriteOsavFile(path, 1, crc, static_cast<std::uint32_t>(payload.size()), payload);

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);

    // 预存一个无关 entity，验证 Load 失败时它不被影响
    World world;
    Entity preexisting = world.CreateEntity();
    world.AddComponent(preexisting, PlayerInventory{777, 777});

    auto rc = sys.Load(path.string(), world);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::InvalidArgument);
    assert(CountSaveable(world) == 0);
    auto* preserved = world.GetComponent<PlayerInventory>(preexisting);
    assert(preserved != nullptr);
    assert(preserved->goldCoins == 777);
    RemoveIfExists(path);
}

// 9. typed read 返 false（schema 合法但字段读失败）→ InvalidArgument + 回滚
void TestTypedReadReturnsFalse()
{
    // 用 "AlwaysFails" reader：注册时写 entry，Load 时 entry.Read 永远返 false。
    auto registry = MakeAlwaysFailsRegistry();
    SaveGameSystem sys(registry);

    // 用同一个 registry 写一份合法的 .save —— Save 路径不调 Read，所以
    // 文件本身合法 + schema 一致；失败应该出在 Load 的 typed read 步骤。
    World worldA;
    Entity e1 = worldA.CreateEntity();
    TagSaveable(worldA, e1);
    worldA.AddComponent(e1, AlwaysFails{});
    Entity e2 = worldA.CreateEntity();
    TagSaveable(worldA, e2);
    worldA.AddComponent(e2, AlwaysFails{});

    auto path = MakeTempPath("read_false");
    assert(sys.Save(worldA, path.string()).IsOk());

    World worldB;
    Entity preexisting = worldB.CreateEntity();   // 验证回滚不动既有 entity

    auto rc = sys.Load(path.string(), worldB);
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::InvalidArgument);
    assert(CountSaveable(worldB) == 0);   // 全部回滚
    assert(worldB.IsValid(preexisting));   // 预存 entity 不被影响
    RemoveIfExists(path);
}

// ---------------------------------------------------------------------------
// C. SlotManager sidecar 错误（仅 Windows，非 Win 平台 SavePath 返 Unsupported）
// ---------------------------------------------------------------------------

#if defined(_WIN32)

std::string CorruptionTestVendor()
{
    return std::string{"OrangeEngineTest_Corrupt_"} + std::to_string(_getpid());
}

constexpr const char* kCorruptionTestGame = "CorruptionTestGame";

SavePathOptions MakeSlotOptions()
{
    SavePathOptions opt{};
    opt.vendor = CorruptionTestVendor();
    opt.game   = kCorruptionTestGame;
    return opt;
}

void CleanupSlotVendor()
{
    auto rootResult = ResolveUserDataRoot();
    if (rootResult.IsErr()) { return; }
    auto vendorDir = std::move(rootResult).Value() / CorruptionTestVendor();
    std::error_code ec;
    std::filesystem::remove_all(vendorDir, ec);
}

// 10. sidecar JSON 完全损坏：ReadMetadata → InvalidArgument；
//     ListSlots 退化到 placeholder（slot 仍出现，displayName 空）
void TestSidecarJsonCorrupted()
{
    CleanupSlotVendor();

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    SlotManager    mgr(MakeSlotOptions());

    World world;
    Entity ent = world.CreateEntity();
    TagSaveable(world, ent);
    world.AddComponent(ent, PlayerInventory{1, 1});

    // 正常写 slot
    SlotMetadata meta{};
    meta.displayName = "Will be lost";
    assert(mgr.Save(sys, world, "slot_corrupt", meta).IsOk());

    // 故意把 sidecar 改成非法 JSON
    auto metaPath = mgr.ResolveMetadataPath("slot_corrupt").Value();
    {
        std::ofstream out(metaPath, std::ios::binary | std::ios::trunc);
        out << "{ this is not valid json";
    }

    // ReadMetadata 应该 InvalidArgument
    auto readRc = mgr.ReadMetadata("slot_corrupt");
    assert(readRc.IsErr());
    assert(readRc.Error() == ResultCode::InvalidArgument);

    // ListSlots 应该退化到 placeholder：slot 仍然出现，displayName 空
    auto listRc = mgr.ListSlots();
    assert(listRc.IsOk());
    auto list = std::move(listRc).Value();
    assert(list.size() == 1);
    assert(list[0].slotName == "slot_corrupt");
    assert(list[0].displayName.empty());          // 来自 placeholder
    assert(list[0].savedAtUnixSeconds > 0);       // 用 mtime 兜底
}

// 11. sidecar schemaVersion 不兼容 → ReadMetadata SchemaMismatch
void TestSidecarSchemaMismatch()
{
    CleanupSlotVendor();

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    SlotManager    mgr(MakeSlotOptions());

    World world;
    Entity ent = world.CreateEntity();
    TagSaveable(world, ent);
    world.AddComponent(ent, PlayerInventory{1, 1});

    assert(mgr.Save(sys, world, "slot_v_mismatch", SlotMetadata{}).IsOk());

    // 把 sidecar 的 schemaVersion 改成不兼容的 namespace
    auto metaPath = mgr.ResolveMetadataPath("slot_v_mismatch").Value();
    {
        std::ofstream out(metaPath, std::ios::binary | std::ios::trunc);
        out << R"({
  "schemaVersion": { "namespace": "save/wrong_ns", "major": 1, "minor": 0 },
  "slotName": "slot_v_mismatch"
})";
    }

    auto rc = mgr.ReadMetadata("slot_v_mismatch");
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::SchemaMismatch);
}

// 12. ListSlots 目录里只有 sidecar 无 .save → 空 vector（orphan 不算 slot）
void TestListSlotsOnlyOrphanSidecars()
{
    CleanupSlotVendor();

    SlotManager mgr(MakeSlotOptions());

    // 通过创建一份正常 slot 让目录存在，然后删 .save 留 sidecar
    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    World world;
    Entity ent = world.CreateEntity();
    TagSaveable(world, ent);
    world.AddComponent(ent, PlayerInventory{1, 1});
    assert(mgr.Save(sys, world, "orphan_slot", SlotMetadata{}).IsOk());

    auto savePath = mgr.ResolveSavePath("orphan_slot").Value();
    std::error_code ec;
    std::filesystem::remove(savePath, ec);

    auto listRc = mgr.ListSlots();
    assert(listRc.IsOk());
    assert(listRc.Value().empty());   // sidecar 不算 slot
}

// 13. DeleteSlot 只有 sidecar 无 .save → 仍 Ok 且 sidecar 被清
void TestDeleteSlotWithOnlySidecar()
{
    CleanupSlotVendor();

    auto registry = MakeRegistry();
    SaveGameSystem sys(registry);
    SlotManager    mgr(MakeSlotOptions());

    World world;
    Entity ent = world.CreateEntity();
    TagSaveable(world, ent);
    world.AddComponent(ent, PlayerInventory{1, 1});
    assert(mgr.Save(sys, world, "orphan_delete", SlotMetadata{}).IsOk());

    // 故意先删 .save，留 sidecar
    auto savePath = mgr.ResolveSavePath("orphan_delete").Value();
    auto metaPath = mgr.ResolveMetadataPath("orphan_delete").Value();
    std::error_code ec;
    std::filesystem::remove(savePath, ec);
    assert(std::filesystem::exists(metaPath));

    auto rc = mgr.DeleteSlot("orphan_delete");
    assert(rc.IsOk());                          // .save 不存在视为幂等无操作
    assert(!std::filesystem::exists(metaPath)); // sidecar 应被清
}

#endif  // _WIN32

}  // namespace

int main()
{
    // A: 文件层面（平台无关）
    TestFileTooShort();
    TestEmptyFile();
    TestHeaderFormatVersionMismatch();
    TestPayloadLengthLessThanActual();
    TestPayloadLengthMoreThanActual();
    TestPayloadNotJson();

    // B: JSON 层面（平台无关）
    TestTopLevelSchemaVersionMissing();
    TestComponentVersionMissing();
    TestTypedReadReturnsFalse();

    // C: SlotManager sidecar（仅 Windows）
#if defined(_WIN32)
    TestSidecarJsonCorrupted();
    TestSidecarSchemaMismatch();
    TestListSlotsOnlyOrphanSidecars();
    TestDeleteSlotWithOnlySidecar();
    CleanupSlotVendor();
    std::printf("[save_corruption_test] all 13 cases passed\n");
#else
    std::printf("[save_corruption_test] 9 platform-agnostic cases passed (sidecar tests skipped on non-Windows)\n");
#endif
    return 0;
}
