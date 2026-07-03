// SavePath 单元测试 —— Phase 5.5 / Task 03。
//
// 用一个测试专用 vendor 名（"OrangeEngineTest_<pid>"）让多次 / 并发运行
// 不互相踩，main() 收尾时把整个 vendor 目录递归删除，避免 %APPDATA% 残
// 留。
//
// 覆盖：
//   1. ResolveUserDataRoot 在 Windows 下返回非空绝对路径
//   2. ResolveSaveDirectory 成功 + 目录被实际创建（mkdir -p）
//   3. ResolveSaveDirectory 输入校验：game 空 / vendor 含分隔符 / "."
//      / ".." / 控制字符 → InvalidArgument
//   4. ResolveSaveSlotPath 拼出 ".save" 后缀 + 父目录已存在
//   5. ResolveSaveSlotPath 输入校验：slot 空 / 含分隔符 → InvalidArgument
//   6. 端到端：拿 ResolveSaveSlotPath 的路径喂给 SaveGameSystem.Save，再
//      Load 回字段一致

#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/save/SaveGameRegistry.h>
#include <orange/engine/save/SaveGameSystem.h>
#include <orange/engine/save/SavePath.h>
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

#if defined(_WIN32)
#include <process.h> // _getpid
#else
#include <unistd.h>
#endif

using Orange::Engine::Entity;
using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;
using Orange::Engine::ResultCode;
using Orange::Engine::SchemaVersion;
using Orange::Engine::World;
using Orange::Engine::Save::ResolveSaveDirectory;
using Orange::Engine::Save::ResolveSaveSlotPath;
using Orange::Engine::Save::ResolveUserDataRoot;
using Orange::Engine::Save::SaveableComponent;
using Orange::Engine::Save::SaveGameRegistry;
using Orange::Engine::Save::SaveGameSystem;
using Orange::Engine::Save::SavePathOptions;

namespace
{

    // 用进程 PID 给 vendor 名打 tag —— 多次同时跑不互相踩；测试结束按
    // vendor 整个删，单次 PID 不会跨进程残留。
    std::string TestVendor()
    {
#if defined(_WIN32)
        const int pid = _getpid();
#else
        const int pid = ::getpid();
#endif
        return std::string{"OrangeEngineTest_"} + std::to_string(pid);
    }

    constexpr const char* kTestGame      = "SavePathTestGame";
    constexpr const char* kTestSubfolder = "saves";

    SavePathOptions MakeTestOptions()
    {
        SavePathOptions opt{};
        opt.vendor    = TestVendor();
        opt.game      = kTestGame;
        opt.subfolder = kTestSubfolder;
        return opt;
    }

