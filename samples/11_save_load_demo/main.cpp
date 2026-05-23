// samples/11_save_load_demo —— 把 SaveGameRegistry /
// SaveGameSystem / SlotManager / SavePath / AutosaveScheduler 全部串成
// 一个完整的"玩家进度存取"闭环。
//
// 关卡内容：
//   * player：白方块（toon），WASD / 方向键平移（无物理，纯 transform 操作
//     ——本 sample 把焦点全放在 save/load 上，物理 / 跳跃 / 复杂关卡省去）
//   * 3 个 token：emissive 小方块；player 走到距离 < 0.6m 时被收集（隐藏）
//   * ground：土砖（textured），无碰撞
//   * sun：DirectionalLight 主光
//
// 操作：
//   * WASD / 方向键 —— 移动
//   * F5            —— 手动保存到 slot1（写 sidecar metadata）
//   * F9            —— 加载 slot1（恢复玩家位置 + 已收集 token 数）
//   * F1            —— 列出当前所有 slot 元数据到 console
//   * 关闭窗口或 Esc —— 退出
//
// 自动机制：
//   * 启动时若 slot1 已存在 → 自动 SaveGameSystem.Load 把进度恢复出来；
//     否则 fresh start，PlayerProgress 全 0。
//   * AutosaveScheduler：每 30s 触发一次（throttle 5s），写到 "autosave"
//     slot；最近一次手动 Save 后 Reset 了计时器，避免立刻又 autosave。
//
// 存档格式与文件位置：
//   * Windows：`%APPDATA%/OrangeEngine/SaveLoadDemo/saves/slot1.save` +
//     `slot1.meta.json` 配套 sidecar；autosave 同模式
//   * 文件格式：OSAV header + JSON payload；
//     PlayerProgress schema = `game/PlayerProgress` v1.0

#include "../common/CaptureLayer.h"

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/input/ActionMap.h>
#include <orange/engine/input/InputContext.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/PostProcessPasses.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/save/AutosaveScheduler.h>
#include <orange/engine/save/SaveGameRegistry.h>
#include <orange/engine/save/SaveGameSystem.h>
#include <orange/engine/save/SavePath.h>
#include <orange/engine/save/SaveableComponent.h>
#include <orange/engine/save/SlotManager.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Render::BloomPass;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Save::AutosaveScheduler;
using Orange::Engine::Save::ResolveSaveSlotPath;
using Orange::Engine::Save::SaveableComponent;
using Orange::Engine::Save::SaveGameRegistry;
using Orange::Engine::Save::SaveGameSystem;
using Orange::Engine::Save::SavePathOptions;
using Orange::Engine::Save::SlotManager;
using Orange::Engine::Save::SlotMetadata;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::TransformComponent;
namespace In  = Orange::Engine::Input;
namespace Sce = Orange::Engine::Scene;

namespace
{

// ---------------------------------------------------------------------------
// Mesh 工厂：单位立方体（与 sample 04 / 09 / 10 同布局）
// ---------------------------------------------------------------------------

struct CubeFace
{
    std::array<VertexPosition3, 4> positions;
};

constexpr std::array<CubeFace, 6> kCubeFaces = {{
    {{{{ 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}}}},
    {{{{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}}}},
    {{{{-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}}}},
    {{{{-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}}}},
    {{{{-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}}}},
    {{{{ 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}}}},
}};

constexpr std::array<VertexUV2, 4> kFaceUVs = {{
    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
}};

std::unique_ptr<MeshAsset> MakeCubeMesh()
{
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    positions.reserve(24); uvs.reserve(24); indices.reserve(36);
    for (std::uint32_t face = 0; face < kCubeFaces.size(); ++face)
    {
        const std::uint32_t base = face * 4;
        for (int i = 0; i < 4; ++i)
        {
            positions.push_back(kCubeFaces[face].positions[i]);
            uvs.push_back(kFaceUVs[i]);
        }
        // CCW winding 与 Pipeline FrontFace=CCW + CullMode=Back 对齐
        // （参 GAP-2026-05-22-samples-cube-mesh-winding-bug）。
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    }
    auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                             std::move(uvs),
                                             std::move(indices));
    pMesh->ComputeSmoothNormalsFromTriangles();
    return pMesh;
}

