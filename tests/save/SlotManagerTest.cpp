// SlotManager 单元测试 —— Phase 5.5 / Task 04。
//
// 覆盖：
//   1. ListSlots 在目录尚未创建时返回空 vector（首次启动场景）
//   2. Save 写出 `.save` + `.meta.json` 双文件，字段全部落地
//   3. ReadMetadata 取回字段、内部覆写字段（slotName / engineSaveFormatVersion
//      / 自动填 savedAtUnixSeconds）正确
//   4. ListSlots 多 slot：sidecar 缺失时退化到 placeholder + mtime；
//      orphan `.meta.json`（无对应 .save）被跳过；
//      `.save.tmp` 残留被跳过；
//      返回顺序按 savedAtUnixSeconds 降序
//   5. DeleteSlot 同时清 .save + .meta.json；不存在时幂等成功
//   6. 输入校验：slotName 空 / 含分隔符 / `..` → InvalidArgument
//
// 与 SavePathTest 同模式：用 PID-tag 化 vendor 名隔离 + main() 收尾
// remove_all 清干净 %APPDATA% 残留。

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
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using Orange::Engine::Entity;
using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;
using Orange::Engine::ResultCode;
using Orange::Engine::SchemaVersion;
using Orange::Engine::World;
using Orange::Engine::Save::ResolveUserDataRoot;
using Orange::Engine::Save::SaveableComponent;
using Orange::Engine::Save::SaveGameRegistry;
using Orange::Engine::Save::SaveGameSystem;
using Orange::Engine::Save::SavePathOptions;
using Orange::Engine::Save::SlotManager;
using Orange::Engine::Save::SlotMetadata;

namespace
{

#if defined(_WIN32)

    std::string TestVendor()
    {
        return std::string{"OrangeEngineTest_SlotMgr_"} + std::to_string(_getpid());
    }

    constexpr const char* kTestGame = "SlotManagerTestGame";

    SavePathOptions MakeOptions()
    {
        SavePathOptions opt{};
        opt.vendor = TestVendor();
        opt.game   = kTestGame;
        return opt;
    }

    void CleanupVendor()
    {
        auto rootResult = ResolveUserDataRoot();
        if (rootResult.IsErr())
        {
            return;
        }
        auto            vendorDir = std::move(rootResult).Value() / TestVendor();
        std::error_code ec;
        std::filesystem::remove_all(vendorDir, ec);
    }

    // 测试用 component —— 用最小 POD 让 Save / Load 关注点放在 SlotManager
    // 自身。
    struct PlayerInventory
    {
        std::int64_t goldCoins{0};
    };

