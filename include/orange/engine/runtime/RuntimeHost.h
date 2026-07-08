#ifndef ORANGE_ENGINE_RUNTIME_RUNTIME_HOST_H
#define ORANGE_ENGINE_RUNTIME_RUNTIME_HOST_H

// ---------------------------------------------------------------------------
// RuntimeHost —— PIE 发布 runtime 宿主（瘦 main，M10 级别 1，ADR-021）。
//
// 定位：不带编辑器的发布宿主——建窗口、加载启动场景、驱动一个 IGameModule 直接
// 跑游戏。与编辑器 Play 装配**收敛同源**（两宿主共用引擎层 PlayAssembly helper：
// PopulatePhysicsFromWorld / StepSimulation / InstantiateAudioSources），消灭
// 「编辑器能跑、发布行为不同」的漂移。
//
// 典型消费者（per-game 发布 exe，OG 侧）：
//   int main() {
//       SlimeGameModule module;
//       Orange::Engine::Runtime::RuntimeConfig cfg;
//       cfg.windowTitle = "Slime";
//       cfg.startupScene = "assets/scenes/slime.scene.json";
//       return Orange::Engine::Runtime::Run(module, cfg);
//   }
//
// 头隔离：只 include IGameModule.h（无第三方依赖的公共头）+ 标准库，前向声明
// World。颜色 / 重力用裸 float[]（非 glm::vec3）避免拖 glm 到公共面——本头
// 刻意不碰任何 <orange/...> RHI / vulkan / render 头（同 spike 瘦 main）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/game/IGameModule.h>

#include <functional>
#include <string>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Runtime
{

    // 发布 runtime 宿主配置。全字段有合理默认，per-game 发布 exe 按需覆盖。
    struct RuntimeConfig
    {
        // 原生窗口标题 / 初始尺寸。
        std::string windowTitle{"Orange Runtime"};
        int         windowWidth{1280};
        int         windowHeight{720};

        // 启动加载的场景（相对 assetRoot 或绝对）。空 = 不加载场景，起空世界，
        // 靠 onSeedWorld / 游戏模块 OnEnterPlay 自 seed 关卡。
        std::string startupScene;

        // 资产 / 场景解析基准目录（绝对路径）。非空 = 启动即 chdir 过去（兼容相对
        // 路径 IO）；空 = 不 chdir，用当前工作目录。
        std::string assetRoot;

        // 主 pass 入口 clear color（HDR linear，alpha 隐含 1）。裸 float[3] 避免拖 glm。
        float clearColor[3]{0.05f, 0.07f, 0.10f};

        // 天空盒显隐（cubemap 已烘焙时生效，否则回退 clear color）。
        bool skyEnabled{true};

        // 接通引擎托管 ImGui overlay（游戏侧 Layer::OnImGui debug UI）。默认关。
        bool enableDebugImGui{false};

        // 物理世界重力（2D，m/s²）。裸 float[2] 避免拖 glm。
        float gravity[2]{0.0f, -9.81f};

        // 音频 null backend（CI / 无声卡机器）。production 默认 false（真硬件设备）。
        bool useNullAudioBackend{false};

        // 场景加载后、进 Play 之前调一次的世界 seed 钩子（authoring 入口 / 程序化
        // 关卡）。空 = 不 seed。
        std::function<void(World&)> onSeedWorld;
    };

    // 跑发布 runtime。`module` 由调用方拥有（生存期须覆盖整个 Run，含关停）；
    // 宿主经非拥有包装挂进 GameModuleHost 驱动。返回进程退出码（0 = 正常退出）。
    //
    // 装配序：AppHost 建窗 → AssetRegistry + loaders + MaterialSystem → World +
    // PhysicsWorld + Scene::Load（走 PopulatePhysicsFromWorld 同源，不传
    // loadOpts.physicsWorld）+ onSeedWorld → Pipeline.Initialize(window) →
    // module.RegisterRenderPasses → PopulatePhysicsFromWorld → AudioEngine +
    // InstantiateAudioSources → module.EnterPlay → 主循环（StepSimulation +
    // Pipeline.Render）→ 关停 module.ExitPlay + Pipeline.Shutdown。
    ORANGE_ENGINE_API int Run(Game::IGameModule& module, const RuntimeConfig& config);

} // namespace Orange::Engine::Runtime

#endif // ORANGE_ENGINE_RUNTIME_RUNTIME_HOST_H