// 按 NameComponent.name 反查 entity（sample 通用 helper）。
Entity FindEntityByName(World& world, std::string_view name)
{
    auto& reg  = world.Registry();
    auto  view = reg.view<NameComponent>();
    for (auto e : view)
    {
        if (reg.get<NameComponent>(e).name == name)
        {
            return World::FromEntt(e);
        }
    }
    return Entity::Invalid();
}

// ---------------------------------------------------------------------------
// 玩家进度（savable）—— 本 sample 唯一注册到 SaveGameRegistry 的 component。
//
// 字段：
//   * tokensCollected：累计收集的 token 数；视觉上 i < tokensCollected 的
//     token entity 被隐藏（visible=false）
//   * playerX / playerY：玩家在 XY 平面上的位置；Save 时由 main loop sync
//     自 player Transform，Load 时 ApplyProgressToWorld 写回 Transform
// ---------------------------------------------------------------------------

struct PlayerProgress
{
    std::int32_t tokensCollected{0};
    float        playerX{-5.0f};
    float        playerY{0.0f};
};

const SchemaVersion kProgressSchema{"game/PlayerProgress", 1, 0};

constexpr int  kTokenCount        = 3;
constexpr float kPlayerSpeed       = 4.5f;
constexpr float kCollectDistance   = 0.6f;
constexpr double kAutosaveInterval = 30.0;
constexpr double kAutosaveThrottle = 5.0;

// ---------------------------------------------------------------------------
// World 反查：拿到当前唯一的 PlayerProgress 实体（约定 sample 内只有一份）。
// ---------------------------------------------------------------------------
Entity FindProgressEntity(World& world)
{
    // 取唯一一个挂 SaveableComponent + PlayerProgress 的 entity。
    // 用 begin() != end() 判定 + 解引用，避免 for-range loop 里
    // `return` 触发 MSVC C4702 unreachable code 警告（与 SaveGameSystemTest
    // 同模式）。
    auto& reg  = world.Registry();
    auto  view = reg.view<SaveableComponent, PlayerProgress>();
    if (view.begin() != view.end())
    {
        return World::FromEntt(*view.begin());
    }
    return Entity::Invalid();
}

void DestroyAllSaveableEntities(World& world)
{
    auto& reg = world.Registry();
    auto  view = reg.view<const SaveableComponent>();
    std::vector<Entity> toDestroy;
    toDestroy.reserve(view.size());
    for (auto e : view) { toDestroy.push_back(World::FromEntt(e)); }
    for (auto e : toDestroy) { world.DestroyEntity(e); }
}

// 把 player Transform 的位置读到 PlayerProgress.playerX/Y —— Save 前调一次。
void SyncProgressFromWorld(World& world, Entity player, Entity progressEntity)
{
    if (!player.IsValid() || !progressEntity.IsValid()) { return; }
    auto* pTx  = world.GetComponent<TransformComponent>(player);
    auto* prog = world.GetComponent<PlayerProgress>(progressEntity);
    if (pTx == nullptr || prog == nullptr) { return; }
    prog->playerX = pTx->position.x;
    prog->playerY = pTx->position.y;
}

// 把 PlayerProgress 写回 World：玩家位置 + token 可见性。Load 后调一次。
void ApplyProgressToWorld(World& world,
                          Entity player,
                          const std::array<Entity, kTokenCount>& tokens,
                          Entity progressEntity)
{
    if (!progressEntity.IsValid()) { return; }
    const auto* prog = world.GetComponent<PlayerProgress>(progressEntity);
    if (prog == nullptr) { return; }

    if (auto* pTx = world.GetComponent<TransformComponent>(player))
    {
        pTx->position.x = prog->playerX;
        pTx->position.y = prog->playerY;
    }
    for (int i = 0; i < kTokenCount; ++i)
    {
        if (!tokens[i].IsValid()) { continue; }
        if (auto* r = world.GetComponent<RenderableComponent>(tokens[i]))
        {
            r->visible = (i >= prog->tokensCollected);
        }
    }
}