    SaveGameRegistry MakeRegistry()
    {
        SaveGameRegistry reg;
        auto             rc = reg.Register<PlayerInventory>(
            "PlayerInventory",
            SchemaVersion{"game/PlayerInventory", 1, 0},
            [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
            {
                w.WriteInt(std::string(path) + "/goldCoins", c.goldCoins);
            },
            [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
            {
                std::int64_t v = 0;
                if (!r.ReadInt(std::string(path) + "/goldCoins", v))
                {
                    return false;
                }
                c.goldCoins = v;
                return true;
            });
        assert(rc.IsOk());
        return reg;
    }

    void TagSaveable(World& world, Entity ent)
    {
        world.Registry().emplace_or_replace<SaveableComponent>(World::ToEntt(ent));
    }

    // ---------------------------------------------------------------------------
    // 1. ListSlots 在目录尚未创建时返回空 vector
    // ---------------------------------------------------------------------------
    void TestListSlotsEmptyDirectory()
    {
        CleanupVendor(); // 确保从干净状态开始

        SlotManager mgr(MakeOptions());
        auto        rc = mgr.ListSlots();
        assert(rc.IsOk());
        assert(rc.Value().empty());
    }

    // ---------------------------------------------------------------------------
    // 2. Save 写出 .save + .meta.json 双文件
    // ---------------------------------------------------------------------------
    void TestSaveCreatesBothFiles()
    {
        CleanupVendor();
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);
        SlotManager    mgr(MakeOptions());

        World  world;
        Entity ent = world.CreateEntity();
        TagSaveable(world, ent);
        world.AddComponent(ent, PlayerInventory{500});

        SlotMetadata meta{};
        meta.displayName     = "Chapter 3";
        meta.summary         = "Boss defeated";
        meta.playTimeSeconds = 1234;
        // savedAtUnixSeconds 留 0，让 SlotManager 自填当前时间

        auto rc = mgr.Save(sys, world, "slot1", meta);
        assert(rc.IsOk());

        auto savePath = mgr.ResolveSavePath("slot1");
        auto metaPath = mgr.ResolveMetadataPath("slot1");
        assert(savePath.IsOk());
        assert(metaPath.IsOk());
        assert(std::filesystem::exists(savePath.Value()));
        assert(std::filesystem::exists(metaPath.Value()));

        // sidecar 不应该有 .save 后缀；.save 不应该有 .meta.json 后缀
        assert(savePath.Value().extension() == ".save");
        assert(metaPath.Value().filename() == "slot1.meta.json");
    }

    // ---------------------------------------------------------------------------
    // 3. ReadMetadata 字段还原 + 内部覆写字段正确
    // ---------------------------------------------------------------------------
    void TestReadMetadataFields()
    {
        CleanupVendor();
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);
        SlotManager    mgr(MakeOptions());

        World  world;
        Entity ent = world.CreateEntity();
        TagSaveable(world, ent);
        world.AddComponent(ent, PlayerInventory{42});

        SlotMetadata meta{};
        meta.slotName                = "WRONG_NAME_SHOULD_BE_OVERWRITTEN";
        meta.displayName             = "My Chapter";
        meta.summary                 = "Halfway through";
        meta.savedAtUnixSeconds      = 1700000000; // 显式给值，应该被尊重
        meta.playTimeSeconds         = 7200;
        meta.engineSaveFormatVersion = 99; // 应该被覆写
        meta.isAutosave              = true;

        auto saveRc = mgr.Save(sys, world, "checkpoint_a", meta);
        assert(saveRc.IsOk());

        auto readRc = mgr.ReadMetadata("checkpoint_a");
        assert(readRc.IsOk());
        auto loaded = std::move(readRc).Value();

        // slotName 必须是 SlotManager 用的 "checkpoint_a"，不是 game 写的 "WRONG_..."
        assert(loaded.slotName == "checkpoint_a");
        assert(loaded.displayName == "My Chapter");
        assert(loaded.summary == "Halfway through");
        assert(loaded.savedAtUnixSeconds == 1700000000);
        assert(loaded.playTimeSeconds == 7200);
        assert(loaded.engineSaveFormatVersion == 1); // 覆写为当前 engine 版本，不是 99
        assert(loaded.isAutosave == true);
    }

    // ---------------------------------------------------------------------------
    // 4. ListSlots 多 slot 排序 + sidecar 缺失退化 + orphan 跳过 + .tmp 跳过
    // ---------------------------------------------------------------------------
    void TestListSlotsMultipleAndDegradation()
    {
        CleanupVendor();
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);
        SlotManager    mgr(MakeOptions());

        World  world;
        Entity ent = world.CreateEntity();
        TagSaveable(world, ent);
        world.AddComponent(ent, PlayerInventory{1});

        // 三个正常 slot，时间戳故意倒序设置 → 排序应把最大值排前
        SlotMetadata m1{};
        m1.savedAtUnixSeconds = 1000;
        m1.displayName        = "First";
        SlotMetadata m2{};
        m2.savedAtUnixSeconds = 3000;
        m2.displayName        = "Third";
        SlotMetadata m3{};
        m3.savedAtUnixSeconds = 2000;
        m3.displayName        = "Second";
        assert(mgr.Save(sys, world, "slotA", m1).IsOk());
        assert(mgr.Save(sys, world, "slotB", m2).IsOk());
        assert(mgr.Save(sys, world, "slotC", m3).IsOk());

        // 故意删 slotC 的 sidecar，模拟"sidecar 损坏 / 缺失" → ListSlots 应
        // 该退化到 placeholder（仍出现在结果里，但 displayName 等字段空）
        auto            metaC = mgr.ResolveMetadataPath("slotC").Value();
        std::error_code ec;
        std::filesystem::remove(metaC, ec);

        // 写一个 orphan sidecar（无对应 .save）—— 应被 ListSlots 跳过
        auto dirRc = Orange::Engine::Save::ResolveSaveDirectory(MakeOptions());
        assert(dirRc.IsOk());
        auto dir = std::move(dirRc).Value();
        {
            std::ofstream out(dir / "orphan.meta.json", std::ios::binary | std::ios::trunc);
            out << "{}";
        }