    // 把 testRoot 下整个 vendor 子目录删掉。失败仅 warn，不让 ctest 失败
    // （%APPDATA% 残留对正式跑测无影响，仅是磁盘占用）。
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
        if (ec)
        {
            std::printf("[save_path_test] warn: 清理 vendor 目录失败 (%s)\n",
                        ec.message().c_str());
        }
    }

    // ---------------------------------------------------------------------------
    // 1. ResolveUserDataRoot 在当前平台行为正确
    // ---------------------------------------------------------------------------
    void TestUserDataRoot()
    {
#if defined(_WIN32)
        auto rc = ResolveUserDataRoot();
        assert(rc.IsOk());
        auto root = std::move(rc).Value();
        assert(!root.empty());
        assert(root.is_absolute());
        // %APPDATA% 在常规 Windows 安装上一定存在
        assert(std::filesystem::exists(root));
#else
        auto rc = ResolveUserDataRoot();
        assert(rc.IsErr());
        assert(rc.Error() == ResultCode::Unsupported);
#endif
    }

    // ---------------------------------------------------------------------------
    // 2. ResolveSaveDirectory 成功 + 目录被创建
    // ---------------------------------------------------------------------------
    void TestResolveSaveDirectoryCreates()
    {
#if !defined(_WIN32)
        return; // 非 Windows 整个 ResolveSaveDirectory 都返 Unsupported
#else
        auto opt = MakeTestOptions();
        auto rc  = ResolveSaveDirectory(opt);
        assert(rc.IsOk());
        auto dir = std::move(rc).Value();
        assert(std::filesystem::exists(dir));
        assert(std::filesystem::is_directory(dir));

        // 路径形态：以 vendor / game / subfolder 三段尾巴结束
        auto last  = dir.filename().string();
        auto mid   = dir.parent_path().filename().string();
        auto first = dir.parent_path().parent_path().filename().string();
        assert(last == kTestSubfolder);
        assert(mid == kTestGame);
        assert(first == TestVendor());
#endif
    }

    // ---------------------------------------------------------------------------
    // 3. ResolveSaveDirectory 输入校验
    // ---------------------------------------------------------------------------
    void TestResolveSaveDirectoryRejectsBadInput()
    {
#if !defined(_WIN32)
        return;
#else
        {
            auto opt = MakeTestOptions();
            opt.game = "";
            auto rc  = ResolveSaveDirectory(opt);
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto opt   = MakeTestOptions();
            opt.vendor = "Vendor/With/Slash";
            auto rc    = ResolveSaveDirectory(opt);
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto opt = MakeTestOptions();
            opt.game = ".."; // 越权 attempt
            auto rc  = ResolveSaveDirectory(opt);
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto opt      = MakeTestOptions();
            opt.subfolder = "."; // 同样 reject "." 防意外目录穿透
            auto rc       = ResolveSaveDirectory(opt);
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto opt = MakeTestOptions();
            opt.game = std::string{"hello\x01world"}; // 控制字符
            auto rc  = ResolveSaveDirectory(opt);
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
#endif
    }

    // ---------------------------------------------------------------------------
    // 4. ResolveSaveSlotPath 拼出 ".save" + 父目录已存在
    // ---------------------------------------------------------------------------
    void TestResolveSaveSlotPath()
    {
#if !defined(_WIN32)
        return;
#else
        auto opt = MakeTestOptions();
        auto rc  = ResolveSaveSlotPath(opt, "slot1");
        assert(rc.IsOk());
        auto path = std::move(rc).Value();
        assert(path.extension() == ".save");
        assert(path.filename() == "slot1.save");
        assert(std::filesystem::exists(path.parent_path()));
        // 文件本身不应预创建
        assert(!std::filesystem::exists(path));
#endif
    }

    // ---------------------------------------------------------------------------
    // 5. ResolveSaveSlotPath 输入校验
    // ---------------------------------------------------------------------------
    void TestResolveSaveSlotPathRejectsBadInput()
    {
#if !defined(_WIN32)
        return;
#else
        auto opt = MakeTestOptions();
        {
            auto rc = ResolveSaveSlotPath(opt, "");
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto rc = ResolveSaveSlotPath(opt, "with/slash");
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
        {
            auto rc = ResolveSaveSlotPath(opt, "..");
            assert(rc.IsErr());
            assert(rc.Error() == ResultCode::InvalidArgument);
        }
#endif
    }

    // ---------------------------------------------------------------------------
    // 6. 端到端：ResolveSaveSlotPath → SaveGameSystem.Save → Load → 字段一致
    // ---------------------------------------------------------------------------
    struct PlayerInventory
    {
        std::int64_t goldCoins{0};
        std::int64_t spiritShards{0};
    };

    void TestEndToEndAcrossPlatformPath()
    {
#if !defined(_WIN32)
        return;
#else
        SaveGameRegistry registry;
        auto             rc = registry.Register<PlayerInventory>(
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
        assert(rc.IsOk());
        SaveGameSystem sys(registry);

        auto pathRc = ResolveSaveSlotPath(MakeTestOptions(), "slot_e2e");
        assert(pathRc.IsOk());
        auto path = std::move(pathRc).Value();

        World  worldA;
        Entity ent = worldA.CreateEntity();
        worldA.Registry().emplace_or_replace<SaveableComponent>(World::ToEntt(ent));
        worldA.AddComponent(ent, PlayerInventory{4321, 13});

        auto saveRc = sys.Save(worldA, path.string());
        assert(saveRc.IsOk());
        assert(std::filesystem::exists(path));

        World worldB;
        auto  loadRc = sys.Load(path.string(), worldB);
        assert(loadRc.IsOk());

        auto view = worldB.Registry().view<const SaveableComponent>();
        assert(view.begin() != view.end());
        Entity restored = World::FromEntt(*view.begin());
        auto*  p        = worldB.GetComponent<PlayerInventory>(restored);
        assert(p != nullptr);
        assert(p->goldCoins == 4321);
        assert(p->spiritShards == 13);

        // 留给 main() 的 CleanupVendor 处理
#endif
    }

} // namespace

int main()
{
    TestUserDataRoot();
    TestResolveSaveDirectoryCreates();
    TestResolveSaveDirectoryRejectsBadInput();
    TestResolveSaveSlotPath();
    TestResolveSaveSlotPathRejectsBadInput();
    TestEndToEndAcrossPlatformPath();

    CleanupVendor();

    std::printf("[save_path_test] all 6 cases passed\n");
    return 0;
}