// ---------------------------------------------------------------------------
// 启动 / F9 重载：尝试 SaveGameSystem.Load slot1；失败则 fresh-start 创建
// 一个挂 SaveableComponent + PlayerProgress 的 entity。返回该 entity。
// ---------------------------------------------------------------------------
Entity LoadOrCreateProgressEntity(World& world,
                                  const SaveGameSystem& sys,
                                  const std::filesystem::path& slotPath)
{
    DestroyAllSaveableEntities(world);

    if (std::filesystem::exists(slotPath))
    {
        auto rc = sys.Load(slotPath.string(), world);
        if (rc.IsOk())
        {
            Entity loaded = FindProgressEntity(world);
            if (loaded.IsValid())
            {
                std::fprintf(stdout, "[save_load_demo] 加载 slot1 成功\n");
                return loaded;
            }
        }
        else
        {
            std::fprintf(stderr,
                         "[save_load_demo] 加载 slot1 失败 (code=%u) —— fall back fresh start\n",
                         static_cast<unsigned>(rc.Error()));
        }
    }

    // Fresh start
    Entity ent = world.CreateEntity();
    world.Registry().emplace_or_replace<SaveableComponent>(World::ToEntt(ent));
    world.AddComponent(ent, PlayerProgress{});
    std::fprintf(stdout, "[save_load_demo] fresh start（slot1 不存在）\n");
    return ent;
}

// ---------------------------------------------------------------------------
// SaveGameRegistry 注册 —— 集中放在一个 helper 里让 main 干净。
// ---------------------------------------------------------------------------
SaveGameRegistry MakeSaveRegistry()
{
    SaveGameRegistry reg;
    auto rc = reg.Register<PlayerProgress>(
        "PlayerProgress", kProgressSchema,
        [](JsonWriter& w, std::string_view path, const PlayerProgress& c)
        {
            const std::string b(path);
            w.WriteInt  (b + "/tokensCollected", c.tokensCollected);
            w.WriteFloat(b + "/playerX",         static_cast<double>(c.playerX));
            w.WriteFloat(b + "/playerY",         static_cast<double>(c.playerY));
        },
        [](const JsonReader& r, std::string_view path, PlayerProgress& c) -> bool
        {
            const std::string b(path);
            std::int64_t tokens = 0;
            double       x      = 0.0;
            double       y      = 0.0;
            if (!r.ReadInt  (b + "/tokensCollected", tokens)) { return false; }
            if (!r.ReadFloat(b + "/playerX",         x))      { return false; }
            if (!r.ReadFloat(b + "/playerY",         y))      { return false; }
            c.tokensCollected = static_cast<std::int32_t>(tokens);
            c.playerX         = static_cast<float>(x);
            c.playerY         = static_cast<float>(y);
            return true;
        });
    if (rc.IsErr())
    {
        std::fprintf(stderr, "[save_load_demo] SaveGameRegistry.Register 失败\n");
    }
    return reg;
}

// ---------------------------------------------------------------------------
// GameplayLayer：input → 玩家移动 → token 收集 → save/load 触发器。
// ---------------------------------------------------------------------------

class GameplayLayer : public Layer
{
public:
    GameplayLayer(In::InputContext& input,
                  World& world,
                  Entity playerEntity,
                  std::array<Entity, kTokenCount> tokenEntities,
                  Entity progressEntity,
                  const SaveGameSystem& sys,
                  const SlotManager& slotMgr,
                  std::filesystem::path slot1Path)
        : Layer("GameplayLayer")
        , mInput(input)
        , mWorld(world)
        , mPlayer(playerEntity)
        , mTokens(tokenEntities)
        , mProgress(progressEntity)
        , mSys(sys)
        , mSlotMgr(slotMgr)
        , mSlot1Path(std::move(slot1Path))
        , mAutosave(MakeAutosaveConfig(),
                    [this]
                    {
                        DoAutosave();
                    })
    {
        // 初始一次：把（可能是从 slot1 加载来的）progress 同步到世界
        ApplyProgressToWorld(mWorld, mPlayer, mTokens, mProgress);
    }