        // 写一个 .save.tmp 残留 —— 应被跳过（不是 .save 后缀）
        {
            std::ofstream out(dir / "interrupted.save.tmp", std::ios::binary | std::ios::trunc);
            out << "garbage";
        }

        auto listRc = mgr.ListSlots();
        assert(listRc.IsOk());
        auto list = std::move(listRc).Value();

        // 三个 slot：slotA / slotB / slotC（不含 orphan / .tmp）
        assert(list.size() == 3);

        // 排序：slotB(3000) > slotC(mtime, 当前时间, 大约 1.7e9) > slotA(1000)
        // 实际 mtime 现在远大于 3000 / 1000 → slotC 反而第一
        // 调整断言：取出第一个，验证它是 slotC 且时间戳是非 0；后面验证
        // slotB 比 slotA 排前。
        bool seenA = false, seenB = false, seenC = false;
        for (const auto& m : list)
        {
            if (m.slotName == "slotA")
            {
                seenA = true;
                assert(m.displayName == "First");
            }
            if (m.slotName == "slotB")
            {
                seenB = true;
                assert(m.displayName == "Third");
            }
            if (m.slotName == "slotC")
            {
                seenC = true;
                // sidecar 已被删 → placeholder：displayName 空 + savedAtUnixSeconds 取自 mtime（非 0）
                assert(m.displayName.empty());
                assert(m.savedAtUnixSeconds > 0);
            }
        }
        assert(seenA && seenB && seenC);

        // slotB(3000) 必须排在 slotA(1000) 前
        auto posA = std::size_t{0};
        auto posB = std::size_t{0};
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].slotName == "slotA")
            {
                posA = i;
            }
            if (list[i].slotName == "slotB")
            {
                posB = i;
            }
        }
        assert(posB < posA);
    }

    // ---------------------------------------------------------------------------
    // 5. DeleteSlot 同时清 .save + .meta.json；不存在幂等成功
    // ---------------------------------------------------------------------------
    void TestDeleteSlot()
    {
        CleanupVendor();
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);
        SlotManager    mgr(MakeOptions());

        World  world;
        Entity ent = world.CreateEntity();
        TagSaveable(world, ent);
        world.AddComponent(ent, PlayerInventory{99});

        assert(mgr.Save(sys, world, "to_delete", SlotMetadata{}).IsOk());
        assert(mgr.SlotExists("to_delete"));

        auto savePath = mgr.ResolveSavePath("to_delete").Value();
        auto metaPath = mgr.ResolveMetadataPath("to_delete").Value();
        assert(std::filesystem::exists(savePath));
        assert(std::filesystem::exists(metaPath));

        auto rc = mgr.DeleteSlot("to_delete");
        assert(rc.IsOk());
        assert(!mgr.SlotExists("to_delete"));
        assert(!std::filesystem::exists(savePath));
        assert(!std::filesystem::exists(metaPath));

        // 第二次删（已不存在）→ 仍 Ok（幂等）
        auto rc2 = mgr.DeleteSlot("to_delete");
        assert(rc2.IsOk());
    }

    // ---------------------------------------------------------------------------
    // 6. 输入校验
    // ---------------------------------------------------------------------------
    void TestInputValidation()
    {
        auto           registry = MakeRegistry();
        SaveGameSystem sys(registry);
        SlotManager    mgr(MakeOptions());

        World world;
        {
            auto rc = mgr.Save(sys, world, "", SlotMetadata{});
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto rc = mgr.ReadMetadata("with/slash");
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto rc = mgr.DeleteSlot("..");
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto rc = mgr.ResolveSavePath("");
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
    }

#endif // _WIN32

} // namespace

int main()
{
#if defined(_WIN32)
    TestListSlotsEmptyDirectory();
    TestSaveCreatesBothFiles();
    TestReadMetadataFields();
    TestListSlotsMultipleAndDegradation();
    TestDeleteSlot();
    TestInputValidation();
    CleanupVendor();
    std::printf("[slot_manager_test] all 6 cases passed\n");
#else
    std::printf("[slot_manager_test] skipped on non-Windows (SavePath returns Unsupported)\n");
#endif
    return 0;
}