    void OnUpdate(const FrameContext& frame) override
    {
        const float dt = static_cast<float>(frame.time.deltaSeconds);
        const In::ActionMap* topMap = mInput.Top();
        if (topMap == nullptr) { mInput.BeginFrame(); return; }

        // ---- 玩家移动 ----
        const float vx = (In::IsHeld(topMap->GetState("move_right")) ? 1.0f : 0.0f)
                       - (In::IsHeld(topMap->GetState("move_left"))  ? 1.0f : 0.0f);
        const float vy = (In::IsHeld(topMap->GetState("move_up"))    ? 1.0f : 0.0f)
                       - (In::IsHeld(topMap->GetState("move_down"))  ? 1.0f : 0.0f);
        if (auto* pTx = mWorld.GetComponent<TransformComponent>(mPlayer))
        {
            pTx->position.x += vx * kPlayerSpeed * dt;
            pTx->position.y += vy * kPlayerSpeed * dt;
        }

        // ---- token 收集 ----
        TryCollectTokens();

        // ---- Save / Load 快捷键 ----
        if (In::IsTriggered(topMap->GetState("save_quick")))
        {
            DoManualSave();
        }
        if (In::IsTriggered(topMap->GetState("load_quick")))
        {
            DoManualLoad();
        }
        if (In::IsTriggered(topMap->GetState("list_slots")))
        {
            DoListSlots();
        }

        // ---- Autosave ----
        mAutosave.Update(static_cast<double>(dt));

        mInput.BeginFrame();
    }

    bool OnEvent(const Platform::WindowEvent& event) override
    {
        if (auto* key = std::get_if<Platform::KeyEvent>(&event))
        {
            const bool isDown = key->action != Platform::KeyAction::Release;
            mInput.PostKeyEvent(static_cast<In::KeyCode>(key->key), isDown);
            return false;
        }
        return false;
    }

private:
    static AutosaveScheduler::Config MakeAutosaveConfig()
    {
        AutosaveScheduler::Config c{};
        c.intervalSeconds   = kAutosaveInterval;
        c.minSecondsBetween = kAutosaveThrottle;
        return c;
    }

    void TryCollectTokens()
    {
        auto* prog = mWorld.GetComponent<PlayerProgress>(mProgress);
        auto* pTx  = mWorld.GetComponent<TransformComponent>(mPlayer);
        if (prog == nullptr || pTx == nullptr) { return; }

        for (int i = 0; i < kTokenCount; ++i)
        {
            if (i < prog->tokensCollected) { continue; }   // 已被收集
            if (!mTokens[i].IsValid())     { continue; }
            const auto* tTx = mWorld.GetComponent<TransformComponent>(mTokens[i]);
            if (tTx == nullptr) { continue; }
            const float dx = pTx->position.x - tTx->position.x;
            const float dy = pTx->position.y - tTx->position.y;
            if (dx * dx + dy * dy < kCollectDistance * kCollectDistance)
            {
                // 仅按"距离最近未收集 token 是这个"才生效，避免乱序——
                // 简单方案：如果第 i 个不是当前最低未收集 index 就跳过。
                if (i != prog->tokensCollected) { continue; }
                prog->tokensCollected = i + 1;
                if (auto* r = mWorld.GetComponent<RenderableComponent>(mTokens[i]))
                {
                    r->visible = false;
                }
                std::fprintf(stdout, "[save_load_demo] 收集到 token %d (%d/%d)\n",
                             i, prog->tokensCollected, kTokenCount);
            }
        }
    }

    void DoManualSave()
    {
        SyncProgressFromWorld(mWorld, mPlayer, mProgress);

        SlotMetadata meta{};
        meta.displayName = "Quick Save";
        if (auto* prog = mWorld.GetComponent<PlayerProgress>(mProgress))
        {
            meta.summary = "tokens " + std::to_string(prog->tokensCollected)
                         + "/" + std::to_string(kTokenCount);
        }

        auto rc = mSlotMgr.Save(mSys, mWorld, "slot1", meta);
        if (rc.IsOk())
        {
            std::fprintf(stdout, "[save_load_demo] [F5] slot1 已保存\n");
            mAutosave.Reset();   // 手动存档后，autosave 计时器清零
        }
        else
        {
            std::fprintf(stderr, "[save_load_demo] [F5] slot1 保存失败 (code=%u)\n",
                         static_cast<unsigned>(rc.Error()));
        }
    }

    void DoManualLoad()
    {
        if (!std::filesystem::exists(mSlot1Path))
        {
            std::fprintf(stdout, "[save_load_demo] [F9] slot1 不存在 —— 跳过\n");
            return;
        }
        // 重做 LoadOrCreate 流程：销毁现有 saveable + Load slot1。失败时
        // fresh start —— 与启动路径一致，保证按下 F9 永远把世界拉回某个
        // 一致状态。
        mProgress = LoadOrCreateProgressEntity(mWorld, mSys, mSlot1Path);
        ApplyProgressToWorld(mWorld, mPlayer, mTokens, mProgress);
        mAutosave.Reset();
        std::fprintf(stdout, "[save_load_demo] [F9] slot1 已加载\n");
    }

    void DoListSlots()
    {
        auto rc = mSlotMgr.ListSlots();
        if (rc.IsErr())
        {
            std::fprintf(stderr, "[save_load_demo] [F1] ListSlots 失败 (code=%u)\n",
                         static_cast<unsigned>(rc.Error()));
            return;
        }
        const auto& list = rc.Value();
        std::fprintf(stdout, "[save_load_demo] [F1] %zu 个 slot:\n", list.size());
        for (const auto& m : list)
        {
            std::fprintf(stdout,
                         "    - %-12s  saved=%lld  display='%s'  summary='%s'%s\n",
                         m.slotName.c_str(),
                         static_cast<long long>(m.savedAtUnixSeconds),
                         m.displayName.c_str(),
                         m.summary.c_str(),
                         m.isAutosave ? "  [autosave]" : "");
        }
    }

    void DoAutosave()
    {
        SyncProgressFromWorld(mWorld, mPlayer, mProgress);
        SlotMetadata meta{};
        meta.displayName = "Autosave";
        meta.isAutosave  = true;
        if (auto* prog = mWorld.GetComponent<PlayerProgress>(mProgress))
        {
            meta.summary = "tokens " + std::to_string(prog->tokensCollected)
                         + "/" + std::to_string(kTokenCount);
        }
        auto rc = mSlotMgr.Save(mSys, mWorld, "autosave", meta);
        if (rc.IsOk())
        {
            std::fprintf(stdout, "[save_load_demo] [autosave] 已写出 autosave\n");
        }
        else
        {
            std::fprintf(stderr,
                         "[save_load_demo] [autosave] 失败 (code=%u)\n",
                         static_cast<unsigned>(rc.Error()));
        }
    }

    In::InputContext&                       mInput;
    World&                                  mWorld;
    Entity                                  mPlayer;
    std::array<Entity, kTokenCount>         mTokens;
    Entity                                  mProgress;
    const SaveGameSystem&                   mSys;
    const SlotManager&                      mSlotMgr;
    std::filesystem::path                   mSlot1Path;
    AutosaveScheduler                       mAutosave;
};

// 简单的 RenderLayer —— Pipeline.Render 串到 frame 末尾。
class RenderLayer : public Layer
{
public:
    RenderLayer(Pipeline& pipeline, World& world)
        : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world) {}

    void OnUpdate(const FrameContext& frame) override
    {
        mPipeline.SetFrameTime(static_cast<float>(frame.time.totalSeconds));
        mPipeline.Render(mWorld);
    }

    bool OnEvent(const Platform::WindowEvent& event) override
    {
        if (auto* resize = std::get_if<Platform::WindowResizeEvent>(&event))
        {
            mPipeline.OnResize(resize->width, resize->height);
        }
        return false;
    }

private:
    Pipeline& mPipeline;
    World&    mWorld;
};

}  // namespace

int main(int argc, char** argv)
{
    const auto captureCli = OrangeSamples::ParseCaptureCli(argc, argv);

    // ---- AppHost ----
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 11 save_load_demo (F5=Save F9=Load F1=List)";
    cfg.window.width  = 1280;
    cfg.window.height = 720;
    auto hostResult = AppHost::Create(cfg);
    if (hostResult.IsErr())
    {
        std::fprintf(stderr, "AppHost::Create failed (code=%u)\n",
                     static_cast<unsigned>(hostResult.Error()));
        return 1;
    }
    auto host = std::move(hostResult).Value();

    // ---- Asset / Material ----
    AssetRegistry assets;
    if (assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>()).IsErr())
    {
        std::fprintf(stderr, "RegisterLoader<ShaderAsset> failed\n");
        return 1;
    }
    if (assets.Insert<MeshAsset>("builtin/cube", MakeCubeMesh()).IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset>(builtin/cube) failed\n");
        return 1;
    }
    MaterialSystem materials(assets);
    if (materials.RegisterBuiltins().IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }
    auto playerMat = materials.CreateInstance("toon");
    auto groundMat = materials.CreateInstance("textured");
    auto tokenMat  = materials.CreateInstance("emissive");
    if (!playerMat || !groundMat || !tokenMat)
    {
        std::fprintf(stderr, "CreateInstance failed\n");
        return 1;
    }

    // ---- Input ----
    In::InputContext input;
    auto actionRes = In::LoadActionMapFromFile(ORANGE_SAMPLE_11_ACTIONS_PATH);
    if (actionRes.IsErr())
    {
        std::fprintf(stderr, "LoadActionMapFromFile failed (path=%s, code=%u)\n",
                     ORANGE_SAMPLE_11_ACTIONS_PATH,
                     static_cast<unsigned>(actionRes.Error()));
        return 1;
    }
    input.Push(std::move(actionRes).Value());

    // ---- Scene ----
    World world;
    {
        Sce::LoadOptions opt{};
        opt.assetRegistry = &assets;
        // 本 sample 不用 PhysicsWorld —— 玩家走纯 transform，省去物理 step
        auto rc = Sce::Load(ORANGE_SAMPLE_11_SCENE_PATH, world, opt);
        if (rc.IsErr())
        {
            std::fprintf(stderr, "Scene::Load failed (code=%u)\n",
                         static_cast<unsigned>(rc.Error()));
            return 1;
        }
    }

    // ---- 反查 entities 并 attach materials ----
    Entity playerEntity = FindEntityByName(world, "player");
    Entity groundEntity = FindEntityByName(world, "ground");
    std::array<Entity, kTokenCount> tokens{};
    for (int i = 0; i < kTokenCount; ++i)
    {
        tokens[i] = FindEntityByName(world, std::string("token_") + std::to_string(i));
    }
    if (!playerEntity.IsValid() || !groundEntity.IsValid())
    {
        std::fprintf(stderr, "Scene 缺 player / ground entity\n");
        return 1;
    }
    auto attachMat = [&](Entity e, MaterialInstance* mat)
    {
        if (!e.IsValid()) return;
        if (auto* r = world.GetComponent<RenderableComponent>(e)) { r->materialInstance = mat; }
    };
    attachMat(playerEntity, playerMat.get());
    attachMat(groundEntity, groundMat.get());
    for (auto e : tokens) { attachMat(e, tokenMat.get()); }

    // ---- Camera ----
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 1.5f, 9.0f),
                               glm::vec3(0.0f, 0.5f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    // ---- SaveGame 全家桶 ----
    SaveGameRegistry saveRegistry = MakeSaveRegistry();
    SaveGameSystem   saveSystem(saveRegistry);

    SavePathOptions slotOpts{};
    slotOpts.vendor = "OrangeEngine";
    slotOpts.game   = "SaveLoadDemo";
    SlotManager slotManager(slotOpts);

    auto slot1PathRc = slotManager.ResolveSavePath("slot1");
    if (slot1PathRc.IsErr())
    {
        std::fprintf(stderr,
                     "ResolveSavePath(slot1) failed (code=%u) —— 平台不支持，sample 退出\n",
                     static_cast<unsigned>(slot1PathRc.Error()));
        return 1;
    }
    const std::filesystem::path slot1Path = std::move(slot1PathRc).Value();
    std::fprintf(stdout, "[save_load_demo] slot1 path: %s\n", slot1Path.string().c_str());

    // 启动时尝试自动加载 slot1；失败则 fresh start（创建 PlayerProgress 默认值）
    Entity progressEntity = LoadOrCreateProgressEntity(world, saveSystem, slot1Path);

    // ---- Pipeline + PostProcessChain ----
    Pipeline pipeline;
    if (pipeline.Initialize(host->GetWindow(), assets).IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed\n");
        return 1;
    }
    PostProcessChain chain = CreateDefault();
    if (auto* bp = dynamic_cast<BloomPass*>(chain.FindByName("bloom")))
    {
        bp->threshold = 0.8f;
        bp->intensity = 0.55f;
    }
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);

    // ---- Layers ----
    if (!captureCli.outPath.empty())
    {
        host->PushLayer(std::make_unique<OrangeSamples::CaptureLayer>(
            pipeline, *host, captureCli.outPath, captureCli.captureFrame));
    }
    host->PushLayer(std::make_unique<GameplayLayer>(
        input, world, playerEntity, tokens, progressEntity,
        saveSystem, slotManager, slot1Path));
    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world));

    std::fprintf(stdout,
                 "[save_load_demo] 操作：WASD/方向键移动；F5=保存 slot1；"
                 "F9=加载 slot1；F1=列出 slot；Esc 退出。\n");

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
